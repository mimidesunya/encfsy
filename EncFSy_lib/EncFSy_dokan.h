#pragma once

#include <dokan.h>

// Dokan operation callbacks
NTSTATUS DOKAN_CALLBACK
EncFSCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
                ACCESS_MASK DesiredAccess, ULONG FileAttributes,
                ULONG ShareAccess, ULONG CreateDisposition,
                ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo);

void DOKAN_CALLBACK EncFSCloseFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo);

void DOKAN_CALLBACK EncFSCleanup(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSReadFile(LPCWSTR FileName, LPVOID Buffer,
              DWORD BufferLength, LPDWORD ReadLength,
              LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSWriteFile(LPCWSTR FileName, LPCVOID Buffer,
               DWORD NumberOfBytesToWrite, LPDWORD NumberOfBytesWritten,
               LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSGetFileInformation(LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
                        PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSFindFiles(LPCWSTR FileName,
               PFillFindData FillFindData, // function pointer
               PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSMoveFile(LPCWSTR FileName, LPCWSTR NewFileName, BOOL ReplaceIfExisting,
              PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSSetEndOfFile(LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSSetAllocationSize(LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSLockFile(LPCWSTR FileName,
              LONGLONG ByteOffset, LONGLONG Length, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSUnlockFile(LPCWSTR FileName, LONGLONG ByteOffset, LONGLONG Length,
                PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSGetFileSecurity(LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
                     PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
                     PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSSetFileSecurity(LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
                     PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG SecurityDescriptorLength,
                     PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSGetVolumeInformation(LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
                          LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
                          LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
                          PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSDokanGetDiskFreeSpace(PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes,
                           PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSFindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
                 PVOID FindStreamContext, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSSetFileAttributes(LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK
EncFSSetFileTime(LPCWSTR FileName, CONST FILETIME* CreationTime,
                 CONST FILETIME* LastAccessTime, CONST FILETIME* LastWriteTime,
                 PDOKAN_FILE_INFO DokanFileInfo);

NTSTATUS DOKAN_CALLBACK EncFSMounted(LPCWSTR MountPoint, PDOKAN_FILE_INFO DokanFileInfo);
NTSTATUS DOKAN_CALLBACK EncFSUnmounted(PDOKAN_FILE_INFO DokanFileInfo);
