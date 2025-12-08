/**
 * @file EncFSy_dokan_fs.cpp
 * @brief Dokan filesystem callback implementations for file system operations.
 *
 * This file implements Dokan callbacks for file locking, flushing, file info,
 * security descriptors, volume information, and mount/unmount events.
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

/**
 * @brief Converts Dokan context to EncFSFile pointer.
 * @param context The ULONG64 context from DokanFileInfo.
 * @return Pointer to the EncFSFile object.
 */
static inline EncFS::EncFSFile* ToEncFSFile(ULONG64 context) {
    return reinterpret_cast<EncFS::EncFSFile*>(context);
}

/**
 * @brief Locks a byte range in a file.
 * @param FileName Virtual path of the file.
 * @param ByteOffset Starting offset of the lock range.
 * @param Length Number of bytes to lock.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSLockFile(LPCWSTR FileName,
    LONGLONG ByteOffset, LONGLONG Length, PDOKAN_FILE_INFO DokanFileInfo) {
    LARGE_INTEGER offset;
    LARGE_INTEGER length;

    DbgPrintV(L"LockFile: '%s' offset=%I64d len=%I64d\n", FileName, ByteOffset, Length);

    if (!DokanFileInfo->Context) {
        DbgPrintV(L"  [WARN] Context is NULL, handle already closed\n");
        return STATUS_INVALID_HANDLE;
    }
    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    length.QuadPart = Length;
    offset.QuadPart = ByteOffset;

    if (!LockFile(encfsFile->getHandle(), offset.LowPart, offset.HighPart, length.LowPart, length.HighPart)) {
        DWORD error = GetLastError();
        ErrorPrint(L"LockFile: '%s' FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrintV(L"LockFile: '%s' OK\n", FileName);
    return STATUS_SUCCESS;
}

/**
 * @brief Flushes file buffers to disk.
 * @param FileName Virtual path of the file.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    DbgPrintV(L"FlushFileBuffers: '%s'\n", FileName);

    if (!DokanFileInfo->Context) {
        DbgPrintV(L"  [WARN] Context is NULL, returning success\n");
        return STATUS_SUCCESS;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);
    if (encfsFile->flush()) {
        DbgPrintV(L"FlushFileBuffers: '%s' OK\n", FileName);
        return STATUS_SUCCESS;
    }
    else {
        DWORD error = GetLastError();
        ErrorPrint(L"FlushFileBuffers: '%s' FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }
}

/**
 * @brief Sets the end-of-file position (truncates or extends the file).
 * @param FileName Virtual path of the file.
 * @param ByteOffset New logical file size.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSSetEndOfFile(
    LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrintV(L"SetEndOfFile: '%s' newSize=%I64d\n", FileName, ByteOffset);

    if (!DokanFileInfo->Context) {
        DbgPrintV(L"  [WARN] Context is NULL\n");
        return STATUS_INVALID_HANDLE;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);
    
    // Acquire file-level lock to prevent data corruption during concurrent access
    EncFS::FileScopedLock fileLock(FileName);
    
    if (!encfsFile->setLength(FileName, ByteOffset)) {
        DWORD error = GetLastError();
        ErrorPrint(L"SetEndOfFile: '%s' FAILED (error=%lu, size=%I64d)\n", FileName, error, ByteOffset);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrintV(L"SetEndOfFile: '%s' OK\n", FileName);
    return STATUS_SUCCESS;
}

/**
 * @brief Retrieves file information (attributes, timestamps, size).
 * @param FileName Virtual path of the file.
 * @param HandleFileInformation Output structure for file information.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSGetFileInformation(
    LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrintV(L"GetFileInfo: '%s' isDir=%d\n", FileName, DokanFileInfo->IsDirectory);

    EncFS::EncFSFile* encfsFile;
    std::unique_ptr<EncFS::EncFSFile> tempEncFSFile;

    // If context is NULL, open a temporary handle for this operation
    if (!DokanFileInfo->Context) {
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);
        DbgPrintV(L"  [INFO] Context is NULL (cleanup occurred?), opening temp handle\n");
        HANDLE handle = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            ErrorPrint(L"GetFileInfo: '%s' CreateFile FAILED (error=%lu)\n", FileName, error);
            return DokanNtStatusFromWin32(error);
        }
        tempEncFSFile = std::make_unique<EncFS::EncFSFile>(handle, false);
        encfsFile = tempEncFSFile.get();
    }
    else {
        encfsFile = ToEncFSFile(DokanFileInfo->Context);
    }

    if (!GetFileInformationByHandle(encfsFile->getHandle(), HandleFileInformation)) {
        DWORD error = GetLastError();
        DbgPrintV(L"  [WARN] GetFileInformationByHandle failed (error=%lu), using fallback\n", error);
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);

        // Fallback: use GetFileAttributes for root, FindFirstFile for others
        if (wcslen(FileName) == 1) {
            DbgPrintV(L"  [INFO] Root directory fallback\n");
            HandleFileInformation->dwFileAttributes = GetFileAttributesW(filePath);
        }
        else {
            WIN32_FIND_DATAW find;
            HANDLE findHandle = FindFirstFileW(filePath, &find);
            if (findHandle == INVALID_HANDLE_VALUE) {
                error = GetLastError();
                ErrorPrint(L"GetFileInfo: '%s' FindFirstFile FAILED (error=%lu)\n", FileName, error);
                return DokanNtStatusFromWin32(error);
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

    // Convert physical (encrypted) size to logical (decrypted) size for files
    if (!DokanFileInfo->IsDirectory) {
        int64_t physicalSize = (HandleFileInformation->nFileSizeHigh * ((int64_t)MAXDWORD + 1)) + HandleFileInformation->nFileSizeLow;
        int64_t logicalSize = encfs.isReverse() ? encfs.toEncodedLength(physicalSize) : encfs.toDecodedLength(physicalSize);
        HandleFileInformation->nFileSizeLow = logicalSize & MAXDWORD;
        HandleFileInformation->nFileSizeHigh = (logicalSize >> 32) & MAXDWORD;
        DbgPrintV(L"  [INFO] Size: physical=%I64d -> logical=%I64d\n", physicalSize, logicalSize);
    }

    DbgPrintV(L"GetFileInfo: '%s' OK (attr=0x%08X)\n", FileName, HandleFileInformation->dwFileAttributes);
    return STATUS_SUCCESS;
}

/**
 * @brief Sets the allocation size for a file (may truncate if smaller).
 * @param FileName Virtual path of the file.
 * @param AllocSize New allocation size.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSSetAllocationSize(
    LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrintV(L"SetAllocationSize: '%s' allocSize=%I64d\n", FileName, AllocSize);

    if (!DokanFileInfo->Context) {
        DbgPrintV(L"  [WARN] Context is NULL\n");
        return STATUS_INVALID_HANDLE;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);
    
    // Acquire file-level lock to prevent data corruption during concurrent access
    EncFS::FileScopedLock fileLock(FileName);
    
    LARGE_INTEGER encodedFileSize;
    if (GetFileSizeEx(encfsFile->getHandle(), &encodedFileSize)) {
        size_t decodedFileSize = encfs.toDecodedLength(encodedFileSize.QuadPart);
        // Truncate only if new allocation is smaller than current logical size
        if (AllocSize < (int64_t)decodedFileSize) {
            DbgPrintV(L"  [INFO] Truncating: %I64d -> %I64d\n", (int64_t)decodedFileSize, AllocSize);
            if (!encfsFile->setLength(FileName, AllocSize)) {
                DWORD error = GetLastError();
                ErrorPrint(L"SetAllocationSize: '%s' setLength FAILED (error=%lu)\n", FileName, error);
                return DokanNtStatusFromWin32(error);
            }
        }
        else {
            DbgPrintV(L"  [INFO] No truncation needed (current=%I64d >= alloc=%I64d)\n", (int64_t)decodedFileSize, AllocSize);
        }
    }
    else {
        DWORD error = GetLastError();
        ErrorPrint(L"SetAllocationSize: '%s' GetFileSizeEx FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrintV(L"SetAllocationSize: '%s' OK\n", FileName);
    return STATUS_SUCCESS;
}

/**
 * @brief Sets file attributes (read-only, hidden, system, etc.).
 * @param FileName Virtual path of the file.
 * @param FileAttributes New file attributes (0 = no change).
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSSetFileAttributes(
    LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    DbgPrintV(L"SetFileAttributes: '%s' attr=0x%08X\n", FileName, FileAttributes);

    // A value of 0 means no change requested
    if (FileAttributes != 0) {
        WCHAR filePath[DOKAN_MAX_PATH];
        GetFilePath(filePath, FileName, false);
        if (!SetFileAttributesW(filePath, FileAttributes)) {
            DWORD error = GetLastError();
            ErrorPrint(L"SetFileAttributes: '%s' FAILED (error=%lu, attr=0x%08X)\n", FileName, error, FileAttributes);
            return DokanNtStatusFromWin32(error);
        }
    }
    else {
        DbgPrintV(L"  [INFO] Attributes=0, no change needed\n");
    }

    DbgPrintV(L"SetFileAttributes: '%s' OK\n", FileName);
    return STATUS_SUCCESS;
}

/**
 * @brief Sets file timestamps (creation, last access, last write).
 * @param FileName Virtual path of the file.
 * @param CreationTime New creation time (NULL = no change).
 * @param LastAccessTime New last access time (NULL = no change).
 * @param LastWriteTime New last write time (NULL = no change).
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSSetFileTime(LPCWSTR FileName, CONST FILETIME* CreationTime,
    CONST FILETIME* LastAccessTime, CONST FILETIME* LastWriteTime,
    PDOKAN_FILE_INFO DokanFileInfo) {

    DbgPrintV(L"SetFileTime: '%s' (create=%s, access=%s, write=%s)\n",
        FileName,
        CreationTime ? L"set" : L"null",
        LastAccessTime ? L"set" : L"null",
        LastWriteTime ? L"set" : L"null");

    if (!DokanFileInfo->Context) {
        DbgPrintV(L"  [WARN] Context is NULL\n");
        return STATUS_INVALID_HANDLE;
    }
    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    if (!SetFileTime(encfsFile->getHandle(), CreationTime, LastAccessTime, LastWriteTime)) {
        DWORD error = GetLastError();
        ErrorPrint(L"SetFileTime: '%s' FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrintV(L"SetFileTime: '%s' OK\n", FileName);
    return STATUS_SUCCESS;
}

/**
 * @brief Unlocks a previously locked byte range in a file.
 * @param FileName Virtual path of the file.
 * @param ByteOffset Starting offset of the locked range.
 * @param Length Number of bytes to unlock.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK
EncFSUnlockFile(LPCWSTR FileName, LONGLONG ByteOffset, LONGLONG Length,
    PDOKAN_FILE_INFO DokanFileInfo) {
    LARGE_INTEGER length;
    LARGE_INTEGER offset;

    DbgPrintV(L"UnlockFile: '%s' offset=%I64d len=%I64d\n", FileName, ByteOffset, Length);

    if (!DokanFileInfo->Context) {
        DbgPrintV(L"  [WARN] Context is NULL\n");
        return STATUS_INVALID_HANDLE;
    }

    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    length.QuadPart = Length;
    offset.QuadPart = ByteOffset;

    if (!UnlockFile(encfsFile->getHandle(), offset.LowPart, offset.HighPart, length.LowPart, length.HighPart)) {
        DWORD error = GetLastError();
        ErrorPrint(L"UnlockFile: '%s' FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }

    DbgPrintV(L"UnlockFile: '%s' OK\n", FileName);
    return STATUS_SUCCESS;
}

/**
 * @brief Retrieves the security descriptor of a file or directory.
 * @param FileName Virtual path of the file.
 * @param SecurityInformation Which security info is requested (DACL, SACL, etc.).
 * @param SecurityDescriptor Output buffer for security descriptor.
 * @param BufferLength Size of the output buffer.
 * @param LengthNeeded Output: actual length needed.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSGetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
    PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo) {

    WCHAR filePath[DOKAN_MAX_PATH];
    BOOLEAN requestingSaclInfo;
    UNREFERENCED_PARAMETER(DokanFileInfo);

    GetFilePath(filePath, FileName, false);
    DbgPrintV(L"GetFileSecurity: '%s' secInfo=0x%08X bufLen=%lu\n", FileName, *SecurityInformation, BufferLength);

    requestingSaclInfo = ((*SecurityInformation & SACL_SECURITY_INFORMATION) ||
        (*SecurityInformation & BACKUP_SECURITY_INFORMATION));

    // SACL access requires SeSecurityPrivilege
    if (!g_efo.g_HasSeSecurityPrivilege) {
        *SecurityInformation &= ~SACL_SECURITY_INFORMATION;
        *SecurityInformation &= ~BACKUP_SECURITY_INFORMATION;
        DbgPrintV(L"  [INFO] SeSecurityPrivilege not held, SACL info stripped\n");
    }

    DbgPrintV(L"  [INFO] Opening handle with READ_CONTROL access\n");
    HANDLE handle = CreateFileW(
        filePath,
        READ_CONTROL | ((requestingSaclInfo && g_efo.g_HasSeSecurityPrivilege) ? ACCESS_SYSTEM_SECURITY : 0),
        FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        ErrorPrint(L"GetFileSecurity: '%s' CreateFile FAILED (error=%lu)\n", FileName, error);
        return DokanNtStatusFromWin32(error);
    }
    // RAII guard for handle cleanup
    auto close_handle = [](HANDLE h) { CloseHandle(h); };
    std::unique_ptr<void, decltype(close_handle)> handle_guard(handle, close_handle);

    if (!GetKernelObjectSecurity(handle, *SecurityInformation, SecurityDescriptor, BufferLength, LengthNeeded)) {
        int error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            DbgPrintV(L"  [INFO] Buffer too small (needed=%lu, provided=%lu)\n", *LengthNeeded, BufferLength);
            return STATUS_BUFFER_OVERFLOW;
        }
        else {
            ErrorPrint(L"GetFileSecurity: '%s' GetKernelObjectSecurity FAILED (error=%d)\n", FileName, error);
            return DokanNtStatusFromWin32(error);
        }
    }

    *LengthNeeded = GetSecurityDescriptorLength(SecurityDescriptor);
    DbgPrintV(L"GetFileSecurity: '%s' OK (sdLen=%lu)\n", FileName, *LengthNeeded);
    return STATUS_SUCCESS;
}

/**
 * @brief Sets the security descriptor of a file or directory.
 * @param FileName Virtual path of the file.
 * @param SecurityInformation Which security info to set.
 * @param SecurityDescriptor The new security descriptor.
 * @param SecurityDescriptorLength Length of the security descriptor.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSSetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG SecurityDescriptorLength,
    PDOKAN_FILE_INFO DokanFileInfo) {

    UNREFERENCED_PARAMETER(SecurityDescriptorLength);
    DbgPrintV(L"SetFileSecurity: '%s' secInfo=0x%08X\n", FileName, *SecurityInformation);

    if (!DokanFileInfo->Context) {
        DbgPrintV(L"  [WARN] Context is NULL\n");
        return STATUS_INVALID_HANDLE;
    }
    EncFS::EncFSFile* encfsFile = ToEncFSFile(DokanFileInfo->Context);

    if (!SetKernelObjectSecurity(encfsFile->getHandle(), *SecurityInformation, SecurityDescriptor)) {
        int error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            return STATUS_BUFFER_OVERFLOW;
        }
        else {
            ErrorPrint(L"SetFileSecurity: '%s' FAILED (error=%d)\n", FileName, error);
            return DokanNtStatusFromWin32(error);
        }
    }

    DbgPrintV(L"SetFileSecurity: '%s' OK\n", FileName);
    return STATUS_SUCCESS;
}

/**
 * @brief Retrieves volume information (name, serial, FS type, capabilities).
 * @param VolumeNameBuffer Output buffer for volume name.
 * @param VolumeNameSize Size of VolumeNameBuffer.
 * @param VolumeSerialNumber Output: volume serial number.
 * @param MaximumComponentLength Output: max file name component length.
 * @param FileSystemFlags Output: supported FS features.
 * @param FileSystemNameBuffer Output buffer for FS name (e.g., "NTFS").
 * @param FileSystemNameSize Size of FileSystemNameBuffer.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSGetVolumeInformation(
    LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
    LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
    LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
    PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    DbgPrintV(L"GetVolumeInformation\n");

    WCHAR volumeRoot[4];
    DWORD fsFlags = 0;

    // Use custom volume name if set, otherwise default to "EncFS"
    if (g_efo.VolumeName[0] != L'\0') {
        wcscpy_s(VolumeNameBuffer, VolumeNameSize, g_efo.VolumeName);
    } else {
        wcscpy_s(VolumeNameBuffer, VolumeNameSize, L"EncFS");
    }

    if (MaximumComponentLength) *MaximumComponentLength = 255;
    if (FileSystemFlags) {
        // Default FS capabilities
        *FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES |
            FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK | FILE_PERSISTENT_ACLS;
        if (encfs.altStream) *FileSystemFlags |= FILE_NAMED_STREAMS;
    }

    // Build underlying volume root path (e.g., "C:\")
    volumeRoot[0] = g_efo.RootDirectory[0];
    volumeRoot[1] = ':';
    volumeRoot[2] = '\\';
    volumeRoot[3] = '\0';

    // Query underlying volume for consistent behavior
    DWORD underlyingSerial = 0;
    if (GetVolumeInformationW(volumeRoot, NULL, 0, &underlyingSerial, MaximumComponentLength,
        &fsFlags, FileSystemNameBuffer, FileSystemNameSize)) {

        if (FileSystemFlags) {
            // Intersect with underlying FS capabilities
            *FileSystemFlags &= fsFlags;
            if (!encfs.altStream) {
                *FileSystemFlags &= ~FILE_NAMED_STREAMS;
            }
        }
        DbgPrintV(L"  [INFO] Underlying FS: '%s', flags=0x%08X\n", FileSystemNameBuffer, fsFlags);
    }
    else {
        DbgPrintV(L"  [WARN] GetVolumeInformation for '%s' failed, using NTFS defaults\n", volumeRoot);
        wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, L"NTFS");
    }

    // Set volume serial number
    if (VolumeSerialNumber) {
        if (g_efo.VolumeSerial != 0) {
            // Use custom serial number
            *VolumeSerialNumber = g_efo.VolumeSerial;
        } else {
            // Use underlying volume's serial number
            *VolumeSerialNumber = underlyingSerial;
        }
    }

    DbgPrintV(L"GetVolumeInformation: OK (name='%s', fs='%s', serial=0x%08X)\n", 
             VolumeNameBuffer, FileSystemNameBuffer, VolumeSerialNumber ? *VolumeSerialNumber : 0);
    return STATUS_SUCCESS;
}

/**
 * @brief Retrieves disk space information (free, total, available).
 * @param FreeBytesAvailable Output: bytes available to caller.
 * @param TotalNumberOfBytes Output: total volume size.
 * @param TotalNumberOfFreeBytes Output: total free bytes.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS indicating success or failure.
 */
NTSTATUS DOKAN_CALLBACK EncFSDokanGetDiskFreeSpace(
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

    if (!GetDiskFreeSpaceExW(volumeRoot, &lpFreeBytesAvailable, &lpTotalNumberOfBytes, &lpTotalNumberOfFreeBytes)) {
        DWORD error = GetLastError();
        ErrorPrint(L"GetDiskFreeSpace: FAILED for '%s' (error=%lu)\n", volumeRoot, error);
        return DokanNtStatusFromWin32(error);
    }

    *FreeBytesAvailable = (ULONGLONG)lpFreeBytesAvailable.QuadPart;
    *TotalNumberOfBytes = (ULONGLONG)lpTotalNumberOfBytes.QuadPart;
    *TotalNumberOfFreeBytes = (ULONGLONG)lpTotalNumberOfFreeBytes.QuadPart;

    DbgPrintV(L"GetDiskFreeSpace: total=%I64u, free=%I64u, avail=%I64u\n",
        *TotalNumberOfBytes, *TotalNumberOfFreeBytes, *FreeBytesAvailable);
    return STATUS_SUCCESS;
}

/**
 * @brief Called when the volume is successfully mounted.
 * @param MountPoint The drive letter or mount path.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS (always STATUS_SUCCESS).
 */
NTSTATUS DOKAN_CALLBACK EncFSMounted(LPCWSTR MountPoint, PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);
    InfoPrint(L"Volume mounted at '%s'\n", MountPoint);

    // Open Explorer window at mount point (unless debug mode)
    if (!g_efo.g_DebugMode) {
        wchar_t buff[20];
        if (swprintf_s(buff, 20, L"%s:\\", MountPoint) != -1) {
            DbgPrintV(L"  [INFO] Opening Explorer at '%s'\n", buff);
            ShellExecuteW(NULL, NULL, buff, NULL, NULL, SW_SHOWNORMAL);
        }
    }
    return STATUS_SUCCESS;
}

/**
 * @brief Called when the volume is unmounted.
 * @param DokanFileInfo Dokan file context.
 * @return NTSTATUS (always STATUS_SUCCESS).
 */
NTSTATUS DOKAN_CALLBACK EncFSUnmounted(PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);
    InfoPrint(L"Volume unmounted\n");
    return STATUS_SUCCESS;
}
