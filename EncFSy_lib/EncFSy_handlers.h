#pragma once
#include <dokan.h>

/**
 * @file EncFSy_handlers.h
 * @brief Dokan filesystem operation callbacks for EncFS
 * 
 * This file declares all Dokan callback functions that implement the encrypted
 * filesystem operations. These callbacks are invoked by the Dokan driver when
 * Windows applications perform file operations on the mounted EncFS volume.
 * 
 * All functions perform encryption/decryption transparently:
 * - File names are encrypted/decrypted using AES-CBC with PKCS#7 padding
 * - File contents are encrypted/decrypted in 1024-byte blocks with MAC
 * - File IVs are stored in the first 8 bytes of each file (if uniqueIV is enabled)
 */

/**
 * @brief Creates or opens a file or directory (ZwCreateFile equivalent)
 * @param FileName Virtual (plaintext) file path
 * @param SecurityContext Security context from kernel
 * @param DesiredAccess Requested access rights (GENERIC_READ, GENERIC_WRITE, etc.)
 * @param FileAttributes File attributes (FILE_ATTRIBUTE_NORMAL, etc.)
 * @param ShareAccess Sharing mode (FILE_SHARE_READ, FILE_SHARE_WRITE, etc.)
 * @param CreateDisposition Creation disposition (CREATE_NEW, OPEN_EXISTING, etc.)
 * @param CreateOptions Create options (FILE_DIRECTORY_FILE, FILE_NON_DIRECTORY_FILE, etc.)
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, STATUS_OBJECT_NAME_COLLISION, etc.)
 * 
 * This is the most complex callback, handling:
 * - Filename encryption/decryption and case-insensitive lookup
 * - Directory vs file creation
 * - User impersonation for security
 * - EncFSFile object creation and context management
 * - Access rights adjustment (read/write operations require both for encryption)
 * - NO_BUFFERING flag removal (encrypted files cannot align to sector size)
 */
NTSTATUS DOKAN_CALLBACK EncFSCreateFile(LPCWSTR, PDOKAN_IO_SECURITY_CONTEXT,
    ACCESS_MASK, ULONG, ULONG, ULONG, ULONG, PDOKAN_FILE_INFO);

/**
 * @brief Performs cleanup operations when the last handle to a file is closed
 * @param FileName Virtual (plaintext) file path
 * @param DokanFileInfo Dokan file context structure
 * 
 * This callback:
 * - Deletes the EncFSFile object and closes the underlying Windows handle
 * - Performs DeleteOnClose operations (file/directory deletion)
 * - Is called before CloseFile when the last handle is closed
 * 
 * Note: Uses dirMoveLock to prevent race conditions with concurrent file operations.
 */
void DOKAN_CALLBACK EncFSCleanup(LPCWSTR, PDOKAN_FILE_INFO);

/**
 * @brief Closes a file handle
 * @param FileName Virtual (plaintext) file path
 * @param DokanFileInfo Dokan file context structure
 * 
 * This callback:
 * - Is called after Cleanup for each handle
 * - Should clean up any remaining resources if Cleanup was not called
 * - Uses dirMoveLock to prevent race conditions
 * 
 * Note: In normal operation, Cleanup should have already freed resources.
 * This is a safety fallback for abnormal termination scenarios.
 */
void DOKAN_CALLBACK EncFSCloseFile(LPCWSTR, PDOKAN_FILE_INFO);

/**
 * @brief Reads decrypted data from an encrypted file
 * @param FileName Virtual (plaintext) file path
 * @param Buffer Output buffer to receive decrypted data
 * @param BufferLength Maximum number of bytes to read
 * @param ReadLength Output parameter receiving actual bytes read
 * @param Offset Logical (decrypted) file offset to read from
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, STATUS_END_OF_FILE, etc.)
 * 
 * This callback:
 * - Calculates which encrypted blocks contain the requested data
 * - Reads and decrypts the necessary blocks
 * - Verifies block MACs for data integrity
 * - Returns only the requested portion of decrypted data
 * - Supports plaintext passthrough for alternate data streams (e.g., Dropbox attributes)
 * - In reverse mode, encrypts plaintext data on-the-fly instead of decrypting
 * 
 * Performance optimization: Caches the last decrypted block to improve sequential reads.
 */
NTSTATUS DOKAN_CALLBACK EncFSReadFile(LPCWSTR, LPVOID, DWORD, LPDWORD,
    LONGLONG, PDOKAN_FILE_INFO);

/**
 * @brief Writes encrypted data to a file
 * @param FileName Virtual (plaintext) file path
 * @param Buffer Input buffer containing plaintext data to encrypt
 * @param NumberOfBytesToWrite Number of bytes to write
 * @param NumberOfBytesWritten Output parameter receiving actual bytes written
 * @param Offset Logical (decrypted) file offset to write to
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * This callback:
 * - Handles WriteToEndOfFile flag for append operations
 * - Performs read-modify-write for partial block updates
 * - Encrypts data in 1024-byte blocks with MAC
 * - Expands file size if writing beyond current end
 * - Handles PagingIo restrictions (cannot write beyond allocated size)
 * - Supports plaintext passthrough for alternate data streams
 * 
 * Note: Write access automatically enables read access because encryption
 * requires reading existing block data for partial updates.
 */
NTSTATUS DOKAN_CALLBACK EncFSWriteFile(LPCWSTR, LPCVOID, DWORD, LPDWORD,
    LONGLONG, PDOKAN_FILE_INFO);

/**
 * @brief Lists files in a directory
 * @param FileName Virtual (plaintext) directory path
 * @param FillFindData Callback function to fill directory entry data
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, STATUS_BUFFER_OVERFLOW, etc.)
 * 
 * This callback:
 * - Enumerates encrypted filenames in the physical directory
 * - Decrypts each filename to its plaintext equivalent
 * - Converts encrypted file sizes to logical (decrypted) sizes
 * - Filters out "." and ".." from root directory listing
 * - Skips files with invalid encryption (failed MAC verification)
 * - In reverse mode, encrypts plaintext filenames instead of decrypting
 * - Keeps .encfs6.xml config file plaintext in reverse mode
 */
NTSTATUS DOKAN_CALLBACK EncFSFindFiles(LPCWSTR, PFillFindData, PDOKAN_FILE_INFO);

/**
 * @brief Moves or renames a file or directory
 * @param FileName Current virtual (plaintext) file path
 * @param NewFileName New virtual (plaintext) file path
 * @param ReplaceIfExisting Whether to replace existing destination file
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, STATUS_OBJECT_NAME_COLLISION, etc.)
 * 
 * This callback handles complex IV management:
 * - For files with externalIVChaining: Updates file IV based on new path
 * - For directories with chainedNameIV or externalIVChaining:
 *   - Recursively renames all encrypted filenames in the directory tree
 *   - Updates file IVs for all files in the tree
 *   - Uses dirMoveLock to ensure single-threaded execution
 * 
 * Note: Directory moves can be slow for large directory trees because
 * all files must be individually renamed and updated.
 */
NTSTATUS DOKAN_CALLBACK EncFSMoveFile(LPCWSTR, LPCWSTR, BOOL, PDOKAN_FILE_INFO);

/**
 * @brief Marks a file for deletion
 * @param FileName Virtual (plaintext) file path
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, STATUS_ACCESS_DENIED, etc.)
 * 
 * This callback:
 * - Sets the DeleteOnClose flag if the file can be deleted
 * - Prevents deletion of directories (returns STATUS_ACCESS_DENIED)
 * - Actual deletion occurs in Cleanup callback
 */
NTSTATUS DOKAN_CALLBACK EncFSDeleteFile(LPCWSTR, PDOKAN_FILE_INFO);

/**
 * @brief Marks a directory for deletion
 * @param FileName Virtual (plaintext) directory path
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, STATUS_DIRECTORY_NOT_EMPTY, etc.)
 * 
 * This callback:
 * - Verifies the directory is empty (only "." and ".." entries)
 * - Sets DeleteOnClose flag if directory can be deleted
 * - Actual deletion occurs in Cleanup callback
 */
NTSTATUS DOKAN_CALLBACK EncFSDeleteDirectory(LPCWSTR, PDOKAN_FILE_INFO);

/**
 * @brief Flushes file buffers to disk
 * @param FileName Virtual (plaintext) file path
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * Ensures all buffered encrypted data is written to the physical disk.
 */
NTSTATUS DOKAN_CALLBACK EncFSFlushFileBuffers(LPCWSTR, PDOKAN_FILE_INFO);

/**
 * @brief Sets the logical (decrypted) end-of-file position
 * @param FileName Virtual (plaintext) file path
 * @param ByteOffset New logical file size in bytes
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * This callback:
 * - Truncates or extends the file to the specified logical size
 * - Handles block boundary adjustments for encryption
 * - Re-encrypts boundary blocks with adjusted padding
 * - Enables sparse file support for large files (>= 100MB)
 */
NTSTATUS DOKAN_CALLBACK EncFSSetEndOfFile(LPCWSTR, LONGLONG, PDOKAN_FILE_INFO);

/**
 * @brief Sets the file allocation size (pre-allocates disk space)
 * @param FileName Virtual (plaintext) file path
 * @param AllocSize New allocation size in logical (decrypted) bytes
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * This callback:
 * - Only shrinks the file if AllocSize is less than current size
 * - Delegates to SetEndOfFile for actual truncation
 * - Windows may call this before writes to pre-allocate space
 */
NTSTATUS DOKAN_CALLBACK EncFSSetAllocationSize(LPCWSTR, LONGLONG, PDOKAN_FILE_INFO);

/**
 * @brief Sets file attributes (hidden, system, read-only, etc.)
 * @param FileName Virtual (plaintext) file path
 * @param FileAttributes New file attributes bitmask
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * Note: FileAttributes=0 means "do not change" per MS-FSCC specification.
 */
NTSTATUS DOKAN_CALLBACK EncFSSetFileAttributes(LPCWSTR, DWORD, PDOKAN_FILE_INFO);

/**
 * @brief Sets file timestamps (creation, last access, last write)
 * @param FileName Virtual (plaintext) file path
 * @param CreationTime New creation time (NULL = do not change)
 * @param LastAccessTime New last access time (NULL = do not change)
 * @param LastWriteTime New last write time (NULL = do not change)
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 */
NTSTATUS DOKAN_CALLBACK EncFSSetFileTime(LPCWSTR, const FILETIME*, const FILETIME*, const FILETIME*, PDOKAN_FILE_INFO);

/**
 * @brief Gets file information (size, attributes, timestamps)
 * @param FileName Virtual (plaintext) file path
 * @param HandleFileInformation Output parameter receiving file information
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * This callback:
 * - Retrieves file metadata from the physical encrypted file
 * - Converts encrypted file size to logical (decrypted) size
 * - Uses FindFirstFile as fallback for root directory and invalid handles
 * - In reverse mode, converts logical size to encrypted size
 */
NTSTATUS DOKAN_CALLBACK EncFSGetFileInformation(LPCWSTR, LPBY_HANDLE_FILE_INFORMATION, PDOKAN_FILE_INFO);

/**
 * @brief Locks a byte range in a file (for file locking coordination)
 * @param FileName Virtual (plaintext) file path
 * @param ByteOffset Starting byte offset of the lock
 * @param Length Number of bytes to lock
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * Note: Locks are advisory and use logical (decrypted) byte offsets.
 */
NTSTATUS DOKAN_CALLBACK EncFSLockFile(LPCWSTR, LONGLONG, LONGLONG, PDOKAN_FILE_INFO);

/**
 * @brief Unlocks a previously locked byte range
 * @param FileName Virtual (plaintext) file path
 * @param ByteOffset Starting byte offset of the lock
 * @param Length Number of bytes to unlock
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 */
NTSTATUS DOKAN_CALLBACK EncFSUnlockFile(LPCWSTR, LONGLONG, LONGLONG, PDOKAN_FILE_INFO);

/**
 * @brief Gets file security descriptor (ACL, owner, group)
 * @param FileName Virtual (plaintext) file path
 * @param SecurityInformation Which security info to retrieve (OWNER, DACL, SACL, etc.)
 * @param SecurityDescriptor Output buffer for security descriptor
 * @param BufferLength Size of output buffer
 * @param LengthNeeded Output parameter receiving required buffer size
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, STATUS_BUFFER_OVERFLOW, etc.)
 * 
 * This callback:
 * - Opens a new handle with READ_CONTROL access
 * - Optionally requests ACCESS_SYSTEM_SECURITY for SACL if SE_SECURITY_NAME privilege is held
 * - Retrieves security descriptor from the physical encrypted file
 * 
 * Note: Security is applied to the encrypted physical file, not per-user encryption.
 */
NTSTATUS DOKAN_CALLBACK EncFSGetFileSecurity(LPCWSTR, PSECURITY_INFORMATION,
    PSECURITY_DESCRIPTOR, ULONG, PULONG, PDOKAN_FILE_INFO);

/**
 * @brief Sets file security descriptor (ACL, owner, group)
 * @param FileName Virtual (plaintext) file path
 * @param SecurityInformation Which security info to set (OWNER, DACL, SACL, etc.)
 * @param SecurityDescriptor Input security descriptor
 * @param SecurityDescriptorLength Size of security descriptor
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, STATUS_BUFFER_OVERFLOW, etc.)
 * 
 * Applies the security descriptor to the physical encrypted file.
 */
NTSTATUS DOKAN_CALLBACK EncFSSetFileSecurity(LPCWSTR, PSECURITY_INFORMATION,
    PSECURITY_DESCRIPTOR, ULONG, PDOKAN_FILE_INFO);

/**
 * @brief Gets disk free space information
 * @param FreeBytesAvailable Output parameter for available bytes to caller
 * @param TotalNumberOfBytes Output parameter for total volume size
 * @param TotalNumberOfFreeBytes Output parameter for total free bytes
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * Reports the underlying physical drive's free space (not adjusted for encryption overhead).
 */
NTSTATUS DOKAN_CALLBACK EncFSDokanGetDiskFreeSpace(PULONGLONG, PULONGLONG, PULONGLONG, PDOKAN_FILE_INFO);

/**
 * @brief Gets volume information (name, serial number, filesystem type)
 * @param VolumeNameBuffer Output buffer for volume name
 * @param VolumeNameSize Size of volume name buffer
 * @param VolumeSerialNumber Output parameter for volume serial number
 * @param MaximumComponentLength Output parameter for max filename length
 * @param FileSystemFlags Output parameter for filesystem feature flags
 * @param FileSystemNameBuffer Output buffer for filesystem name
 * @param FileSystemNameSize Size of filesystem name buffer
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * This callback:
 * - Returns "EncFS" as the volume name
 * - Queries the underlying drive's filesystem type (NTFS, FAT, etc.)
 * - Enables FILE_NAMED_STREAMS flag if altStream support is enabled
 * - Always enables FILE_CASE_SENSITIVE_SEARCH for EncFS behavior
 */
NTSTATUS DOKAN_CALLBACK EncFSGetVolumeInformation(LPWSTR, DWORD, LPDWORD, LPDWORD, LPDWORD, LPWSTR, DWORD, PDOKAN_FILE_INFO);

/**
 * @brief Called when the volume is successfully mounted
 * @param MountPoint Drive letter or mount point path
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * This callback:
 * - Logs the successful mount
 * - Optionally opens Explorer to the mounted drive (if not in debug mode)
 */
NTSTATUS DOKAN_CALLBACK EncFSMounted(LPCWSTR, PDOKAN_FILE_INFO);

/**
 * @brief Called when the volume is unmounted
 * @param DokanFileInfo Dokan file context structure
 * @return NTSTATUS code (STATUS_SUCCESS, etc.)
 * 
 * Logs the unmount event for debugging.
 */
NTSTATUS DOKAN_CALLBACK EncFSUnmounted(PDOKAN_FILE_INFO);
