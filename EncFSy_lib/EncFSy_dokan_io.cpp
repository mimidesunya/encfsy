/**
 * @file EncFSy_dokan_io.cpp
 * @brief Dokan filesystem callback implementations for I/O operations.
 *
 * This file implements Dokan callbacks for file creation, reading, writing,
 * directory enumeration, file deletion, moving/renaming, and stream handling.
 */

#include "EncFSy_dokan.h"
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

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

using namespace std;

extern EncFS::EncFSVolume encfs;
extern EncFSOptions g_efo;
extern std::mutex dirMoveLock;

/**
 * @brief Converts Dokan context to EncFSFile pointer.
 * @param context The ULONG64 context from DokanFileInfo.
 * @return Pointer to the EncFSFile object.
 */
static inline EncFS::EncFSFile* ToEncFSFile(ULONG64 context) {
    return reinterpret_cast<EncFS::EncFSFile*>(context);
}

/**
 * @brief Prints the requesting user's account name (debug mode only).
 * @param DokanFileInfo Dokan file context containing requestor token.
 */
static void PrintUserName(PDOKAN_FILE_INFO DokanFileInfo) {
    if (!g_efo.g_DebugMode)
        return;

    HANDLE handle = DokanOpenRequestorToken(DokanFileInfo);
    if (handle == INVALID_HANDLE_VALUE) {
        DbgPrint(L"  [DEBUG] DokanOpenRequestorToken failed\n");
        return;
    }
    // RAII guard for token handle
    auto close_handle = [](HANDLE h) { CloseHandle(h); };
    std::unique_ptr<void, decltype(close_handle)> handle_guard(handle, close_handle);

    UCHAR buffer[1024];
    DWORD returnLength;
    if (!GetTokenInformation(handle, TokenUser, buffer, sizeof(buffer), &returnLength)) {
        DbgPrint(L"  [DEBUG] GetTokenInformation failed (error=%lu)\n", GetLastError());
        return;
    }

    WCHAR accountName[256];
    WCHAR domainName[256];
    DWORD accountLength = sizeof(accountName) / sizeof(WCHAR);
    DWORD domainLength = sizeof(domainName) / sizeof(WCHAR);
    SID_NAME_USE snu;
    PTOKEN_USER tokenUser = (PTOKEN_USER)buffer;

    if (!LookupAccountSid(NULL, tokenUser->User.Sid, accountName, &accountLength,
        domainName, &domainLength, &snu)) {
        DbgPrint(L"  [DEBUG] LookupAccountSid failed (error=%lu)\n", GetLastError());
        return;
    }

    DbgPrint(L"  [DEBUG] Requestor: %s\\%s\n", domainName, accountName);
}

/**
 * @brief Creates or opens a file or directory.
 *
 * This is the main entry point for file/directory access. Handles encryption
 * requirements, impersonation, case-insensitive mode, and access permissions.
 *
 * @param FileName Virtual path of the file.
 * @param SecurityContext Security context for the operation.
 * @param DesiredAccess Requested access rights.
 * @param FileAttributes File attributes for new files.
 * @param ShareAccess Share mode flags.
 * @param CreateDisposition Create/open disposition.
 * @param CreateOptions Additional create options.
 * @param DokanFileInfo Dokan file context (output).
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
    ACCESS_MASK DesiredAccess, ULONG FileAttributes,
    ULONG ShareAccess, ULONG CreateDisposition,
    ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo) {

    // Validate input parameters
    if (!FileName) {
        ErrorPrint(L"CreateFile: FileName is NULL\n");
        return STATUS_INVALID_PARAMETER;
    }
    size_t nameLen = wcsnlen_s(FileName, 32768);
    if (nameLen == 0 || nameLen >= 32768) {
        ErrorPrint(L"CreateFile: FileName length invalid (%zu)\n", nameLen);
        return STATUS_INVALID_PARAMETER;
    }

    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD fileAttr;
    NTSTATUS status = STATUS_SUCCESS;
    DWORD creationDisposition;
    DWORD fileAttributesAndFlags;
    DWORD error = 0;
    SECURITY_ATTRIBUTES securityAttrib;
    ACCESS_MASK genericDesiredAccess;
    HANDLE userTokenHandle = INVALID_HANDLE_VALUE;

    securityAttrib.nLength = sizeof(securityAttrib);
    securityAttrib.lpSecurityDescriptor = SecurityContext->AccessState.SecurityDescriptor;
    securityAttrib.bInheritHandle = FALSE;

    // Convert kernel-mode flags to user-mode equivalents
    DokanMapKernelToUserCreateFileFlags(
        DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
        &genericDesiredAccess, &fileAttributesAndFlags, &creationDisposition);

    // EncFS requires buffered I/O for encryption block alignment
    fileAttributesAndFlags &= ~FILE_FLAG_NO_BUFFERING;

    // Case-insensitive collision check for new file creation
    if (g_efo.CaseInsensitive && (creationDisposition == CREATE_NEW || creationDisposition == CREATE_ALWAYS)) {
        if (PlainPathExistsCaseInsensitive(FileName)) {
            if (creationDisposition == CREATE_NEW) {
                DbgPrint(L"CreateFile: '%s' collision (case-insensitive)\n", FileName);
                return STATUS_OBJECT_NAME_COLLISION;
            }
        }
    }

    // Convert virtual path to encrypted physical path
    try {
        GetFilePath(filePath, FileName,
            creationDisposition == CREATE_NEW || creationDisposition == CREATE_ALWAYS);
        if (wcslen(filePath) < 8) {
            ErrorPrint(L"CreateFile: Path too short for '%s'\n", FileName);
            return STATUS_OBJECT_NAME_INVALID;
        }
    }
    catch (const std::exception& ex) {
        ErrorPrint(L"CreateFile: Path conversion failed for '%s': %S\n", FileName, ex.what());
        return STATUS_OBJECT_NAME_INVALID;
    }

    DbgPrint(L"CreateFile: '%s' -> '%s' (access=0x%08X, share=0x%X, disp=%lu, opt=0x%X)\n",
        FileName, filePath, DesiredAccess, ShareAccess, creationDisposition, CreateOptions);
    PrintUserName(DokanFileInfo);

    fileAttr = GetFileAttributesW(filePath);

    // Check if target is a directory
    if (fileAttr != INVALID_FILE_ATTRIBUTES) {
        if (fileAttr & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(CreateOptions & FILE_NON_DIRECTORY_FILE)) {
                DokanFileInfo->IsDirectory = TRUE;
                ShareAccess |= FILE_SHARE_READ;
            } else {
                DbgPrint(L"  [INFO] Directory found but FILE_NON_DIRECTORY_FILE specified\n");
                return STATUS_FILE_IS_A_DIRECTORY;
            }
        }
    }

    // Enable POSIX semantics for case-sensitive mode
    if (!g_efo.CaseInsensitive) {
        fileAttributesAndFlags |= FILE_FLAG_POSIX_SEMANTICS;
    }

    // Setup impersonation if enabled
    if (g_efo.g_ImpersonateCallerUser) {
        userTokenHandle = DokanOpenRequestorToken(DokanFileInfo);
    }
    // RAII helper for impersonation cleanup
    struct AutoRevert { 
        HANDLE token; 
        bool active; 
        AutoRevert(HANDLE t):token(t),active(t!=INVALID_HANDLE_VALUE){} 
        ~AutoRevert(){ if(active){ RevertToSelf(); CloseHandle(token);} } 
    } reverter(userTokenHandle);

    if (DokanFileInfo->IsDirectory) {
        // Directory creation/opening
        if (creationDisposition == CREATE_NEW || creationDisposition == OPEN_ALWAYS) {
            if (reverter.active) ImpersonateLoggedOnUser(reverter.token);
            if (!CreateDirectory(filePath, &securityAttrib)) {
                error = GetLastError();
                if (error != ERROR_ALREADY_EXISTS || creationDisposition == CREATE_NEW) {
                    ErrorPrint(L"CreateFile: CreateDirectory '%s' FAILED (error=%lu)\n", FileName, error);
                    if (reverter.active) RevertToSelf();
                    return DokanNtStatusFromWin32(error);
                }
                DbgPrint(L"  [INFO] Directory already exists\n");
            }
            if (reverter.active) RevertToSelf();
        }

        if (status == STATUS_SUCCESS) {
            // Validate directory expectations
            if (fileAttr != INVALID_FILE_ATTRIBUTES &&
                !(fileAttr & FILE_ATTRIBUTE_DIRECTORY) &&
                (CreateOptions & FILE_DIRECTORY_FILE)) {
                DbgPrint(L"  [INFO] Expected directory but found file\n");
                return STATUS_NOT_A_DIRECTORY;
            }
            // Open directory handle
            if (reverter.active) ImpersonateLoggedOnUser(reverter.token);
            handle = CreateFileW(filePath, genericDesiredAccess, ShareAccess,
                &securityAttrib, OPEN_EXISTING,
                fileAttributesAndFlags | FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (reverter.active) RevertToSelf();

            if (handle == INVALID_HANDLE_VALUE) {
                error = GetLastError();
                ErrorPrint(L"CreateFile: Open directory '%s' FAILED (error=%lu)\n", FileName, error);
                status = DokanNtStatusFromWin32(error);
            } else {
                DokanFileInfo->Context = (ULONG64)new EncFS::EncFSFile(handle, false);
                if (creationDisposition == OPEN_ALWAYS && fileAttr != INVALID_FILE_ATTRIBUTES) {
                    status = STATUS_OBJECT_NAME_COLLISION;
                }
            }
        }
    } else {
        // File creation/opening

        // Prevent delete-on-close for read-only files
        if ((fileAttr != INVALID_FILE_ATTRIBUTES && (fileAttr & FILE_ATTRIBUTE_READONLY) ||
            (fileAttributesAndFlags & FILE_ATTRIBUTE_READONLY)) &&
            (fileAttributesAndFlags & FILE_FLAG_DELETE_ON_CLOSE)) {
            DbgPrint(L"  [INFO] Cannot delete read-only file\n");
            return STATUS_CANNOT_DELETE;
        }
        
        // TRUNCATE_EXISTING requires write access
        if (creationDisposition == TRUNCATE_EXISTING) {
            genericDesiredAccess |= GENERIC_WRITE;
        }
        
        if (reverter.active) ImpersonateLoggedOnUser(reverter.token);
        
        bool fileExists = (fileAttr != INVALID_FILE_ATTRIBUTES);
        bool isCreatingNewFile = (!fileExists && 
            (creationDisposition == CREATE_NEW || 
             creationDisposition == CREATE_ALWAYS || 
             creationDisposition == OPEN_ALWAYS));
        bool isTruncating = (fileExists && 
            (creationDisposition == CREATE_ALWAYS || 
             creationDisposition == TRUNCATE_EXISTING));
        
        // EncFS requires read access for encryption/decryption operations
        if (fileExists && (genericDesiredAccess & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA))) {
            genericDesiredAccess |= FILE_READ_DATA | FILE_WRITE_DATA;
            if (ShareAccess & FILE_SHARE_WRITE) ShareAccess |= FILE_SHARE_READ;
        }
        if (fileExists && (genericDesiredAccess & DELETE)) {
            genericDesiredAccess |= FILE_READ_DATA | FILE_WRITE_DATA;
            if (ShareAccess & FILE_SHARE_DELETE) ShareAccess |= FILE_SHARE_WRITE | FILE_SHARE_READ;
        }
        
        // New/truncated files need read access for IV operations
        if (isCreatingNewFile || isTruncating) {
            genericDesiredAccess |= FILE_READ_DATA | FILE_WRITE_DATA;
        }
        
        // Separate file attributes (low word) from flags (high word)
        DWORD attrPart = fileAttributesAndFlags & 0x0000FFFF;
        DWORD flagPart = fileAttributesAndFlags & 0xFFFF0000;
        
        // Preserve HIDDEN/SYSTEM attributes for truncate operations (Windows requirement)
        if (fileExists && (creationDisposition == CREATE_ALWAYS || creationDisposition == TRUNCATE_EXISTING)) {
            attrPart |= (fileAttr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
        }
        
        // Normalize attributes: NORMAL is only valid when alone
        if (attrPart == 0) {
            attrPart = FILE_ATTRIBUTE_NORMAL;
        }
        if ((attrPart & ~FILE_ATTRIBUTE_NORMAL) != 0) {
            attrPart &= ~FILE_ATTRIBUTE_NORMAL;
        }
        
        DWORD finalAttributes = attrPart | flagPart;
        
        handle = CreateFileW(filePath, genericDesiredAccess, ShareAccess,
            &securityAttrib, creationDisposition, finalAttributes, NULL);
        
        if (reverter.active) RevertToSelf();
        
        if (handle == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            DbgPrint(L"CreateFile: Open file '%s' FAILED (error=%lu, access=0x%08X, disp=%lu, attr=0x%08X)\n", 
                FileName, error, genericDesiredAccess, creationDisposition, finalAttributes);
            status = DokanNtStatusFromWin32(error);
        } else {
            // Determine read capability for EncFS operations
            bool canRead = (genericDesiredAccess & (GENERIC_READ | FILE_READ_DATA)) != 0 ||
                           isCreatingNewFile || isTruncating;
            
            DokanFileInfo->Context = (ULONG64)new EncFS::EncFSFile(handle, canRead);
            
            if (creationDisposition == OPEN_ALWAYS || creationDisposition == CREATE_ALWAYS) {
                error = GetLastError();
                if (error == ERROR_ALREADY_EXISTS) {
                    status = STATUS_OBJECT_NAME_COLLISION;
                }
            }
        }
    }

    DbgPrint(L"CreateFile: '%s' completed (status=0x%08X, isDir=%d)\n", FileName, status, DokanFileInfo->IsDirectory);
    return status;
}

/**
 * @brief Called when all handles to a file are closed.
 *
 * This is a notification callback. The actual cleanup should have been
 * done in EncFSCleanup. If Context is still set, it indicates an error.
 *
 * @param FileName Virtual path of the file.
 * @param DokanFileInfo Dokan file context.
 */
void DOKAN_CALLBACK EncFSCloseFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    DbgPrint(L"CloseFile: '%s'\n", FileName);

    if (DokanFileInfo->Context) {
        // Context should have been cleared in Cleanup
        ErrorPrint(L"CloseFile: '%s' - Context not cleaned up in Cleanup!\n", FileName);
        // Use atomic exchange to prevent double-delete in concurrent scenarios
        ULONG64 ctx = InterlockedExchange64((LONG64*)&DokanFileInfo->Context, 0);
        if (ctx) {
            EncFS::EncFSFile* encfsFile = reinterpret_cast<EncFS::EncFSFile*>(ctx);
            delete encfsFile;
        }
    }
}

/**
 * @brief Cleans up file resources before the file is closed.
 *
 * This is where actual resource cleanup occurs. If DeletePending is set,
 * the file/directory is deleted after the handle is closed.
 *
 * @param FileName Virtual path of the file.
 * @param DokanFileInfo Dokan file context.
 */
void DOKAN_CALLBACK EncFSCleanup(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    DbgPrint(L"Cleanup: '%s' (deletePending=%d)\n", FileName, DokanFileInfo->DeletePending);

    // Atomically clear and delete context to prevent double-delete
    ULONG64 ctx = InterlockedExchange64((LONG64*)&DokanFileInfo->Context, 0);
    if (ctx) {
        EncFS::EncFSFile* encfsFile = reinterpret_cast<EncFS::EncFSFile*>(ctx);
        delete encfsFile;
    }
    else {
        DbgPrint(L"  [WARN] Context already NULL for '%s'\n", FileName);
    }

    // Perform deferred deletion if requested
    if (DokanFileInfo->DeletePending) {
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);

        if (DokanFileInfo->IsDirectory) {
            DbgPrint(L"  [INFO] Deleting directory '%s'\n", FileName);
            if (!RemoveDirectoryW(filePath)) {
                ErrorPrint(L"Cleanup: RemoveDirectory '%s' FAILED (error=%lu)\n", FileName, GetLastError());
            }
            else {
                DbgPrint(L"Cleanup: Directory '%s' deleted OK\n", FileName);
            }
        }
        else {
            DbgPrint(L"  [INFO] Deleting file '%s'\n", FileName);
            if (DeleteFileW(filePath) == 0) {
                ErrorPrint(L"Cleanup: DeleteFile '%s' FAILED (error=%lu)\n", FileName, GetLastError());
            }
            else {
                DbgPrint(L"Cleanup: File '%s' deleted OK\n", FileName);
            }
        }
    }
}

/**
 * @brief Marks a file for deletion (pre-check only).
 *
 * The actual deletion is performed in Cleanup when DeletePending is TRUE.
 *
 * @param FileName Virtual path of the file.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];

    GetFilePath(filePath, FileName, false);
    DbgPrint(L"DeleteFile: '%s' (pending=%d)\n", FileName, DokanFileInfo->DeletePending);

    DWORD dwAttrib = GetFileAttributesW(filePath);

    // Cannot use DeleteFile on directories
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        DbgPrint(L"  [INFO] Cannot delete directory using DeleteFile\n");
        return STATUS_ACCESS_DENIED;
    }

    // This is just a pre-check; actual deletion happens in Cleanup
    return STATUS_SUCCESS;
}

/**
 * @brief Marks a directory for deletion (checks if empty).
 *
 * Verifies the directory is empty when DeletePending is TRUE.
 * Actual deletion occurs in Cleanup.
 *
 * @param FileName Virtual path of the directory.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    size_t fileLen;
    WCHAR filePath[DOKAN_MAX_PATH];

    GetFilePath(filePath, FileName, false);
    DbgPrint(L"DeleteDirectory: '%s' (pending=%d)\n", FileName, DokanFileInfo->DeletePending);

    // Pre-check always succeeds
    if (!DokanFileInfo->DeletePending) {
        return STATUS_SUCCESS;
    }

    // Verify directory is empty
    fileLen = wcslen(filePath);
    if (fileLen + 2 >= DOKAN_MAX_PATH) return STATUS_BUFFER_OVERFLOW;

    if (filePath[fileLen - 1] != L'\\') {
        filePath[fileLen++] = L'\\';
    }
    filePath[fileLen] = L'*';
    filePath[fileLen + 1] = L'\0';

    hFind = FindFirstFileW(filePath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        ErrorPrint(L"DeleteDirectory: FindFirstFile '%s' FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }
    // RAII guard for find handle
    auto find_closer = [](HANDLE h) { FindClose(h); };
    std::unique_ptr<void, decltype(find_closer)> find_guard(hFind, find_closer);

    // Check for any entries besides . and ..
    do {
        if (wcscmp(findData.cFileName, L"..") != 0 &&
            wcscmp(findData.cFileName, L".") != 0) {
            DbgPrint(L"  [INFO] Directory not empty: contains '%s'\n", findData.cFileName);
            return STATUS_DIRECTORY_NOT_EMPTY;
        }
    } while (FindNextFileW(hFind, &findData) != 0);

    DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
        ErrorPrint(L"DeleteDirectory: FindNextFile FAILED (error=%lu)\n", error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrint(L"DeleteDirectory: '%s' is empty, ready for deletion\n", FileName);
    return STATUS_SUCCESS;
}

/**
 * @brief Reads data from an encrypted file.
 *
 * Handles decryption for normal mode, encryption for reverse mode,
 * and plaintext reads for special streams (e.g., Dropbox attributes).
 *
 * @param FileName Virtual path of the file.
 * @param Buffer Output buffer for read data.
 * @param BufferLength Size of the output buffer.
 * @param ReadLength Output: actual bytes read.
 * @param Offset File offset to start reading from.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSReadFile(LPCWSTR FileName, LPVOID Buffer,
    DWORD BufferLength, LPDWORD ReadLength,
    LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo) {

    ULONG offset = (ULONG)Offset;
    EncFS::EncFSFile* encfsFile;
    std::unique_ptr<EncFS::EncFSFile> tempEncFSFile;

    // Open temporary handle if context is unavailable
    if (!DokanFileInfo->Context) {
        WCHAR filePath[DOKAN_MAX_PATH];
        try {
            GetFilePath(filePath, FileName, false);
        }
        catch (const std::exception& ex) {
            ErrorPrint(L"ReadFile: Path conversion failed for '%s': %S\n", FileName, ex.what());
            return STATUS_OBJECT_NAME_INVALID;
        }
        catch (...) {
            ErrorPrint(L"ReadFile: Unknown exception during path conversion for '%s'\n", FileName);
            return STATUS_OBJECT_NAME_INVALID;
        }
        
        DbgPrint(L"ReadFile: '%s' - Context NULL, opening temp handle\n", FileName);
        HANDLE handle = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            ErrorPrint(L"ReadFile: CreateFile '%s' FAILED (error=%lu)\n", FileName, error);
            return DokanNtStatusFromWin32(error);
        }
        tempEncFSFile = std::make_unique<EncFS::EncFSFile>(handle, true);
        encfsFile = tempEncFSFile.get();
    }
    else {
        encfsFile = ToEncFSFile(DokanFileInfo->Context);
    }

    DbgPrint(L"ReadFile: '%s' offset=%I64d len=%lu\n", FileName, Offset, BufferLength);

    int32_t readLen;
    bool plain = false;

    // Check for Dropbox attribute streams (bypass encryption)
    if (encfs.altStream) {
        const LPCWSTR suffix = L":com.dropbox.attrs:$DATA";
        size_t str_len = wcslen(FileName);
        size_t suffix_len = wcslen(suffix);
        plain = str_len >= suffix_len && 0 == wcscmp(FileName + str_len - suffix_len, suffix);
        if (plain) {
            DbgPrint(L"  [INFO] Dropbox attr stream, using plaintext read\n");
        }
    }

    if (!plain) {
        if (encfs.isReverse()) {
            // Reverse mode: encrypt plaintext for output
            WCHAR filePath[DOKAN_MAX_PATH];
            try {
                GetFilePath(filePath, FileName, false);
            }
            catch (...) {
                plain = true;
            }
            if (!plain) {
                size_t len = wcslen(filePath);
                // Config file is never encrypted
                if (len >= 12 && wcscmp(filePath + len - 12, L"\\.encfs6.xml") == 0) {
                    plain = true;
                    DbgPrint(L"  [INFO] Config file, using plaintext read\n");
                }
                else {
                    readLen = encfsFile->reverseRead(FileName, (char*)Buffer, offset, BufferLength);
                }
            }
        }
        else {
            // Normal mode: decrypt ciphertext for output
            readLen = encfsFile->read(FileName, (char*)Buffer, offset, BufferLength);
        }
    }

    // Plaintext read (no encryption/decryption)
    if (plain) {
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = Offset;
        if (!SetFilePointerEx(encfsFile->getHandle(), distanceToMove, NULL, FILE_BEGIN)) {
            DWORD error = GetLastError();
            ErrorPrint(L"ReadFile: Seek failed for '%s' (error=%lu, offset=%I64d)\n", FileName, error, Offset);
            return DokanNtStatusFromWin32(error);
        }
        else if (!ReadFile(encfsFile->getHandle(), Buffer, BufferLength, ReadLength, NULL)) {
            readLen = -1;
        }
        else {
            readLen = *ReadLength;
        }
    }

    if (readLen == -1) {
        DWORD error = GetLastError();
        ErrorPrint(L"ReadFile: '%s' FAILED (error=%lu, bufLen=%lu, offset=%I64d)\n",
            FileName, error, BufferLength, Offset);
        return DokanNtStatusFromWin32(error);
    }

    *ReadLength = readLen;
    DbgPrint(L"ReadFile: '%s' OK (requested=%lu, read=%lu, offset=%I64d)\n", 
        FileName, BufferLength, *ReadLength, Offset);

    return STATUS_SUCCESS;
}

/**
 * @brief Writes data to an encrypted file.
 *
 * Handles encryption and writing to the underlying storage.
 * Supports write-to-end-of-file and paging I/O restrictions.
 *
 * @param FileName Virtual path of the file.
 * @param Buffer Data to write.
 * @param NumberOfBytesToWrite Number of bytes to write.
 * @param NumberOfBytesWritten Output: actual bytes written.
 * @param Offset File offset to start writing at.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSWriteFile(LPCWSTR FileName, LPCVOID Buffer,
    DWORD NumberOfBytesToWrite, LPDWORD NumberOfBytesWritten,
    LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrint(L"WriteFile: '%s' offset=%I64d len=%lu (writeToEOF=%d, pagingIO=%d)\n", 
        FileName, Offset, NumberOfBytesToWrite, 
        DokanFileInfo->WriteToEndOfFile, DokanFileInfo->PagingIo);

    UINT64 fileSize = 0;
    EncFS::EncFSFile* encfsFile;
    std::unique_ptr<EncFS::EncFSFile> tempEncFSFile;

    // Open temporary handle if context is unavailable
    if (!DokanFileInfo->Context) {
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);
        DbgPrint(L"WriteFile: '%s' - Context NULL, opening temp handle\n", FileName);
        HANDLE handle = CreateFileW(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            ErrorPrint(L"WriteFile: CreateFile '%s' FAILED (error=%lu)\n", FileName, error);
            return DokanNtStatusFromWin32(error);
        }
        tempEncFSFile = std::make_unique<EncFS::EncFSFile>(handle, false);
        encfsFile = tempEncFSFile.get();
    }
    else {
        encfsFile = ToEncFSFile(DokanFileInfo->Context);
    }

    // Get logical (decrypted) file size
    LARGE_INTEGER li;
    if (GetFileSizeEx(encfsFile->getHandle(), &li)) {
        fileSize = li.QuadPart;
    }
    else {
        DWORD error = GetLastError();
        ErrorPrint(L"WriteFile: GetFileSizeEx '%s' FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }
    fileSize = encfs.toDecodedLength(fileSize);

    size_t off;
    if (DokanFileInfo->WriteToEndOfFile) {
        off = fileSize;
        DbgPrint(L"  [INFO] Write to EOF: offset adjusted to %I64u\n", (UINT64)off);
    }
    else {
        // Paging I/O cannot extend file beyond allocated size
        if (DokanFileInfo->PagingIo) {
            if ((UINT64)Offset >= fileSize) {
                DbgPrint(L"  [INFO] PagingIO offset beyond EOF, returning 0 bytes\n");
                *NumberOfBytesWritten = 0;
                return STATUS_SUCCESS;
            }
            if (((UINT64)Offset + NumberOfBytesToWrite) > fileSize) {
                UINT64 bytes = fileSize - Offset;
                DbgPrint(L"  [INFO] PagingIO truncated write: %lu -> %I64u\n", NumberOfBytesToWrite, bytes);
                NumberOfBytesToWrite = (DWORD)bytes;
            }
        }
        off = Offset;
    }

    // Check for Dropbox attribute streams (bypass encryption)
    bool plain = false;
    if (encfs.altStream) {
        const LPCWSTR suffix = L":com.dropbox.attrs:$DATA";
        size_t str_len = wcslen(FileName);
        size_t suffix_len = wcslen(suffix);
        plain = str_len >= suffix_len && 0 == wcscmp(FileName + str_len - suffix_len, suffix);
        if (plain) {
            DbgPrint(L"  [INFO] Dropbox attr stream, using plaintext write\n");
        }
    }

    int32_t writtenLen;
    if (!plain) {
        // Encrypted write
        writtenLen = encfsFile->write(FileName, fileSize, (char*)Buffer, off, NumberOfBytesToWrite);
        if (writtenLen == -1) {
            DWORD error = GetLastError();
            ErrorPrint(L"WriteFile: '%s' encrypt/write FAILED (error=%lu, len=%lu)\n",
                FileName, error, NumberOfBytesToWrite);
            return DokanNtStatusFromWin32(error);
        }
    }
    else {
        // Plaintext write for special streams
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = Offset;
        if (!SetFilePointerEx(encfsFile->getHandle(), distanceToMove, NULL, FILE_BEGIN)) {
            DWORD error = GetLastError();
            ErrorPrint(L"WriteFile: Seek failed for '%s' (error=%lu, offset=%I64d)\n", FileName, error, Offset);
            return DokanNtStatusFromWin32(error);
        }
        else if (!WriteFile(encfsFile->getHandle(), Buffer, NumberOfBytesToWrite, NumberOfBytesWritten, NULL)) {
            writtenLen = -1;
        }
        else {
            writtenLen = *NumberOfBytesWritten;
        }
    }
    *NumberOfBytesWritten = writtenLen;

    DbgPrint(L"WriteFile: '%s' OK (requested=%lu, written=%lu)\n", FileName, NumberOfBytesToWrite, writtenLen);
    return STATUS_SUCCESS;
}

/**
 * @brief Enumerates files in a directory.
 *
 * Decrypts filenames (normal mode) or encrypts them (reverse mode)
 * and adjusts file sizes to logical (decrypted) sizes.
 *
 * @param FileName Virtual path of the directory.
 * @param FillFindData Callback to add entries to the result set.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSFindFiles(LPCWSTR FileName,
    PFillFindData FillFindData,
    PDOKAN_FILE_INFO DokanFileInfo) {

    WCHAR filePath[DOKAN_MAX_PATH];
    size_t fileLen;
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    DWORD error;
    int count = 0;

    try {
        GetFilePath(filePath, FileName, false);
    }
    catch (const std::exception& ex) {
        ErrorPrint(L"FindFiles: Path conversion failed for '%s': %S\n", FileName, ex.what());
        return STATUS_OBJECT_NAME_INVALID;
    }
    catch (...) {
        ErrorPrint(L"FindFiles: Unknown exception during path conversion for '%s'\n", FileName);
        return STATUS_OBJECT_NAME_INVALID;
    }
    
    DbgPrint(L"FindFiles: '%s' -> '%s'\n", FileName, filePath);

    fileLen = wcslen(filePath);
    if (fileLen + 2 >= DOKAN_MAX_PATH) return STATUS_BUFFER_OVERFLOW;

    // Append wildcard for enumeration
    if (filePath[fileLen - 1] != L'\\') {
        filePath[fileLen++] = L'\\';
    }
    filePath[fileLen] = L'*';
    filePath[fileLen + 1] = L'\0';

    hFind = FindFirstFileW(filePath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        ErrorPrint(L"FindFiles: FindFirstFile '%s' FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }
    // RAII guard for find handle
    auto find_closer = [](HANDLE h) { FindClose(h); };
    std::unique_ptr<void, decltype(find_closer)> find_guard(hFind, find_closer);

    wstring wPath(FileName);
    wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
    string cPath;
    
    try {
        cPath = strConv.to_bytes(wPath);
        if (wPath.length() > 0 && cPath.empty()) {
            throw std::runtime_error("Conversion resulted in empty string");
        }
    }
    catch (const std::exception& ex) {
        ErrorPrint(L"FindFiles: UTF-8 conversion failed: %S\n", ex.what());
        return STATUS_INVALID_PARAMETER;
    }

    BOOLEAN rootFolder = (wcscmp(FileName, L"") == 0 || wcscmp(FileName, L"\\") == 0);
    do {
        // Skip . and .. for root directory
        if (!rootFolder || (wcscmp(findData.cFileName, L".") != 0 &&
            wcscmp(findData.cFileName, L"..") != 0)) {

            wstring wcFileName(findData.cFileName);
            string ccFileName;
            string cPlainFileName;
            
            try {
                ccFileName = strConv.to_bytes(wcFileName);
                if (wcFileName.length() > 0 && ccFileName.empty()) {
                    DbgPrint(L"  [WARN] Skipping file (conversion failed): '%s'\n", findData.cFileName);
                    continue;
                }
                
                if (encfs.isReverse()) {
                    // Reverse mode: encrypt plaintext names for display
                    if (wcscmp(findData.cFileName, L".encfs6.xml") != 0) {
                        encfs.encodeFileName(ccFileName, cPath, cPlainFileName);
                    }
                    else {
                        cPlainFileName = ccFileName;
                    }
                }
                else {
                    // Normal mode: decrypt encrypted names for display
                    encfs.decodeFileName(ccFileName, cPath, cPlainFileName);
                }
            }
            catch (const EncFS::EncFSInvalidBlockException&) {
                // Skip files with invalid encrypted names
                DbgPrint(L"  [WARN] Skipping file (invalid encryption): '%s'\n", findData.cFileName);
                continue;
            }
            catch (const std::exception& ex) {
                DbgPrint(L"  [WARN] Skipping file (exception: %S): '%s'\n", ex.what(), findData.cFileName);
                continue;
            }
            
            wstring wPlainFileName;
            try {
                wPlainFileName = strConv.from_bytes(cPlainFileName);
                if (cPlainFileName.length() > 0 && wPlainFileName.empty()) {
                    DbgPrint(L"  [WARN] Skipping file (back-conversion failed)\n");
                    continue;
                }
            }
            catch (const std::exception& ex) {
                DbgPrint(L"  [WARN] Skipping file (exception: %S)\n", ex.what());
                continue;
            }
            
            wcscpy_s(findData.cFileName, wPlainFileName.c_str());
            findData.cAlternateFileName[0] = 0; // No 8.3 short name

            // Convert physical size to logical size
            int64_t size = (findData.nFileSizeHigh * ((int64_t)MAXDWORD + 1)) + findData.nFileSizeLow;
            size = encfs.isReverse() ? encfs.toEncodedLength(size) : encfs.toDecodedLength(size);
            findData.nFileSizeLow = size & MAXDWORD;
            findData.nFileSizeHigh = (size >> 32) & MAXDWORD;

            FillFindData(&findData, DokanFileInfo);
        }
        count++;
    } while (FindNextFileW(hFind, &findData) != 0);

    error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
        ErrorPrint(L"FindFiles: FindNextFile FAILED (error=%lu)\n", error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrint(L"FindFiles: '%s' OK (entries=%d)\n", FileName, count);
    return STATUS_SUCCESS;
}

/**
 * @brief Recursively updates IVs for all files in a moved directory.
 *
 * When using chained name IV or external IV chaining, moving a directory
 * requires updating the IVs of all contained files and subdirectories.
 *
 * @param newFilePath Physical path of the moved directory.
 * @param cOldPlainDirPath Old plaintext directory path (for IV calculation).
 * @param cNewPlainDirPath New plaintext directory path (for IV calculation).
 * @return NTSTATUS indicating success or failure.
 */
static NTSTATUS changeIVRecursive(LPCWSTR newFilePath, const string cOldPlainDirPath, const string cNewPlainDirPath) {
    WCHAR findPath[DOKAN_MAX_PATH];
    WCHAR oldPath[DOKAN_MAX_PATH];
    WCHAR newPath[DOKAN_MAX_PATH];

    if (wcslen(newFilePath) + 5 > DOKAN_MAX_PATH) return STATUS_BUFFER_OVERFLOW;

    wcscpy_s(findPath, DOKAN_MAX_PATH, newFilePath);
    wcscat_s(findPath, DOKAN_MAX_PATH, L"\\*.*");

    wcscpy_s(oldPath, DOKAN_MAX_PATH, newFilePath);
    wcscat_s(oldPath, DOKAN_MAX_PATH, L"\\");

    size_t oldPathLen = wcslen(oldPath);
    wcscpy_s(newPath, DOKAN_MAX_PATH, oldPath);

    DbgPrint(L"ChangeIVRecursive: '%s'\n", newFilePath);

    WIN32_FIND_DATAW find;
    HANDLE findHandle = FindFirstFileW(findPath, &find);
    if (findHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        ErrorPrint(L"ChangeIVRecursive: FindFirstFile FAILED (error=%lu)\n", error);
        return DokanNtStatusFromWin32(error);
    }
    // RAII guard for find handle
    auto find_closer = [](HANDLE h) { FindClose(h); };
    std::unique_ptr<void, decltype(find_closer)> find_guard(findHandle, find_closer);

    wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;

    do {
        // Skip . and .. entries
        if (find.cFileName[0] == L'.') continue;

        wstring wOldName(find.cFileName);
        string cOldName = strConv.to_bytes(wOldName);
        string plainName;
        try {
            encfs.decodeFileName(cOldName, cOldPlainDirPath, plainName);
        }
        catch (const EncFS::EncFSInvalidBlockException&) {
            DbgPrint(L"  [WARN] Skipping file (invalid name): '%s'\n", find.cFileName);
            continue;
        }

        string cNewName;
        encfs.encodeFileName(plainName, cNewPlainDirPath, cNewName);
        wstring wNewName = strConv.from_bytes(cNewName);

        if (oldPathLen + wcslen(find.cFileName) >= DOKAN_MAX_PATH ||
            oldPathLen + wNewName.length() >= DOKAN_MAX_PATH) {
            return STATUS_BUFFER_OVERFLOW;
        }

        wcscpy_s(&oldPath[oldPathLen], DOKAN_MAX_PATH - oldPathLen, find.cFileName);
        wcscpy_s(&newPath[oldPathLen], DOKAN_MAX_PATH - oldPathLen, wNewName.c_str());

        // Rename file if using chained name IV
        if (encfs.isChainedNameIV()) {
            DbgPrint(L"  [INFO] Renaming (chained IV): '%s' -> '%s'\n", oldPath, newPath);
            if (!MoveFileW(oldPath, newPath)) {
                DWORD error = GetLastError();
                ErrorPrint(L"ChangeIVRecursive: MoveFile FAILED (error=%lu)\n", error);
                return DokanNtStatusFromWin32(error);
            }
        }

        string cPlainOldPath = cOldPlainDirPath + "\\" + plainName;
        wstring wPlainOldPath = strConv.from_bytes(cPlainOldPath);
        string cPlainNewPath = cNewPlainDirPath + "\\" + plainName;
        wstring wPlainNewPath = strConv.from_bytes(cPlainNewPath);

        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectories
            NTSTATUS status = changeIVRecursive(newPath, cPlainOldPath, cPlainNewPath);
            if (status != STATUS_SUCCESS) {
                return status;
            }
        }
        else {
            // Update file IV if using external IV chaining
            if (encfs.isExternalIVChaining()) {
                DbgPrint(L"  [INFO] Updating file IV: '%s'\n", newPath);
                HANDLE handle2 = CreateFileW(newPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
                if (handle2 == INVALID_HANDLE_VALUE) {
                    DWORD error = GetLastError();
                    ErrorPrint(L"ChangeIVRecursive: CreateFile FAILED (error=%lu)\n", error);
                    return DokanNtStatusFromWin32(error);
                }

                EncFS::EncFSFile encfsFile2(handle2, false);
                if (!encfsFile2.changeFileIV(wPlainOldPath.c_str(), wPlainNewPath.c_str())) {
                    DWORD error = GetLastError();
                    ErrorPrint(L"ChangeIVRecursive: changeFileIV FAILED (error=%lu)\n", error);
                    return DokanNtStatusFromWin32(error);
                }
            }
        }
    } while (FindNextFileW(findHandle, &find));

    DbgPrint(L"ChangeIVRecursive: '%s' OK\n", newFilePath);
    return STATUS_SUCCESS;
}

/**
 * @brief Moves or renames a file or directory.
 *
 * Handles IV updates for chained name IV and external IV chaining modes.
 * Supports case-insensitive collision detection.
 *
 * @param FileName Current virtual path.
 * @param NewFileName New virtual path.
 * @param ReplaceIfExisting Whether to replace existing target.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSMoveFile(LPCWSTR FileName, LPCWSTR NewFileName, BOOL ReplaceIfExisting,
    PDOKAN_FILE_INFO DokanFileInfo) {

    BOOL result;
    size_t newFilePathLen;

    WCHAR filePath[DOKAN_MAX_PATH];
    WCHAR newFilePath[DOKAN_MAX_PATH];
    GetFilePath(filePath, FileName, false);
    GetFilePath(newFilePath, NewFileName, true);

    DbgPrint(L"MoveFile: '%s' -> '%s' (replace=%d, isDir=%d)\n", 
        FileName, NewFileName, ReplaceIfExisting, DokanFileInfo->IsDirectory);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"  [WARN] Context is NULL\n");
        return STATUS_INVALID_HANDLE;
    }

    // Case-insensitive collision detection
    if (g_efo.CaseInsensitive) {
        std::wstring wNew(NewFileName);
        size_t sep = wNew.find_last_of(L'\\');
        if (sep != std::wstring::npos && (sep + 1) < wNew.size()) {
            std::wstring wParentPlain = wNew.substr(0, sep);
            std::wstring wLeafPlain = wNew.substr(sep + 1);
 
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> strConv;
            std::string cParentPlain = strConv.to_bytes(wParentPlain);
            std::string cParentEnc;
            encfs.encodeFilePath(cParentPlain, cParentEnc);
            
            // Remove trailing separator
            if (!cParentEnc.empty()) {
                char last = cParentEnc.back();
                if (last == '\\' || last == '/') {
                    cParentEnc.pop_back();
                }
            }
            
            std::string cSearch = cParentEnc + EncFS::g_pathSeparator + "*";
            WCHAR searchPath[DOKAN_MAX_PATH];
            ToWFilePath(strConv, cSearch, searchPath);

            WCHAR parentPhys[DOKAN_MAX_PATH];
            ToWFilePath(strConv, cParentEnc, parentPhys);

            WIN32_FIND_DATAW find;
            ZeroMemory(&find, sizeof(find));
            HANDLE hFind = FindFirstFileW(searchPath, &find);
            if (hFind != INVALID_HANDLE_VALUE) {
                auto closer = [](HANDLE h) { FindClose(h); };
                std::unique_ptr<void, decltype(closer)> guard(hFind, closer);

                // Check for case-insensitive collisions
                do {
                    if (wcscmp(find.cFileName, L".") == 0 || wcscmp(find.cFileName, L"..") == 0) continue;
                    std::wstring wEncName(find.cFileName);
                    std::string cEncName = strConv.to_bytes(wEncName);
                    std::string cPlainChild;
                    try {
                        encfs.decodeFileName(cEncName, cParentEnc, cPlainChild);
                    } catch (const EncFS::EncFSInvalidBlockException&) {
                        continue;
                    }
                    std::wstring wPlainChild = strConv.from_bytes(cPlainChild);

                    int cmp = CompareStringOrdinal(
                        wPlainChild.c_str(), static_cast<int>(wPlainChild.length()),
                        wLeafPlain.c_str(), static_cast<int>(wLeafPlain.length()),
                        TRUE);
                    if (cmp == CSTR_EQUAL) {
                        // Build full physical path
                        WCHAR candidate[DOKAN_MAX_PATH];
                        wcsncpy_s(candidate, parentPhys, _TRUNCATE);
                        size_t len = wcslen(candidate);
                        if (len + 1 < DOKAN_MAX_PATH) {
                            if (candidate[len - 1] != L'\\') {
                                candidate[len++] = L'\\';
                                candidate[len] = L'\0';
                            }
                            wcsncat_s(candidate, find.cFileName, _TRUNCATE);
                        }

                        // Collision with different file
                        if (wcscmp(candidate, filePath) != 0) {
                            if (!ReplaceIfExisting) {
                                DbgPrint(L"  [INFO] Case-insensitive collision detected\n");
                                return STATUS_OBJECT_NAME_COLLISION;
                            }
                            break;
                        }
                        // Same file (case change only) - allowed
                        break;
                    }
                } while (FindNextFileW(hFind, &find));
            }
        }
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    // Handle IV updates for path-dependent encryption
    if (encfs.isChainedNameIV() || encfs.isExternalIVChaining()) {
        if (DokanFileInfo->IsDirectory) {
            // Directory move requires recursive IV update
            lock_guard<decltype(dirMoveLock)> dlock(dirMoveLock);

            // Close handle before moving
            delete encfsFile;
            DokanFileInfo->Context = 0;

            DbgPrint(L"  [INFO] Moving directory (with IV update)\n");
            if (!MoveFileW(filePath, newFilePath)) {
                DWORD error = GetLastError();
                ErrorPrint(L"MoveFile: MoveFileW FAILED (error=%lu)\n", error);
                return DokanNtStatusFromWin32(error);
            }

            // Recursively update IVs
            wstring wOldPlainDirPath(FileName);
            wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
            string cOldPlainDirPath = strConv.to_bytes(wOldPlainDirPath);
            wstring wNewPlainDirPath(NewFileName);
            string cNewPlainDirPath = strConv.to_bytes(wNewPlainDirPath);

            return changeIVRecursive(newFilePath, cOldPlainDirPath, cNewPlainDirPath);
        }
        else {
            // Single file IV update
            if (encfs.isExternalIVChaining()) {
                DbgPrint(L"  [INFO] Updating file IV for move\n");
                if (!encfsFile->changeFileIV(FileName, NewFileName)) {
                    DWORD error = GetLastError();
                    ErrorPrint(L"MoveFile: changeFileIV FAILED (error=%lu)\n", error);
                    return DokanNtStatusFromWin32(error);
                }
            }
        }
    }

    // Perform the actual rename using SetFileInformationByHandle
    newFilePathLen = wcslen(newFilePath);
    DWORD bufferSize = (DWORD)(sizeof(FILE_RENAME_INFO) + newFilePathLen * sizeof(newFilePath[0]));

    std::vector<BYTE> renameInfoBuffer(bufferSize);
    PFILE_RENAME_INFO renameInfo = (PFILE_RENAME_INFO)renameInfoBuffer.data();

    renameInfo->ReplaceIfExists = ReplaceIfExisting ? TRUE : FALSE;
    renameInfo->RootDirectory = NULL;
    renameInfo->FileNameLength = (DWORD)newFilePathLen * sizeof(newFilePath[0]);
    wcscpy_s(renameInfo->FileName, newFilePathLen + 1, newFilePath);

    result = SetFileInformationByHandle(encfsFile->getHandle(), FileRenameInfo, renameInfo, bufferSize);

    if (result) {
        // Invalidate filename cache for IV recalculation
        encfsFile->invalidateFileNameCache();
        DbgPrint(L"MoveFile: '%s' -> '%s' OK\n", FileName, NewFileName);
        return STATUS_SUCCESS;
    }
    else {
        DWORD error = GetLastError();
        ErrorPrint(L"MoveFile: SetFileInformationByHandle FAILED (error=%lu)\n", error);
        return DokanNtStatusFromWin32(error);
    }
}

#pragma warning(push)
#pragma warning(disable : 4201)
typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    } DUMMYUNIONNAME;
    ULONG_PTR Information;
} IO_STATUS_BLOCK, * PIO_STATUS_BLOCK;
#pragma warning(pop)

/**
 * @brief Enumerates alternate data streams of a file.
 *
 * @param FileName Virtual path of the file.
 * @param FillFindStreamData Callback to add stream entries.
 * @param FindStreamContext Context for the callback.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSFindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
    PVOID FindStreamContext, PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE hFind;
    WIN32_FIND_STREAM_DATA findData;
    DWORD error;
    int count = 0;

    GetFilePath(filePath, FileName, false);
    DbgPrint(L"FindStreams: '%s'\n", FileName);

    hFind = FindFirstStreamW(filePath, FindStreamInfoStandard, &findData, 0);
    if (hFind == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        ErrorPrint(L"FindStreams: FindFirstStreamW '%s' FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }
    // RAII guard for find handle
    auto find_closer = [](HANDLE h) { FindClose(h); };
    std::unique_ptr<void, decltype(find_closer)> find_guard(hFind, find_closer);

    BOOL bufferFull = FillFindStreamData(&findData, FindStreamContext);
    if (bufferFull) {
        count++;
        while (FindNextStreamW(hFind, &findData) != 0) {
            bufferFull = FillFindStreamData(&findData, FindStreamContext);
            if (!bufferFull) break;
            count++;
        }
    }

    error = GetLastError();

    if (!bufferFull) {
        DbgPrint(L"FindStreams: '%s' buffer full (entries=%d)\n", FileName, count);
        return STATUS_SUCCESS;
    }

    if (error != ERROR_HANDLE_EOF) {
        ErrorPrint(L"FindStreams: FindNextStreamW FAILED (error=%lu)\n", error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrint(L"FindStreams: '%s' OK (entries=%d)\n", FileName, count);
    return STATUS_SUCCESS;
}
