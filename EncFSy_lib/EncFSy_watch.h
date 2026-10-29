#pragma once

// dokan.h must come before windows.h (it sets WIN32_NO_STATUS to avoid
// ntstatus.h macro redefinitions).
#include <dokan.h>
#include <windows.h>

/**
 * @file EncFSy_watch.h
 * @brief Backing-store watcher for externally modified encrypted files.
 *
 * Cloud sync clients (Dropbox, Google Drive, OneDrive, ...) modify, replace
 * and delete the encrypted backing files while the volume is mounted. EncFSy
 * caches the file IV per open handle and the Dokan kernel driver caches
 * metadata and decrypted pages, so external changes would otherwise be served
 * from stale caches (or, after a ReplaceFile-style sync update, reads/writes
 * would keep using a handle to the orphaned old file).
 *
 * This module watches the backing root with ReadDirectoryChangesW and, for
 * events not caused by EncFSy itself:
 *  - invalidates the per-handle caches of all open EncFSFile instances of the
 *    affected file (reopening the physical handle when the file was replaced)
 *  - notifies the Dokan kernel driver via DokanNotify* so kernel caches and
 *    directory-change listeners see the external change.
 */

namespace EncFS { class EncFSFile; }

/**
 * @brief Registers an open EncFSFile context under its virtual (plaintext) path.
 *
 * Called from EncFSCreateFile for file (non-directory) contexts. The entry is
 * removed automatically by the EncFSFile destructor.
 */
void EncFSRegisterOpenFile(LPCWSTR virtualPath, EncFS::EncFSFile* file);

/**
 * @brief Removes an EncFSFile from the open-file registry (no-op if absent).
 *
 * Called from the EncFSFile destructor so no code path can leave a dangling
 * pointer in the registry.
 */
void EncFSUnregisterOpenFile(EncFS::EncFSFile* file);

/**
 * @brief Re-keys a registered EncFSFile after a successful rename through the mount.
 *
 * Every other open handle of the same physical file moves to the new name too,
 * as the Dokan FCB does. Handles still open on a file replaced at the new name
 * keep referring to that replaced file (NTFS semantics), so their reopen is
 * disabled: the watcher must not move them to the file that now has the name.
 */
void EncFSRekeyOpenFile(EncFS::EncFSFile* file, LPCWSTR newVirtualPath);

/**
 * @brief Pins the other open handles of a file deleted through the mount.
 *
 * They keep referring to the deleted file, so a file that later gets the same
 * name must not be swapped in by the watcher. Call before the handle is closed.
 */
void EncFSPinDeletedOpenFiles(LPCWSTR virtualPath, EncFS::EncFSFile* file);

/**
 * @brief Records that EncFSy itself just modified the backing file for this
 * virtual path, so the watcher ignores the resulting change events.
 */
void EncFSNoteSelfChange(LPCWSTR virtualPath);

/**
 * @brief Stores the mount point reported by the Mounted callback; used to
 * build the absolute paths required by DokanNotify*.
 */
void EncFSSetNotifyMountPoint(LPCWSTR mountPoint);

/**
 * @brief Starts the backing-store watcher thread for g_efo.RootDirectory.
 * @param dokanInstance Mount instance from DokanCreateFileSystem (may be NULL;
 *        kernel notifications are then skipped but cache invalidation still runs).
 * @return true if the watcher thread was started.
 */
bool EncFSStartBackingStoreWatcher(DOKAN_HANDLE dokanInstance);

/**
 * @brief Stops the watcher thread and clears the registry state.
 */
void EncFSStopBackingStoreWatcher();
