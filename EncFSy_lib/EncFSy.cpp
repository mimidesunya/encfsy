/**
 * @file EncFSy.cpp
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
#include <codecvt> // Note: deprecated in C++17, keeping for compatibility

#include "EncFSFile.h"
#include "EncFSUtils.hpp"

#include "EncFSy_globals.h"
#include "EncFSy_logging.h"
#include "EncFSy_path.h"
#include "EncFSy_handlers.h"
#include "EncFSy_mount.h"

 // Suppress warnings for wstring_convert if compiling with C++17 or later
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

using namespace std;

// Define globals once here (declared extern in EncFSy_globals.h)
EncFS::EncFSVolume encfs;
EncFSOptions g_efo;
std::mutex dirMoveLock;

/**
 * @brief Safely converts a ULONG64 context from Dokan to an EncFSFile pointer.
 * @param context The ULONG64 value from PDOKAN_FILE_INFO->Context.
 * @return A pointer to an EncFS::EncFSFile object.
 */
static inline EncFS::EncFSFile* ToEncFSFile(ULONG64 context) {
    return reinterpret_cast<EncFS::EncFSFile*>(context);
}

/**
 * @brief Helper function for debug printing user account information.
 * @param DokanFileInfo Dokan file context structure.
 * @details Only outputs when debug mode is enabled. Prints the account name and domain
 * of the user making the filesystem request.
 */
static void PrintUserName(PDOKAN_FILE_INFO DokanFileInfo) {
    if (!g_efo.g_DebugMode)
        return;

    HANDLE handle = DokanOpenRequestorToken(DokanFileInfo);
    if (handle == INVALID_HANDLE_VALUE) {
        DbgPrint(L"  DokanOpenRequestorToken failed\n");
        return;
    }
    // RAII for handle
    auto close_handle = [](HANDLE h) { CloseHandle(h); };
    std::unique_ptr<void, decltype(close_handle)> handle_guard(handle, close_handle);

    UCHAR buffer[1024];
    DWORD returnLength;
    if (!GetTokenInformation(handle, TokenUser, buffer, sizeof(buffer), &returnLength)) {
        ErrorPrint(L"  GetTokenInformation failed: %d\n", GetLastError());
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
        ErrorPrint(L"  LookupAccountSid failed: %d\n", GetLastError());
        return;
    }

    DbgPrint(L"  AccountName: %s, DomainName: %s\n", accountName, domainName);
}

/**
 * @brief Macro for debug printing individual flag values in bitmasks.
 */
#define EncFSCheckFlag(val, flag)                                             \
  if (val & flag) {                                                            \
    DbgPrint(L"\t" L#flag L"\n");                                              \
  }

 // ============================================================================
 // Dokan Callback Implementations
 // ============================================================================

 /**
  * @brief Dokan callback for creating or opening a file or directory.
  * @details This is one of the most complex callbacks. It handles file/directory
  * creation, opening, access checks, and impersonation. It maps Windows
  * CreateFile flags to underlying file system operations.
  */
static NTSTATUS DOKAN_CALLBACK
EncFSCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
    ACCESS_MASK DesiredAccess, ULONG FileAttributes,
    ULONG ShareAccess, ULONG CreateDisposition,
    ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo) {

    // Use stack allocation for path to avoid heap allocation overhead and leaks.
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

    // Map kernel-mode flags to user-mode flags for CreateFileW.
    DokanMapKernelToUserCreateFileFlags(
        DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
        &genericDesiredAccess, &fileAttributesAndFlags, &creationDisposition);

    // Get the full path in the underlying filesystem.
    GetFilePath(filePath, FileName,
        creationDisposition == CREATE_NEW || creationDisposition == CREATE_ALWAYS);

    DbgPrint(L"CreateFile : '%s' ; '%s'\n", FileName, filePath);
    PrintUserName(DokanFileInfo);

    // Debug prints for access flags.
    DbgPrint(L"\tShareMode = 0x%x\n", ShareAccess);
    EncFSCheckFlag(ShareAccess, FILE_SHARE_READ);
    EncFSCheckFlag(ShareAccess, FILE_SHARE_WRITE);
    EncFSCheckFlag(ShareAccess, FILE_SHARE_DELETE);
    DbgPrint(L"\tDesiredAccess = 0x%x\n", DesiredAccess);
    // ... (Access flags checking retained for brevity, same as original) ...

    fileAttr = GetFileAttributesW(filePath);

    // Check if the path already exists and if it's a directory.
    if (fileAttr != INVALID_FILE_ATTRIBUTES) {
        if (fileAttr & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(CreateOptions & FILE_NON_DIRECTORY_FILE)) {
                DokanFileInfo->IsDirectory = TRUE;
                ShareAccess |= FILE_SHARE_READ; // Allow reading directory contents.
            }
            else {
                DbgPrint(L"\tCannot open a dir as a file\n");
                return STATUS_FILE_IS_A_DIRECTORY;
            }
        }
    }

    // Add POSIX semantics for case-sensitive filesystems if configured.
    if (!g_efo.CaseInsensitive) {
        fileAttributesAndFlags |= FILE_FLAG_POSIX_SEMANTICS;
    }

    // Impersonate the caller if the option is enabled.
    if (g_efo.g_ImpersonateCallerUser) {
        userTokenHandle = DokanOpenRequestorToken(DokanFileInfo);
        if (userTokenHandle == INVALID_HANDLE_VALUE) {
            DbgPrint(L"  DokanOpenRequestorToken failed\n");
        }
    }

    // RAII helper to automatically revert impersonation and close the token handle.
    struct AutoRevert {
        HANDLE token;
        bool active;
        AutoRevert(HANDLE t) : token(t), active(t != INVALID_HANDLE_VALUE) {}
        ~AutoRevert() {
            if (active) {
                RevertToSelf();
                CloseHandle(token);
            }
        }
    } reverter(userTokenHandle);

    if (DokanFileInfo->IsDirectory) {
        // Handle directory creation/opening.
        if (creationDisposition == CREATE_NEW || creationDisposition == OPEN_ALWAYS) {
            if (reverter.active) ImpersonateLoggedOnUser(reverter.token);

            if (!CreateDirectory(filePath, &securityAttrib)) {
                error = GetLastError();
                if (error != ERROR_ALREADY_EXISTS || creationDisposition == CREATE_NEW) {
                    ErrorPrint(L"CreateDirectory error code = %d\n\n", error);
                    if (reverter.active) RevertToSelf();
                    return DokanNtStatusFromWin32(error);
                }
            }
            if (reverter.active) RevertToSelf(); // Revert before next operation.
        }

        if (status == STATUS_SUCCESS) {
            // Trying to open a file as a directory.
            if (fileAttr != INVALID_FILE_ATTRIBUTES &&
                !(fileAttr & FILE_ATTRIBUTE_DIRECTORY) &&
                (CreateOptions & FILE_DIRECTORY_FILE)) {
                return STATUS_NOT_A_DIRECTORY;
            }

            if (reverter.active) ImpersonateLoggedOnUser(reverter.token);

            // Open a handle to the directory.
            handle = CreateFileW(filePath, genericDesiredAccess, ShareAccess,
                &securityAttrib, OPEN_EXISTING,
                fileAttributesAndFlags | FILE_FLAG_BACKUP_SEMANTICS, NULL);

            if (reverter.active) RevertToSelf();

            if (handle == INVALID_HANDLE_VALUE) {
                error = GetLastError();
                ErrorPrint(L"CreateFile error code = %d\n\n", error);
                status = DokanNtStatusFromWin32(error);
            }
            else {
                // Store the handle in Dokan's context for future operations.
                DokanFileInfo->Context = (ULONG64)new EncFS::EncFSFile(handle, false);
                if (creationDisposition == OPEN_ALWAYS && fileAttr != INVALID_FILE_ATTRIBUTES) {
                    status = STATUS_OBJECT_NAME_COLLISION;
                }
            }
        }
    }
    else {
        // Handle file creation/opening.
        if (fileAttr != INVALID_FILE_ATTRIBUTES &&
            ((!(fileAttributesAndFlags & FILE_ATTRIBUTE_HIDDEN) && (fileAttr & FILE_ATTRIBUTE_HIDDEN)) ||
                (!(fileAttributesAndFlags & FILE_ATTRIBUTE_SYSTEM) && (fileAttr & FILE_ATTRIBUTE_SYSTEM))) &&
            (creationDisposition == TRUNCATE_EXISTING || creationDisposition == CREATE_ALWAYS)) {
            return STATUS_ACCESS_DENIED;
        }

        if ((fileAttr != INVALID_FILE_ATTRIBUTES && (fileAttr & FILE_ATTRIBUTE_READONLY) ||
            (fileAttributesAndFlags & FILE_ATTRIBUTE_READONLY)) &&
            (fileAttributesAndFlags & FILE_FLAG_DELETE_ON_CLOSE)) {
            return STATUS_CANNOT_DELETE;
        }

        if (creationDisposition == TRUNCATE_EXISTING)
            genericDesiredAccess |= GENERIC_WRITE;

        if (reverter.active) ImpersonateLoggedOnUser(reverter.token);

        // Encryption requires both read and write access to the underlying file.
        if (genericDesiredAccess & GENERIC_WRITE || genericDesiredAccess & FILE_WRITE_DATA || genericDesiredAccess & FILE_APPEND_DATA) {
            genericDesiredAccess |= FILE_READ_DATA | FILE_WRITE_DATA;
            if (ShareAccess & FILE_SHARE_WRITE) ShareAccess |= FILE_SHARE_READ;
        }
        if (genericDesiredAccess & DELETE) {
            genericDesiredAccess |= FILE_READ_DATA | FILE_WRITE_DATA;
            if (ShareAccess & FILE_SHARE_DELETE) ShareAccess |= FILE_SHARE_WRITE | FILE_SHARE_READ;
        }

        // Normalize file attributes.
        if (fileAttributesAndFlags & FILE_ATTRIBUTE_NORMAL) {
            fileAttributesAndFlags = FILE_ATTRIBUTE_NORMAL;
        }
        // Encrypted files cannot be unbuffered as their size doesn't align with sector boundaries.
        if (fileAttributesAndFlags & FILE_FLAG_NO_BUFFERING) {
            fileAttributesAndFlags ^= FILE_FLAG_NO_BUFFERING;
        }

        handle = CreateFileW(filePath, genericDesiredAccess, ShareAccess,
            &securityAttrib, creationDisposition, fileAttributesAndFlags, NULL);

        if (reverter.active) RevertToSelf();

        if (handle == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            ErrorPrint(L"CreateFile2 error code = %d\n", error);
            status = DokanNtStatusFromWin32(error);
        }
        else {
            if (fileAttr != INVALID_FILE_ATTRIBUTES && creationDisposition == TRUNCATE_EXISTING) {
                SetFileAttributesW(filePath, fileAttributesAndFlags | fileAttr);
            }

            // Store the handle and encryption state in Dokan's context.
            DokanFileInfo->Context = (ULONG64)new EncFS::EncFSFile(handle, true);

            if (creationDisposition == OPEN_ALWAYS || creationDisposition == CREATE_ALWAYS) {
                error = GetLastError();
                if (error == ERROR_ALREADY_EXISTS) {
                    DbgPrint(L"\tOpen an already existing file\n");
                    status = STATUS_OBJECT_NAME_COLLISION;
                }
            }
        }
    }

    DbgPrint(L"CreateFileEnd %d err=%d\n", status, GetLastError());
    return status;
}

/**
 * @brief Dokan callback for when the last handle to a file is closed.
 * @details This is called after Cleanup. It's a final notification, but most
 * resource cleanup should happen in Cleanup.
 */
static void DOKAN_CALLBACK EncFSCloseFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    lock_guard<decltype(dirMoveLock)> dlock(dirMoveLock);

    if (DokanFileInfo->Context) {
        DbgPrint(L"CloseFile: %s\n", FileName);
        DbgPrint(L"\terror : not cleanuped file\n\n");
        EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

        // Per Dokan guidelines, Context should be cleaned in Cleanup.
        // If it still exists here, it's a potential leak. We delete it to be safe.
        if (encfsFile) {
            delete encfsFile;
            DokanFileInfo->Context = 0;
        }
    }
    else {
        DbgPrint(L"Close: %s\n", FileName);
    }
}

/**
 * @brief Dokan callback for cleaning up file resources.
 * @details This is called when an application closes its handle to a file.
 * This is the primary place to release file-specific resources, such as
 * the file handle stored in the context. It also handles delete-on-close.
 */
static void DOKAN_CALLBACK EncFSCleanup(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    lock_guard<decltype(dirMoveLock)> dlock(dirMoveLock);

    // Free the context object, which in turn closes the underlying file handle.
    if (DokanFileInfo->Context) {
        DbgPrint(L"Cleanup: %s\n", FileName);
        EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);
        if (encfsFile) {
            delete encfsFile;
            DokanFileInfo->Context = 0;
        }
    }
    else {
        DbgPrint(L"Cleanup: %s\n\tinvalid handle\n", FileName);
    }

    // If the file was opened with FILE_FLAG_DELETE_ON_CLOSE, delete it now.
    if (DokanFileInfo->DeleteOnClose) {
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);
        DbgPrint(L"\tDeleteOnClose\n");
        if (DokanFileInfo->IsDirectory) {
            DbgPrint(L"  DeleteDirectory ");
            if (!RemoveDirectoryW(filePath)) {
                ErrorPrint(L"DeleteDirectory error code = %d\n", GetLastError());
            }
            else {
                DbgPrint(L"success\n");
            }
        }
        else {
            DbgPrint(L"  DeleteFile ");
            if (DeleteFileW(filePath) == 0) {
                ErrorPrint(L"DeleteFile error code = %d\n", GetLastError());
            }
            else {
                DbgPrint(L"success\n");
            }
        }
    }
}

/**
 * @brief Dokan callback for reading from a file.
 * @details Reads encrypted data from the underlying file and decrypts it into the buffer.
 * If the context is missing, it temporarily opens a handle for the read operation.
 */
static NTSTATUS DOKAN_CALLBACK EncFSReadFile(LPCWSTR FileName, LPVOID Buffer,
    DWORD BufferLength, LPDWORD ReadLength,
    LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo) {

    ULONG offset = (ULONG)Offset;
    EncFS::EncFSFile* encfsFile;
    // Use unique_ptr for RAII-based management of a temporarily opened file.
    std::unique_ptr<EncFS::EncFSFile> tempEncFSFile;

    if (!DokanFileInfo->Context) {
        // This can happen in some cases. Open a temporary handle for this operation.
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);
        DbgPrint(L"\tinvalid handle, cleanuped?\n");
        HANDLE handle = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            ErrorPrint(L"\tCreateFile error : %d\n", error);
            return DokanNtStatusFromWin32(error);
        }
        tempEncFSFile = std::make_unique<EncFS::EncFSFile>(handle, true);
        encfsFile = tempEncFSFile.get();
    }
    else {
        encfsFile = ToEncFSFile(DokanFileInfo->Context);
    }

    DbgPrint(L"ReadFile : %s, handle = %ld\n", FileName, encfsFile->getHandle());

    int32_t readLen;
    bool plain = false; // Flag to bypass decryption for certain alternate streams.
    if (encfs.altStream) {
        // Exclude Dropbox attribute streams from encryption/decryption.
        const LPCWSTR suffix = L":com.dropbox.attrs:$DATA";
        size_t str_len = wcslen(FileName);
        size_t suffix_len = wcslen(suffix);
        plain = str_len >= suffix_len && 0 == wcscmp(FileName + str_len - suffix_len, suffix);
    }

    if (!plain) {
        if (encfs.isReverse()) {
            // In reverse mode, the config file is not encrypted.
            WCHAR filePath[DOKAN_MAX_PATH];
            GetFilePath(filePath, FileName, false);
            size_t len = wcslen(filePath);
            if (len >= 12 && wcscmp(filePath + len - 12, L"\\.encfs6.xml") == 0) {
                plain = true;
            }
            else {
                // In reverse mode, "reading" means encrypting the underlying plaintext.
                readLen = encfsFile->reverseRead(FileName, (char*)Buffer, offset, BufferLength);
            }
        }
        else {
            // Normal mode: decrypt data from the underlying file.
            readLen = encfsFile->read(FileName, (char*)Buffer, offset, BufferLength);
        }
    }

    if (plain) {
        // For plaintext reads, just read directly from the handle.
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = Offset;
        if (!SetFilePointerEx(encfsFile->getHandle(), distanceToMove, NULL, FILE_BEGIN)) {
            DWORD error = GetLastError();
            DbgPrint(L"\tseek error, offset = %d\n\n", offset);
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
        ErrorPrint(L"\tRead error = %u, buffer length = %d, offset = %d\n\n",
            error, BufferLength, offset);
        return DokanNtStatusFromWin32(error);
    }

    *ReadLength = readLen;
    DbgPrint(L"\tByte to read: %d, Byte read %d, offset %d\n\n", BufferLength, *ReadLength, offset);

    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback for writing to a file.
 * @details Encrypts data from the buffer and writes it to the underlying file.
 * Handles write-to-end-of-file and paged I/O scenarios.
 */
static NTSTATUS DOKAN_CALLBACK EncFSWriteFile(LPCWSTR FileName, LPCVOID Buffer,
    DWORD NumberOfBytesToWrite, LPDWORD NumberOfBytesWritten,
    LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrint(L"WriteFile : %s, offset %I64d, length %d\n", FileName, Offset, NumberOfBytesToWrite);

    UINT64 fileSize = 0;
    EncFS::EncFSFile* encfsFile;
    std::unique_ptr<EncFS::EncFSFile> tempEncFSFile;

    if (!DokanFileInfo->Context) {
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);
        DbgPrint(L"\tinvalid handle, cleanuped?\n");
        HANDLE handle = CreateFileW(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            ErrorPrint(L"\tCreateFile error : %d\n", error);
            return DokanNtStatusFromWin32(error);
        }
        tempEncFSFile = std::make_unique<EncFS::EncFSFile>(handle, false);
        encfsFile = tempEncFSFile.get();
    }
    else {
        encfsFile = ToEncFSFile(DokanFileInfo->Context);
    }

    // Get the logical (decrypted) file size to handle appends correctly.
    LARGE_INTEGER li;
    if (GetFileSizeEx(encfsFile->getHandle(), &li)) {
        fileSize = li.QuadPart;
    }
    else {
        DWORD error = GetLastError();
        ErrorPrint(L"GetFileSize error code = %d\n", error);
        return DokanNtStatusFromWin32(error);
    }
    fileSize = encfs.toDecodedLength(fileSize);

    size_t off;
    if (DokanFileInfo->WriteToEndOfFile) {
        off = fileSize;
    }
    else {
        // For Paging I/O, writes cannot extend beyond the allocated file size.
        if (DokanFileInfo->PagingIo) {
            if ((UINT64)Offset >= fileSize) {
                *NumberOfBytesWritten = 0;
                return STATUS_SUCCESS;
            }
            if (((UINT64)Offset + NumberOfBytesToWrite) > fileSize) {
                UINT64 bytes = fileSize - Offset;
                NumberOfBytesToWrite = (DWORD)bytes;
            }
        }
        off = Offset;
    }

    bool plain = false;
    if (encfs.altStream) {
        const LPCWSTR suffix = L":com.dropbox.attrs:$DATA";
        size_t str_len = wcslen(FileName);
        size_t suffix_len = wcslen(suffix);
        plain = str_len >= suffix_len && 0 == wcscmp(FileName + str_len - suffix_len, suffix);
    }

    int32_t writtenLen;
    if (!plain) {
        // Normal mode: encrypt and write data.
        writtenLen = encfsFile->write(FileName, fileSize, (char*)Buffer, off, NumberOfBytesToWrite);
        if (writtenLen == -1) {
            DWORD error = GetLastError();
            ErrorPrint(L"\twrite error = %u, buffer length = %d, write length = %d\n",
                error, NumberOfBytesToWrite, writtenLen);
            return DokanNtStatusFromWin32(error);
        }
        else {
            DbgPrint(L"\twrite %d, offset %I64d\n\n", writtenLen, Offset);
        }
    }
    else {
        // Plaintext write for specific alternate streams.
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = Offset;
        if (!SetFilePointerEx(encfsFile->getHandle(), distanceToMove, NULL, FILE_BEGIN)) {
            DWORD error = GetLastError();
            DbgPrint(L"\tseek error, offset = %d\n\n", (ULONG)Offset);
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

    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback for finding files in a directory.
 * @details Enumerates files in the underlying directory, decrypts their names,
 * adjusts their sizes to reflect decrypted content length, and passes the
 * information back to Dokan.
 */
static NTSTATUS DOKAN_CALLBACK
EncFSFindFiles(LPCWSTR FileName,
    PFillFindData FillFindData, // function pointer
    PDOKAN_FILE_INFO DokanFileInfo) {

    WCHAR filePath[DOKAN_MAX_PATH];
    size_t fileLen;
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    DWORD error;
    int count = 0;

    GetFilePath(filePath, FileName, false);
    DbgPrint(L"FindFiles : %s ; %s\n", FileName, filePath);

    fileLen = wcslen(filePath);
    if (fileLen + 2 >= DOKAN_MAX_PATH) return STATUS_BUFFER_OVERFLOW;

    // Append wildcard for FindFirstFile.
    if (filePath[fileLen - 1] != L'\\') {
        filePath[fileLen++] = L'\\';
    }
    filePath[fileLen] = L'*';
    filePath[fileLen + 1] = L'\0';

    hFind = FindFirstFileW(filePath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        ErrorPrint(L"FindFiles invalid file handle. Error is %u\n\n", error);
        return DokanNtStatusFromWin32(error);
    }
    // RAII for find handle.
    auto find_closer = [](HANDLE h) { FindClose(h); };
    std::unique_ptr<void, decltype(find_closer)> find_guard(hFind, find_closer);

    wstring wPath(FileName);
    wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
    string cPath = strConv.to_bytes(wPath);

    BOOLEAN rootFolder = (wcscmp(FileName, L"\\") == 0);
    do {
        // Skip '.' and '..' for root directory.
        if (!rootFolder || (wcscmp(findData.cFileName, L".") != 0 &&
            wcscmp(findData.cFileName, L"..") != 0)) {

            wstring wcFileName(findData.cFileName);
            string ccFileName = strConv.to_bytes(wcFileName);
            string cPlainFileName;
            try {
                if (encfs.isReverse()) {
                    // In reverse mode, encrypt filenames to show in the virtual drive.
                    if (wcscmp(findData.cFileName, L".encfs6.xml") != 0) {
                        encfs.encodeFileName(ccFileName, cPath, cPlainFileName);
                    }
                    else {
                        cPlainFileName = ccFileName;
                    }
                }
                else {
                    // Normal mode, decrypt filenames.
                    encfs.decodeFileName(ccFileName, cPath, cPlainFileName);
                }
            }
            catch (const EncFS::EncFSInvalidBlockException&) {
                // Skip files with invalid names (e.g., not valid base64).
                continue;
            }
            wstring wPlainFileName = strConv.from_bytes(cPlainFileName);
            wcscpy_s(findData.cFileName, wPlainFileName.c_str());
            findData.cAlternateFileName[0] = 0; // No 8.3 name.

            // Adjust file size to logical (decrypted) size.
            int64_t size = (findData.nFileSizeHigh * ((int64_t)MAXDWORD + 1)) + findData.nFileSizeLow;
            size = encfs.isReverse() ? encfs.toEncodedLength(size) : encfs.toDecodedLength(size);
            findData.nFileSizeLow = size & MAXDWORD;
            findData.nFileSizeHigh = (size >> 32) & MAXDWORD;

            // Pass the modified find data to Dokan.
            FillFindData(&findData, DokanFileInfo);
        }
        count++;
    } while (FindNextFileW(hFind, &findData) != 0);

    error = GetLastError();

    if (error != ERROR_NO_MORE_FILES) {
        ErrorPrint(L"\tFindNextFile error. Error is %u\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrint(L"\tFindFiles return %d entries in %s\n\n", count, filePath);
    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback for deleting a directory.
 * @details Checks if the directory is empty before allowing deletion.
 * The actual deletion is handled by Cleanup if this returns STATUS_SUCCESS.
 */
static NTSTATUS DOKAN_CALLBACK
EncFSDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    size_t fileLen;
    WCHAR filePath[DOKAN_MAX_PATH];

    GetFilePath(filePath, FileName, false);
    DbgPrint(L"DeleteDirectory %s ; %s - %d\n", FileName, filePath, DokanFileInfo->DeleteOnClose);

    // If not marked for deletion, it's a pre-check, so succeed.
    if (!DokanFileInfo->DeleteOnClose) {
        return STATUS_SUCCESS;
    }

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
        ErrorPrint(L"DeleteDirectory error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }
    auto find_closer = [](HANDLE h) { FindClose(h); };
    std::unique_ptr<void, decltype(find_closer)> find_guard(hFind, find_closer);

    // Check if the directory contains any files or subdirectories.
    do {
        if (wcscmp(findData.cFileName, L"..") != 0 &&
            wcscmp(findData.cFileName, L".") != 0) {
            DbgPrint(L"\tDirectory is not empty: %s\n", findData.cFileName);
            return STATUS_DIRECTORY_NOT_EMPTY;
        }
    } while (FindNextFileW(hFind, &findData) != 0);

    DWORD error = GetLastError();

    if (error != ERROR_NO_MORE_FILES) {
        ErrorPrint(L"DeleteDirectory error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Recursively changes IV of files in a directory and its subdirectories.
 * @param newFilePath New file path with possibly updated IV.
 * @param cOldPlainDirPath Old plain directory path.
 * @param cNewPlainDirPath New plain directory path.
 * @details This function is used when a directory is moved/renamed, which may
 * require updating the IVs of all contained files if chained IV is used.
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

    DbgPrint(L"ChangeIVRecursive for: %s\n", newFilePath);

    WIN32_FIND_DATAW find;
    HANDLE findHandle = FindFirstFileW(findPath, &find);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return DokanNtStatusFromWin32(GetLastError());
    }
    auto find_closer = [](HANDLE h) { FindClose(h); };
    std::unique_ptr<void, decltype(find_closer)> find_guard(findHandle, find_closer);

    wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;

    do {
        if (find.cFileName[0] == L'.') continue;

        wstring wOldName(find.cFileName);
        string cOldName = strConv.to_bytes(wOldName);
        string plainName;
        try {
            encfs.decodeFileName(cOldName, cOldPlainDirPath, plainName);
        }
        catch (const EncFS::EncFSInvalidBlockException&) {
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

        // If name IV chaining is used, the filename itself must be changed.
        if (encfs.isChainedNameIV()) {
            if (!MoveFileW(oldPath, newPath)) {
                return DokanNtStatusFromWin32(GetLastError());
            }
        }

        string cPlainOldPath = cOldPlainDirPath + "\\" + plainName;
        wstring wPlainOldPath = strConv.from_bytes(cPlainOldPath);
        string cPlainNewPath = cNewPlainDirPath + "\\" + plainName;
        wstring wPlainNewPath = strConv.from_bytes(cPlainNewPath);

        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectories.
            NTSTATUS status = changeIVRecursive(newPath, cPlainOldPath, cPlainNewPath);
            if (status != STATUS_SUCCESS) {
                return status;
            }
        }
        else {
            // If external IV chaining is used, the file content's IV must be updated.
            if (encfs.isExternalIVChaining()) {
                HANDLE handle2 = CreateFileW(newPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
                if (handle2 == INVALID_HANDLE_VALUE) {
                    return DokanNtStatusFromWin32(GetLastError());
                }

                EncFS::EncFSFile encfsFile2(handle2, false);
                if (!encfsFile2.changeFileIV(wPlainOldPath.c_str(), wPlainNewPath.c_str())) {
                    return DokanNtStatusFromWin32(GetLastError());
                }
                // encfsFile2 is destructed here, closing handle2.
            }
        }
    } while (FindNextFileW(findHandle, &find));

    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback for moving or renaming a file or directory.
 * @details Handles the move/rename operation. If IV chaining is enabled, this
 * can be a complex operation involving recursive updates to file contents or names.
 */
static NTSTATUS DOKAN_CALLBACK
EncFSMoveFile(LPCWSTR FileName, LPCWSTR NewFileName, BOOL ReplaceIfExisting,
    PDOKAN_FILE_INFO DokanFileInfo) {

    BOOL result;
    size_t newFilePathLen;

    WCHAR filePath[DOKAN_MAX_PATH];
    WCHAR newFilePath[DOKAN_MAX_PATH];
    GetFilePath(filePath, FileName, false);
    GetFilePath(newFilePath, NewFileName, true);

    DbgPrint(L"MoveFile %s -> %s ; %s -> %s\n", FileName, NewFileName, filePath, newFilePath);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"\tinvalid handle\n");
        return STATUS_INVALID_HANDLE;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    // If IVs depend on the path, moving a file/dir requires IV updates.
    if (encfs.isChainedNameIV() || encfs.isExternalIVChaining()) {
        if (DokanFileInfo->IsDirectory) {
            lock_guard<decltype(dirMoveLock)> dlock(dirMoveLock);

            // The handle in the context must be closed before moving the directory.
            delete encfsFile;
            DokanFileInfo->Context = 0;

            if (!MoveFileW(filePath, newFilePath)) {
                return DokanNtStatusFromWin32(GetLastError());
            }

            // After moving, recursively update IVs of all children.
            wstring wOldPlainDirPath(FileName);
            wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
            string cOldPlainDirPath = strConv.to_bytes(wOldPlainDirPath);
            wstring wNewPlainDirPath(NewFileName);
            string cNewPlainDirPath = strConv.to_bytes(wNewPlainDirPath);

            return changeIVRecursive(newFilePath, cOldPlainDirPath, cNewPlainDirPath);
        }
        else {
            // For a single file, just update its IV.
            if (encfs.isExternalIVChaining()) {
                if (!encfsFile->changeFileIV(FileName, NewFileName)) {
                    return DokanNtStatusFromWin32(GetLastError());
                }
            }
        }
    }

    // For the actual move, use SetFileInformationByHandle with FileRenameInfo.
    newFilePathLen = wcslen(newFilePath);
    DWORD bufferSize = (DWORD)(sizeof(FILE_RENAME_INFO) + newFilePathLen * sizeof(newFilePath[0]));

    // Use vector for RAII-based memory management.
    std::vector<BYTE> renameInfoBuffer(bufferSize);
    PFILE_RENAME_INFO renameInfo = (PFILE_RENAME_INFO)renameInfoBuffer.data();

    renameInfo->ReplaceIfExists = ReplaceIfExisting ? TRUE : FALSE;
    renameInfo->RootDirectory = NULL;
    renameInfo->FileNameLength = (DWORD)newFilePathLen * sizeof(newFilePath[0]);
    wcscpy_s(renameInfo->FileName, newFilePathLen + 1, newFilePath);

    result = SetFileInformationByHandle(encfsFile->getHandle(), FileRenameInfo, renameInfo, bufferSize);

    if (result) {
        return STATUS_SUCCESS;
    }
    else {
        DWORD error = GetLastError();
        ErrorPrint(L"\tMoveFile error = %u\n", error);
        return DokanNtStatusFromWin32(error);
    }
}

/**
 * @brief Dokan callback to lock a portion of a file.
 */
static NTSTATUS DOKAN_CALLBACK EncFSLockFile(LPCWSTR FileName,
    LONGLONG ByteOffset, LONGLONG Length, PDOKAN_FILE_INFO DokanFileInfo) {
    LARGE_INTEGER offset;
    LARGE_INTEGER length;

    DbgPrint(L"LockFile  %s\n", FileName);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }
    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    length.QuadPart = Length;
    offset.QuadPart = ByteOffset;

    if (!LockFile(encfsFile->getHandle(), offset.LowPart, offset.HighPart, length.LowPart, length.HighPart)) {
        DWORD error = GetLastError();
        ErrorPrint(L"LockFile error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrint(L"\tsuccess\n\n");
    return STATUS_SUCCESS;
}

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
 * @brief Dokan callback to flush file buffers.
 * @details Commits any cached data to the underlying file.
 */
static NTSTATUS DOKAN_CALLBACK
EncFSFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    DbgPrint(L"FlushFileBuffers: %s\n", FileName);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"\tinvalid handle\n\n");
        return STATUS_SUCCESS;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);
    if (encfsFile->flush()) {
        return STATUS_SUCCESS;
    }
    else {
        DWORD error = GetLastError();
        ErrorPrint(L"\tflush error code = %d\n", error);
        return DokanNtStatusFromWin32(error);
    }
}

/**
 * @brief Dokan callback to set the end-of-file position.
 * @details This is used to truncate or extend a file.
 */
static NTSTATUS DOKAN_CALLBACK EncFSSetEndOfFile(
    LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrint(L"SetEndOfFile %s, %I64d\n", FileName, ByteOffset);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);
    if (!encfsFile->setLength(FileName, ByteOffset)) {
        DWORD error = GetLastError();
        ErrorPrint(L"\tSetFilePointer error: %d, offset = %I64d\n\n", error, ByteOffset);
        return DokanNtStatusFromWin32(error);
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to get file information (attributes, size, timestamps).
 * @details Retrieves metadata from the underlying file and adjusts the file size
 * to its logical (decrypted) length.
 */
static NTSTATUS DOKAN_CALLBACK EncFSGetFileInformation(
    LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrint(L"GetFileInfo : %s\n", FileName);

    EncFS::EncFSFile* encfsFile;
    std::unique_ptr<EncFS::EncFSFile> tempEncFSFile;

    if (!DokanFileInfo->Context) {
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);
        DbgPrint(L"\tinvalid handle, cleanuped?\n");
        HANDLE handle = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            return DokanNtStatusFromWin32(GetLastError());
        }
        tempEncFSFile = std::make_unique<EncFS::EncFSFile>(handle, false);
        encfsFile = tempEncFSFile.get();
    }
    else {
        encfsFile = ToEncFSFile(DokanFileInfo->Context);
    }

    if (!GetFileInformationByHandle(encfsFile->getHandle(), HandleFileInformation)) {
        ErrorPrint(L"GetFileInfo error code = %d\n", GetLastError());
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);

        // Fallback for root directory or if GetFileInformationByHandle fails.
        if (wcslen(FileName) == 1) {
            DbgPrint(L"  root dir\n");
            HandleFileInformation->dwFileAttributes = GetFileAttributesW(filePath);
        }
        else {
            WIN32_FIND_DATAW find;
            HANDLE findHandle = FindFirstFileW(filePath, &find);
            if (findHandle == INVALID_HANDLE_VALUE) {
                return DokanNtStatusFromWin32(GetLastError());
            }
            HandleFileInformation->dwFileAttributes = find.dwFileAttributes;
            HandleFileInformation->ftCreationTime = find.ftCreationTime;
            HandleFileInformation->ftLastAccessTime = find.ftLastAccessTime;
            HandleFileInformation->ftLastWriteTime = find.ftLastWriteTime;
            HandleFileInformation->nFileSizeHigh = find.nFileSizeHigh;
            HandleFileInformation->nFileSizeLow = find.nFileSizeLow;
            FindClose(findHandle);
        }
    }

    // Adjust file size to logical (decrypted) size for files.
    if (!DokanFileInfo->IsDirectory) {
        int64_t size = (HandleFileInformation->nFileSizeHigh * ((int64_t)MAXDWORD + 1)) + HandleFileInformation->nFileSizeLow;
        size = encfs.isReverse() ? encfs.toEncodedLength(size) : encfs.toDecodedLength(size);
        HandleFileInformation->nFileSizeLow = size & MAXDWORD;
        HandleFileInformation->nFileSizeHigh = (size >> 32) & MAXDWORD;
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback for deleting a file.
 * @details This is a pre-delete check. The actual deletion happens in Cleanup
 * if the file was opened with FILE_FLAG_DELETE_ON_CLOSE.
 */
static NTSTATUS DOKAN_CALLBACK
EncFSDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];

    GetFilePath(filePath, FileName, false);
    DbgPrint(L"DeleteFile %s ; %s - %d\n", FileName, filePath, DokanFileInfo->DeleteOnClose);

    DWORD dwAttrib = GetFileAttributesW(filePath);

    // Cannot delete a directory with DeleteFile.
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
        return STATUS_ACCESS_DENIED;

    // If there is a handle, set the disposition info.
    if (DokanFileInfo->Context) {
        FILE_DISPOSITION_INFO fdi;
        fdi.DeleteFile = DokanFileInfo->DeleteOnClose;
        EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);
        if (!SetFileInformationByHandle(encfsFile->getHandle(), FileDispositionInfo, &fdi, sizeof(FILE_DISPOSITION_INFO)))
            return DokanNtStatusFromWin32(GetLastError());
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to set the allocation size for a file.
 * @details This is often a hint to pre-allocate space. We only handle truncation here.
 */
static NTSTATUS DOKAN_CALLBACK EncFSSetAllocationSize(
    LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrint(L"SetAllocationSize %s, %I64d\n", FileName, AllocSize);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"\tinvalid handle\n");
        return STATUS_INVALID_HANDLE;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);
    LARGE_INTEGER encodedFileSize;
    if (GetFileSizeEx(encfsFile->getHandle(), &encodedFileSize)) {
        size_t decodedFileSize = encfs.toDecodedLength(encodedFileSize.QuadPart);
        // Only truncate the file if the new allocation size is smaller.
        if (AllocSize < (int64_t)decodedFileSize) {
            if (!encfsFile->setLength(FileName, AllocSize)) {
                DWORD error = GetLastError();
                ErrorPrint(L"\tSetFilePointer error: %d, offset = %I64d\n", error, AllocSize);
                return DokanNtStatusFromWin32(error);
            }
            DbgPrint(L"Logical file size truncated from %I64d to %I64d", decodedFileSize, AllocSize);
        }
    }
    else {
        DWORD error = GetLastError();
        ErrorPrint(L"GetFileSize error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to set file attributes.
 */
static NTSTATUS DOKAN_CALLBACK EncFSSetFileAttributes(
    LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    DbgPrint(L"SetFileAttributes %s 0x%x\n", FileName, FileAttributes);

    // A value of 0 means attributes should not be changed.
    if (FileAttributes != 0) {
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);
        if (!SetFileAttributesW(filePath, FileAttributes)) {
            DWORD error = GetLastError();
            DbgPrint(L"\terror code = %d\n\n", error);
            return DokanNtStatusFromWin32(error);
        }
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to set file timestamps.
 */
static NTSTATUS DOKAN_CALLBACK
EncFSSetFileTime(LPCWSTR FileName, CONST FILETIME* CreationTime,
    CONST FILETIME* LastAccessTime, CONST FILETIME* LastWriteTime,
    PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrint(L"SetFileTime %s\n", FileName);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }
    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    if (!SetFileTime(encfsFile->getHandle(), CreationTime, LastAccessTime, LastWriteTime)) {
        DWORD error = GetLastError();
        ErrorPrint(L"SetFileTime error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to unlock a portion of a file.
 */
static NTSTATUS DOKAN_CALLBACK
EncFSUnlockFile(LPCWSTR FileName, LONGLONG ByteOffset, LONGLONG Length,
    PDOKAN_FILE_INFO DokanFileInfo) {
    LARGE_INTEGER length;
    LARGE_INTEGER offset;

    DbgPrint(L"UnlockFile %s\n", FileName);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    length.QuadPart = Length;
    offset.QuadPart = ByteOffset;

    if (!UnlockFile(encfsFile->getHandle(), offset.LowPart, offset.HighPart, length.LowPart, length.HighPart)) {
        DWORD error = GetLastError();
        ErrorPrint(L"UnlockFile error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrint(L"\tsuccess\n\n");
    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to get file security information (ACLs).
 */
static NTSTATUS DOKAN_CALLBACK EncFSGetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
    PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo) {

    WCHAR filePath[DOKAN_MAX_PATH];
    BOOLEAN requestingSaclInfo;
    UNREFERENCED_PARAMETER(DokanFileInfo);

    GetFilePath(filePath, FileName, false);
    DbgPrint(L"GetFileSecurity %s ; %s\n", FileName, filePath);

    requestingSaclInfo = ((*SecurityInformation & SACL_SECURITY_INFORMATION) ||
        (*SecurityInformation & BACKUP_SECURITY_INFORMATION));

    // Reading SACL requires a specific privilege.
    if (!g_efo.g_HasSeSecurityPrivilege) {
        *SecurityInformation &= ~SACL_SECURITY_INFORMATION;
        *SecurityInformation &= ~BACKUP_SECURITY_INFORMATION;
    }

    DbgPrint(L"  Opening new handle with READ_CONTROL access\n");
    HANDLE handle = CreateFileW(
        filePath,
        READ_CONTROL | ((requestingSaclInfo && g_efo.g_HasSeSecurityPrivilege) ? ACCESS_SYSTEM_SECURITY : 0),
        FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (handle == INVALID_HANDLE_VALUE) {
        DbgPrint(L"\tinvalid handle\n\n");
        return DokanNtStatusFromWin32(GetLastError());
    }
    auto close_handle = [](HANDLE h) { CloseHandle(h); };
    std::unique_ptr<void, decltype(close_handle)> handle_guard(handle, close_handle);

    if (!GetKernelObjectSecurity(handle, *SecurityInformation, SecurityDescriptor, BufferLength, LengthNeeded)) {
        int error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            return STATUS_BUFFER_OVERFLOW;
        }
        else {
            ErrorPrint(L"  GetKernelObjectSecurity  error: %d\n", error);
            return DokanNtStatusFromWin32(error);
        }
    }

    *LengthNeeded = GetSecurityDescriptorLength(SecurityDescriptor);

    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to set file security information (ACLs).
 */
static NTSTATUS DOKAN_CALLBACK EncFSSetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG SecurityDescriptorLength,
    PDOKAN_FILE_INFO DokanFileInfo) {

    UNREFERENCED_PARAMETER(SecurityDescriptorLength);
    DbgPrint(L"SetFileSecurity %s\n", FileName);

    if (!DokanFileInfo->Context) {
        DbgPrint(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }
    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    if (!SetKernelObjectSecurity(encfsFile->getHandle(), *SecurityInformation, SecurityDescriptor)) {
        int error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            return STATUS_BUFFER_OVERFLOW;
        }
        else {
            ErrorPrint(L"  SetKernelObjectSecurity error: %d\n", error);
            return DokanNtStatusFromWin32(error);
        }
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to get volume information.
 * @details Provides information about the virtual volume, such as its name,
 * serial number, and filesystem features. It tries to mirror some features
 * of the underlying volume.
 */
static NTSTATUS DOKAN_CALLBACK EncFSGetVolumeInformation(
    LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
    LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
    LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
    PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    WCHAR volumeRoot[4];
    DWORD fsFlags = 0;

    wcscpy_s(VolumeNameBuffer, VolumeNameSize, L"EncFS");

    if (MaximumComponentLength) *MaximumComponentLength = 255;
    if (FileSystemFlags) {
        *FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES |
            FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK | FILE_PERSISTENT_ACLS;
        if (encfs.altStream) *FileSystemFlags |= FILE_NAMED_STREAMS;
    }

    volumeRoot[0] = g_efo.RootDirectory[0];
    volumeRoot[1] = ':';
    volumeRoot[2] = '\\';
    volumeRoot[3] = '\0';

    // Try to get info from the underlying volume to be consistent.
    if (GetVolumeInformationW(volumeRoot, NULL, 0, VolumeSerialNumber, MaximumComponentLength,
        &fsFlags, FileSystemNameBuffer, FileSystemNameSize)) {

        if (FileSystemFlags) {
            *FileSystemFlags &= fsFlags; // Inherit flags.
            if (!encfs.altStream) {
                *FileSystemFlags &= ~FILE_NAMED_STREAMS; // But override stream support.
            }
        }
    }
    else {
        DbgPrint(L"GetVolumeInformation: unable to query underlying fs, using defaults.\n");
        // Default to NTFS for max compatibility if underlying FS is not available.
        wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, L"NTFS");
    }

    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback to get disk free space.
 * @details Passes through the query to the underlying volume.
 */
static NTSTATUS DOKAN_CALLBACK EncFSDokanGetDiskFreeSpace(
    PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes,
    PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    WCHAR volumeRoot[4];
    volumeRoot[0] = g_efo.RootDirectory[0];
    volumeRoot[1] = ':';
    volumeRoot[2] = '\\';
    volumeRoot[3] = '\0';

    ULARGE_INTEGER lpFreeBytesAvailable;
    ULARGE_INTEGER lpTotalNumberOfBytes;
    ULARGE_INTEGER lpTotalNumberOfFreeBytes;
    GetDiskFreeSpaceExW(volumeRoot, &lpFreeBytesAvailable, &lpTotalNumberOfBytes, &lpTotalNumberOfFreeBytes);
    *FreeBytesAvailable = (ULONGLONG)lpFreeBytesAvailable.QuadPart;
    *TotalNumberOfBytes = (ULONGLONG)lpTotalNumberOfBytes.QuadPart;
    *TotalNumberOfFreeBytes = (ULONGLONG)lpTotalNumberOfFreeBytes.QuadPart;

    return STATUS_SUCCESS;
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
 * @brief Dokan callback to find alternate data streams for a file.
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
    DbgPrint(L"FindStreams :%s ; %s\n", FileName, filePath);

    hFind = FindFirstStreamW(filePath, FindStreamInfoStandard, &findData, 0);

    if (hFind == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        ErrorPrint(L"FindStream invalid file handle. Error is %u", error);
        return DokanNtStatusFromWin32(error);
    }
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
        // Per Dokan docs, return success even on buffer overflow.
        return STATUS_SUCCESS;
    }

    if (error != ERROR_HANDLE_EOF) {
        ErrorPrint(L"\tFindNextStreamW error. Error is %u\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrint(L"\tFindStreams return %d entries in %s\n\n", count, filePath);
    return STATUS_SUCCESS;
}

/**
 * @brief Dokan callback executed when the volume is successfully mounted.
 */
static NTSTATUS DOKAN_CALLBACK EncFSMounted(LPCWSTR MountPoint, PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);
    DbgPrint(L"Mounted as %s\n", MountPoint);
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
    DbgPrint(L"Unmounted\n");
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
    dokanOptions.SingleThread = efo.SingleThread;
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
    dokanOptions.Options |= DOKAN_OPTION_CASE_SENSITIVE;

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