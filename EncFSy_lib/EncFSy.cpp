/**
 * @file EncFS.cpp
 * @brief Main implementation file containing Dokan callbacks and global state.
 * @details This file defines global state and contains all Dokan filesystem operation
 * callbacks. It has been optimized for safety and performance by using
 * stack allocation for paths, RAII for resource management, and safer
 * context handling to prevent memory leaks.
 */

#include <dokan.h>
#include "EncFSy.h"

#include <fileinfo.h>
#include <winbase.h>
#include <string>
#include <fstream>
#include <streambuf>
#include <vector>
#include <memory>
#include <mutex>
#include <codecvt>

#include "EncFSFile.h"
#include "EncFSUtils.hpp"

#include "EncFSy_globals.h"
#include "EncFSy_logging.h"
#include "EncFSy_path.h"
#include "EncFSy_mount.h"
#include "EncFSy_dokan.h"

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

using namespace std;

EncFS::EncFSVolume encfs;
EncFSOptions g_efo;
std::mutex dirMoveLock;

/**
 * @brief Adds the SE_SECURITY_NAME privilege to the process token.
 * @details This privilege is required to read or write the SACL of an object's
 * security descriptor.
 */
static BOOL AddSeSecurityNamePrivilege() {
    HANDLE token = 0;
    DbgPrint(L"## Attempting to add SE_SECURITY_NAME privilege to process token ##\n");
    DWORD err;
    LUID luid;
    if (!LookupPrivilegeValue(0, SE_SECURITY_NAME, &luid)) {
        err = GetLastError();
        ErrorPrint(L"  failed: Unable to lookup privilege value. error = %u\n", err);
        return FALSE;
    }

    LUID_AND_ATTRIBUTES attr;
    attr.Attributes = SE_PRIVILEGE_ENABLED;
    attr.Luid = luid;

    TOKEN_PRIVILEGES priv;
    priv.PrivilegeCount = 1;
    priv.Privileges[0] = attr;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        err = GetLastError();
        ErrorPrint(L"  failed: Unable obtain process token. error = %u\n", err);
        return FALSE;
    }
    auto close_handle = [](HANDLE h) { CloseHandle(h); };
    std::unique_ptr<void, decltype(close_handle)> token_guard(token, close_handle);

    TOKEN_PRIVILEGES oldPriv;
    DWORD retSize;
    AdjustTokenPrivileges(token, FALSE, &priv, sizeof(TOKEN_PRIVILEGES), &oldPriv, &retSize);
    err = GetLastError();
    if (err != ERROR_SUCCESS) {
        ErrorPrint(L"  failed: Unable to adjust token privileges: %u\n", err);
        return FALSE;
    }

    BOOL privAlreadyPresent = FALSE;
    for (unsigned int i = 0; i < oldPriv.PrivilegeCount; i++) {
        if (oldPriv.Privileges[i].Luid.HighPart == luid.HighPart &&
            oldPriv.Privileges[i].Luid.LowPart == luid.LowPart) {
            privAlreadyPresent = TRUE;
            break;
        }
    }
    DbgPrint(privAlreadyPresent ? L"  success: privilege already present\n"
        : L"  success: privilege added\n");
    return TRUE;
}

/**
 * @brief Dokan callback executed when the volume is successfully mounted.
 */
static NTSTATUS DOKAN_CALLBACK EncFSMounted(LPCWSTR MountPoint, PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);
    InfoPrint(L"Mounted as %s\n", MountPoint);
    // Open explorer window on mount point if not in debug mode.
    if (!g_efo.g_DebugMode) {
        wchar_t buff[20];
        if (swprintf_s(buff, 20, L"%s:\\", MountPoint) != -1) {
            ShellExecuteW(NULL, NULL, buff, NULL, NULL, SW_SHOWNORMAL);
        }
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback executed when the volume is unmounted.
 */
static NTSTATUS DOKAN_CALLBACK EncFSUnmounted(PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);
    InfoPrint(L"Unmounted\n");
    return STATUS_SUCCESS;
}

/**
 * @brief Console control handler to gracefully unmount the drive on exit.
 */
BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
    switch (dwCtrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        SetConsoleCtrlHandler(CtrlHandler, FALSE);
        DokanRemoveMountPoint(g_efo.MountPoint);
        return TRUE;
    default:
        return FALSE;
    }
}

#define CONFIG_XML "\\.encfs6.xml"
/**
 * @brief Checks if an EncFS filesystem exists in the given directory.
 */
bool IsEncFSExists(LPCWSTR rootDir) {
    const wstring wRootDir(rootDir);
    wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
    string cRootDir = strConv.to_bytes(wRootDir);
    string configFile = cRootDir + CONFIG_XML;
    ifstream in(configFile);
    return in.is_open();
}

/**
 * @brief Creates a new EncFS filesystem configuration.
 */
int CreateEncFS(LPCWSTR rootDir, char* password, EncFSMode mode, bool reverse) {
    const wstring wRootDir(rootDir);
    wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
    string cRootDir = strConv.to_bytes(wRootDir);
    string configFile = cRootDir + CONFIG_XML;

    ifstream in(configFile);
    if (in.is_open()) return EXIT_FAILURE; // Already exists.

    encfs.create(password, (EncFS::EncFSMode)mode, reverse);
    string xml;
    encfs.save(xml);
    ofstream out(configFile);
    out << xml;
    out.close();
    return EXIT_SUCCESS;
}

/**
 * @brief Main function to initialize and start the Dokan filesystem.
 */
int StartEncFS(EncFSOptions& efo, char* password) {
    DOKAN_OPERATIONS dokanOperations{};
    DOKAN_OPTIONS dokanOptions{};

    encfs.altStream = efo.AltStream;
    encfs.cloudConflict = efo.CloudConflict;
    string configFile;
    wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;

    const wstring wRootDir(efo.RootDirectory);
    string cRootDir = strConv.to_bytes(wRootDir);
    configFile = cRootDir + CONFIG_XML;

    try {
        ifstream in(configFile);
        if (in.is_open()) {
            string xml((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
            in.close();
            encfs.load(xml, efo.Reverse);
        }
        else {
            return EXIT_FAILURE;
        }
    }
    catch (const EncFS::EncFSBadConfigurationException& ex) {
        printf("%s\n", ex.what());
        return EXIT_FAILURE;
    }

    // Set Dokan options from our configuration.
    dokanOptions.Version = DOKAN_VERSION;
    dokanOptions.Timeout = efo.Timeout;
    dokanOptions.MountPoint = efo.MountPoint;
    dokanOptions.Options = efo.DokanOptions;
    dokanOptions.AllocationUnitSize = efo.AllocationUnitSize;
    dokanOptions.SectorSize = efo.SectorSize;

    if (efo.UNCName && wcscmp(efo.UNCName, L"") != 0 && !(dokanOptions.Options & DOKAN_OPTION_NETWORK)) {
        fwprintf(stderr, L"  Warning: UNC provider name should be set on network drive only.\n");
        dokanOptions.UNCName = efo.UNCName;
    }
    else {
        dokanOptions.UNCName = L"";
    }

    // Validate option combinations.
    if (dokanOptions.Options & DOKAN_OPTION_NETWORK && dokanOptions.Options & DOKAN_OPTION_MOUNT_MANAGER) {
        fwprintf(stderr, L"Mount manager cannot be used on network drive.\n");
        return EXIT_FAILURE;
    }
    if (!(dokanOptions.Options & DOKAN_OPTION_MOUNT_MANAGER) && wcscmp(efo.MountPoint, L"") == 0) {
        fwprintf(stderr, L"Mount Point required.\n");
        return EXIT_FAILURE;
    }
    if ((dokanOptions.Options & DOKAN_OPTION_MOUNT_MANAGER) && (dokanOptions.Options & DOKAN_OPTION_CURRENT_SESSION)) {
        fwprintf(stderr, L"Mount Manager always mount the drive for all user sessions.\n");
        return EXIT_FAILURE;
    }

    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        fwprintf(stderr, L"Control Handler is not set.\n");
    }

    if (efo.g_ImpersonateCallerUser && !efo.g_HasSeSecurityPrivilege) {
        fwprintf(stderr, L"Impersonate Caller User requires administrator right to work properly\n");
    }

    // Set debug, write-protect, and stream options.
    if (efo.g_DokanDebug) {
        if (efo.g_DebugMode) dokanOptions.Options |= DOKAN_OPTION_DEBUG;
        if (efo.g_UseStdErr) dokanOptions.Options |= DOKAN_OPTION_STDERR;
    }
    if (efo.Reverse) dokanOptions.Options |= DOKAN_OPTION_WRITE_PROTECT;
    if (efo.AltStream) dokanOptions.Options |= DOKAN_OPTION_ALT_STREAM;
    
    // Only set case-sensitive flag if CaseInsensitive is not enabled
    if (!efo.CaseInsensitive) {
        dokanOptions.Options |= DOKAN_OPTION_CASE_SENSITIVE;
    }

    // Map our functions to the Dokan operation callbacks.
    dokanOperations.ZwCreateFile = EncFSCreateFile;
    dokanOperations.Cleanup = EncFSCleanup;
    dokanOperations.CloseFile = EncFSCloseFile;
    dokanOperations.ReadFile = EncFSReadFile;
    dokanOperations.WriteFile = EncFSWriteFile;
    dokanOperations.FlushFileBuffers = EncFSFlushFileBuffers;
    dokanOperations.GetFileInformation = EncFSGetFileInformation;
    dokanOperations.FindFiles = EncFSFindFiles;
    dokanOperations.FindFilesWithPattern = NULL; // Not implemented.
    dokanOperations.SetFileAttributes = EncFSSetFileAttributes;
    dokanOperations.SetFileTime = EncFSSetFileTime;
    dokanOperations.DeleteFile = EncFSDeleteFile;
    dokanOperations.DeleteDirectory = EncFSDeleteDirectory;
    dokanOperations.MoveFile = EncFSMoveFile;
    dokanOperations.SetEndOfFile = EncFSSetEndOfFile;
    dokanOperations.SetAllocationSize = EncFSSetAllocationSize;
    dokanOperations.LockFile = EncFSLockFile;
    dokanOperations.UnlockFile = EncFSUnlockFile;
    dokanOperations.GetFileSecurity = EncFSGetFileSecurity;
    dokanOperations.SetFileSecurity = EncFSSetFileSecurity;
    dokanOperations.GetDiskFreeSpace = EncFSDokanGetDiskFreeSpace;
    dokanOperations.GetVolumeInformation = EncFSGetVolumeInformation;
    dokanOperations.Unmounted = EncFSUnmounted;
    dokanOperations.Mounted = EncFSMounted;
    dokanOperations.FindStreams = efo.AltStream ? EncFSFindStreams : NULL;

    // Unlock the filesystem with the provided password.
    try {
        encfs.unlock(password);
    }
    catch (const EncFS::EncFSUnlockFailedException& ex) {
        printf("%s\n", ex.what());
        return EXIT_FAILURE;
    }

    g_efo = efo;
    DokanInit();
    // This call blocks until the filesystem is unmounted.
    int status = DokanMain(&dokanOptions, &dokanOperations);
    DokanShutdown();

    // Report the final status.
    switch (status) {
    case DOKAN_SUCCESS: fprintf(stderr, "Success\n"); break;
    case DOKAN_ERROR: fprintf(stderr, "Error\n"); break;
    case DOKAN_DRIVE_LETTER_ERROR: fprintf(stderr, "Bad Drive letter\n"); break;
    case DOKAN_DRIVER_INSTALL_ERROR: fprintf(stderr, "Can't install driver\n"); break;
    case DOKAN_START_ERROR: fprintf(stderr, "Driver something wrong\n"); break;
    case DOKAN_MOUNT_ERROR: fprintf(stderr, "Can't assign a drive letter\n"); break;
    case DOKAN_MOUNT_POINT_ERROR: fprintf(stderr, "Mount point error\n"); break;
    case DOKAN_VERSION_ERROR: fprintf(stderr, "Version error\n"); break;
    default: fprintf(stderr, "Unknown error: %d\n", status); break;
    }
    return EXIT_SUCCESS;
}