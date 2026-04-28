// Tests for nameio/block and nameio/stream volume creation behavior.

#include "test_common.h"
#include <stdint.h>

static const WCHAR* ENCFS_EXE = L".\\encfs.exe";
static const char* ENCFS_PASSWORD = "TEST";

static bool IsDriveMounted(const std::wstring& mountPoint)
{
    WCHAR rootPath[8];
    wsprintfW(rootPath, L"%s\\", mountPoint.c_str());
    return GetDriveTypeW(rootPath) != DRIVE_NO_ROOT_DIR;
}

static bool WaitForDriveState(const std::wstring& mountPoint, bool shouldBeMounted, int timeoutSeconds)
{
    for (int i = 0; i < timeoutSeconds * 10; i++) {
        if (IsDriveMounted(mountPoint) == shouldBeMounted) {
            return true;
        }
        Sleep(100);
    }
    return false;
}

static std::wstring FindFreeMountPoint()
{
    for (WCHAR letter = L'P'; letter <= L'Z'; letter++) {
        WCHAR mountPoint[] = { letter, L':', L'\0' };
        if (!IsDriveMounted(mountPoint)) {
            return mountPoint;
        }
    }
    for (WCHAR letter = L'D'; letter <= L'N'; letter++) {
        WCHAR mountPoint[] = { letter, L':', L'\0' };
        if (!IsDriveMounted(mountPoint)) {
            return mountPoint;
        }
    }
    return L"";
}

static bool DeleteTree(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);

    if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return DeleteFileW(path.c_str()) != FALSE;
    }

    std::wstring search = path + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE find = FindFirstFileW(search.c_str(), &findData);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }

            std::wstring child = path + L"\\" + findData.cFileName;
            if (!DeleteTree(child)) {
                FindClose(find);
                return false;
            }
        } while (FindNextFileW(find, &findData));
        FindClose(find);
    }

    return RemoveDirectoryW(path.c_str()) != FALSE;
}

static std::wstring MakeTempRoot(const WCHAR* suffix)
{
    WCHAR tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath) == 0) {
        return L"";
    }

    WCHAR root[MAX_PATH];
    wsprintfW(root, L"%sEncFSy_%s_%lu", tempPath, suffix, GetCurrentProcessId());
    return root;
}

static bool PrepareEmptyRoot(const std::wstring& root)
{
    if (root.empty()) {
        return false;
    }

    DeleteTree(root);
    if (!CreateDirectoryW(root.c_str(), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            printf("ERROR: Failed to create temp root (error=%lu)\n", static_cast<unsigned long>(err));
            return false;
        }
    }
    return true;
}

static bool ReadFileToString(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open file for read (error=%lu)\n", static_cast<unsigned long>(GetLastError()));
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return false;
    }

    out.resize(static_cast<size_t>(size.QuadPart));
    if (out.empty()) {
        CloseHandle(file);
        return true;
    }

    DWORD read = 0;
    BOOL ok = ReadFile(file, &out[0], static_cast<DWORD>(out.size()), &read, NULL);
    CloseHandle(file);
    if (!ok || static_cast<size_t>(read) != out.size()) {
        return false;
    }
    return true;
}

static bool ConfigHasNameAlgorithm(const std::wstring& root, const char* expectedName, const char* expectedMajor)
{
    std::wstring configPath = root + L"\\.encfs6.xml";
    std::string xml;
    if (!ReadFileToString(configPath, xml)) {
        return false;
    }

    size_t start = xml.find("<nameAlg>");
    size_t end = xml.find("</nameAlg>", start);
    if (start == std::string::npos || end == std::string::npos) {
        printf("ERROR: nameAlg block not found in .encfs6.xml\n");
        return false;
    }

    std::string block = xml.substr(start, end - start);
    std::string nameTag = std::string("<name>") + expectedName + "</name>";
    std::string majorTag = std::string("<major>") + expectedMajor + "</major>";
    if (block.find(nameTag) == std::string::npos) {
        printf("ERROR: Expected %s in nameAlg block\n", nameTag.c_str());
        return false;
    }
    if (block.find(majorTag) == std::string::npos) {
        printf("ERROR: Expected %s in nameAlg block\n", majorTag.c_str());
        return false;
    }
    return true;
}

static bool PathExistsLocal(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool StartEncFS(const std::wstring& root, const std::wstring& mountPoint,
                       bool nameIoStream, int passwordPromptCount)
{
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE readPipe = NULL;
    HANDLE writePipe = NULL;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        printf("ERROR: Failed to create stdin pipe\n");
        return false;
    }
    SetHandleInformation(writePipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"\"";
    cmd += ENCFS_EXE;
    cmd += L"\" \"";
    cmd += root;
    cmd += L"\" ";
    cmd += mountPoint;
    cmd += L" --dokan-mount-manager --alt-stream";
    if (nameIoStream) {
        cmd += L" --nameio-stream";
    }

    std::vector<WCHAR> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = readPipe;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    wprintf(L"Executing: %s\n", cmd.c_str());
    if (!CreateProcessW(NULL, cmdLine.data(), NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        printf("ERROR: Failed to start encfs.exe (error=%lu)\n", static_cast<unsigned long>(GetLastError()));
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return false;
    }

    Sleep(1500);

    char passwordWithNewline[64];
    sprintf_s(passwordWithNewline, "%s\r\n", ENCFS_PASSWORD);
    for (int i = 0; i < passwordPromptCount; i++) {
        DWORD written = 0;
        WriteFile(writePipe, passwordWithNewline, static_cast<DWORD>(strlen(passwordWithNewline)), &written, NULL);
        FlushFileBuffers(writePipe);
        Sleep(500);
    }

    CloseHandle(writePipe);
    CloseHandle(readPipe);

    if (!WaitForDriveState(mountPoint, true, 30)) {
        printf("ERROR: Timeout waiting for %ls to mount\n", mountPoint.c_str());
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

static bool UnmountEncFS(const std::wstring& mountPoint)
{
    if (!IsDriveMounted(mountPoint)) {
        return true;
    }

    std::wstring cmd = L"\"";
    cmd += ENCFS_EXE;
    cmd += L"\" -u ";
    cmd += mountPoint;

    std::vector<WCHAR> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(NULL, cmdLine.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("ERROR: Failed to start unmount command (error=%lu)\n", static_cast<unsigned long>(GetLastError()));
        return false;
    }

    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return WaitForDriveState(mountPoint, false, 10);
}

static bool WriteMountedTestFile(const std::wstring& mountPoint,
                                 const std::wstring& fileName,
                                 const char* expected)
{
    DWORD expectedLen = static_cast<DWORD>(strlen(expected));
    std::wstring path = mountPoint + L"\\" + fileName;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to create test file (error=%lu)\n", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    DWORD written = 0;
    BOOL writeOk = WriteFile(file, expected, expectedLen, &written, NULL);
    CloseHandle(file);
    if (!writeOk || written != expectedLen) {
        printf("ERROR: Failed to write test file\n");
        return false;
    }
    return true;
}

static bool ReadMountedTestFile(const std::wstring& mountPoint,
                                const std::wstring& fileName,
                                const char* expected)
{
    std::wstring path = mountPoint + L"\\" + fileName;
    std::string actual;
    if (!ReadFileToString(path, actual)) {
        return false;
    }

    if (actual != expected) {
        printf("ERROR: File contents mismatch on remounted nameio/stream volume\n");
        return false;
    }
    return true;
}

bool Test_NameIOStreamNewVolume(const WCHAR*)
{
    std::wstring mountPoint = FindFreeMountPoint();
    std::wstring root = MakeTempRoot(L"nameio_stream");
    if (mountPoint.empty() || !PrepareEmptyRoot(root)) {
        printf("ERROR: Failed to prepare nameio/stream test volume\n");
        return false;
    }

    bool passed = false;
    const std::wstring testName = L"nameio_stream_roundtrip_document.txt";
    const char* expected = "nameio-stream-encrypt-decrypt-contents";
    if (StartEncFS(root, mountPoint, true, 2)) {
        passed = ConfigHasNameAlgorithm(root, "nameio/stream", "2") &&
                 WriteMountedTestFile(mountPoint, testName, expected) &&
                 !PathExistsLocal(root + L"\\" + testName);
    }

    bool firstUnmount = UnmountEncFS(mountPoint);
    if (passed && firstUnmount && StartEncFS(root, mountPoint, false, 1)) {
        passed = ReadMountedTestFile(mountPoint, testName, expected);
    } else {
        passed = false;
    }

    bool secondUnmount = UnmountEncFS(mountPoint);
    bool cleaned = DeleteTree(root);
    return passed && firstUnmount && secondUnmount && cleaned;
}

bool Test_NameIOStreamIgnoredForExistingVolume(const WCHAR*)
{
    std::wstring mountPoint = FindFreeMountPoint();
    std::wstring root = MakeTempRoot(L"nameio_existing_block");
    if (mountPoint.empty() || !PrepareEmptyRoot(root)) {
        printf("ERROR: Failed to prepare existing-volume nameio test\n");
        return false;
    }

    bool passed = false;
    if (StartEncFS(root, mountPoint, false, 2)) {
        passed = ConfigHasNameAlgorithm(root, "nameio/block", "3");
    }
    bool firstUnmount = UnmountEncFS(mountPoint);

    if (passed && firstUnmount && StartEncFS(root, mountPoint, true, 1)) {
        passed = ConfigHasNameAlgorithm(root, "nameio/block", "3");
    } else {
        passed = false;
    }

    bool secondUnmount = UnmountEncFS(mountPoint);
    bool cleaned = DeleteTree(root);
    return passed && firstUnmount && secondUnmount && cleaned;
}
