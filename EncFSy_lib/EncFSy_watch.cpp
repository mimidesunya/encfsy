/**
 * @file EncFSy_watch.cpp
 * @brief Backing-store watcher implementation (see EncFSy_watch.h).
 */

#include "EncFSy_watch.h"

#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "EncFSFile.h"
#include "EncFSy_globals.h"
#include "EncFSy_logging.h"

using namespace std;

namespace {

    // How long a self-initiated change suppresses kernel notifications for the
    // same virtual path. Only DokanNotify* is suppressed (the kernel already
    // notifies directory watchers about operations made through the volume);
    // per-handle cache reconciliation always runs because its identity check
    // makes self-caused events harmless.
    const ULONGLONG SELF_CHANGE_SUPPRESS_MS = 5 * 1000;

    // Prune threshold for the self-change map.
    const size_t SELF_CHANGE_PRUNE_SIZE = 4096;

    //=========================================================================
    // Open-file registry
    //=========================================================================

    mutex g_registryLock;
    // file -> virtual path as registered (original case, for diagnostics/reopen)
    unordered_map<EncFS::EncFSFile*, wstring> g_fileToPath;
    // normalized virtual path -> files (multiple handles may be open)
    unordered_multimap<wstring, EncFS::EncFSFile*> g_pathToFiles;

    //=========================================================================
    // Self-change tracker
    //=========================================================================

    mutex g_selfChangeLock;
    unordered_map<wstring, ULONGLONG> g_selfChanges;

    //=========================================================================
    // Watcher state
    //=========================================================================

    DOKAN_HANDLE g_dokanInstance = nullptr;
    HANDLE g_stopEvent = NULL;
    thread g_watcherThread;
    bool g_watcherRunning = false;

    mutex g_mountPointLock;
    wstring g_notifyMountPoint;

    //=========================================================================
    // Helpers
    //=========================================================================

    bool WatchWideToUtf8(const wstring& wstr, string& out) {
        if (wstr.empty()) {
            out.clear();
            return true;
        }
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
        if (size <= 0) return false;
        out.resize(size);
        return WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &out[0], size, NULL, NULL) > 0;
    }

    bool WatchUtf8ToWide(const string& str, wstring& out) {
        if (str.empty()) {
            out.clear();
            return true;
        }
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
        if (size <= 0) return false;
        out.resize(size);
        return MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], size) > 0;
    }

    // Normalizes a virtual path for use as a registry / self-change key:
    // strips an alternate-data-stream suffix from the leaf and upper-cases
    // the path when the volume is mounted case-insensitively.
    wstring NormalizeVirtualPath(LPCWSTR virtualPath) {
        wstring key(virtualPath);
        size_t lastSep = key.find_last_of(L'\\');
        size_t colon = key.find(L':', lastSep == wstring::npos ? 0 : lastSep + 1);
        if (colon != wstring::npos) {
            key.resize(colon);
        }
        if (g_efo.CaseInsensitive && !key.empty()) {
            CharUpperBuffW(&key[0], (DWORD)key.size());
        }
        return key;
    }

    bool IsRecentSelfChange(const wstring& key) {
        lock_guard<mutex> lock(g_selfChangeLock);
        auto it = g_selfChanges.find(key);
        if (it == g_selfChanges.end()) {
            return false;
        }
        return (GetTickCount64() - it->second) < SELF_CHANGE_SUPPRESS_MS;
    }

    // Maps a backing-store relative path (as reported by ReadDirectoryChangesW)
    // to the virtual path visible on the mounted volume. In normal mode the
    // backing names are encrypted and get decoded; in reverse mode the backing
    // store holds plaintext and the virtual names are the encoded ones.
    bool BackingToVirtualPath(const wstring& relPath, wstring& virtualPath) {
        string cBacking;
        if (!WatchWideToUtf8(L"\\" + relPath, cBacking)) {
            return false;
        }
        string cVirtual;
        try {
            if (g_efo.Reverse) {
                encfs.encodeFilePath(cBacking, cVirtual);
            }
            else {
                encfs.decodeFilePath(cBacking, cVirtual);
            }
        }
        catch (...) {
            // Not an EncFS name (e.g. desktop.ini, sync temp files) - ignore.
            return false;
        }
        return WatchUtf8ToWide(cVirtual, virtualPath);
    }

    // Builds the absolute physical path of a backing-store relative path,
    // with the \\?\ long-path prefix for non-UNC roots.
    wstring BuildPhysicalPath(const wstring& relPath) {
        wstring root(g_efo.RootDirectory);
        while (!root.empty() && root.back() == L'\\') {
            root.pop_back();
        }
        if (root.compare(0, 2, L"\\\\") == 0) {
            return root + L"\\" + relPath;
        }
        return L"\\\\?\\" + root + L"\\" + relPath;
    }

    // Mount point in "X:" form (or mount-directory path without trailing
    // backslash), ready to be concatenated with a leading-backslash virtual path.
    wstring GetNotifyMountPoint() {
        wstring mount;
        {
            lock_guard<mutex> lock(g_mountPointLock);
            mount = g_notifyMountPoint;
        }
        if (mount.empty()) {
            mount = g_efo.MountPoint;
        }
        while (!mount.empty() && mount.back() == L'\\') {
            mount.pop_back();
        }
        if (mount.size() == 1 && iswalpha(mount[0])) {
            mount += L':';
        }
        return mount;
    }

    void NotifyKernel(DWORD action, const wstring& relPath, const wstring& virtualPath) {
        if (!g_dokanInstance) {
            return;
        }
        wstring mount = GetNotifyMountPoint();
        if (mount.empty()) {
            return;
        }
        wstring absPath = mount + virtualPath;

        switch (action) {
        case FILE_ACTION_ADDED:
        case FILE_ACTION_RENAMED_NEW_NAME: {
            DWORD attr = GetFileAttributesW(BuildPhysicalPath(relPath).c_str());
            BOOL isDir = (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
            DokanNotifyCreate(g_dokanInstance, absPath.c_str(), isDir);
            break;
        }
        case FILE_ACTION_REMOVED:
        case FILE_ACTION_RENAMED_OLD_NAME:
            // The directory flag of an already-deleted entry is unknown; FALSE
            // only affects the notification filter class, not cache invalidation.
            DokanNotifyDelete(g_dokanInstance, absPath.c_str(), FALSE);
            break;
        case FILE_ACTION_MODIFIED:
        default:
            DokanNotifyUpdate(g_dokanInstance, absPath.c_str());
            break;
        }
    }

    void ProcessChange(DWORD action, const wstring& relPath) {
        if (relPath.empty() || relPath == L".encfs6.xml") {
            return;
        }
        // Alternate data stream notifications carry a ':' in the leaf; the
        // main-stream event for the same file arrives separately.
        if (relPath.find(L':') != wstring::npos) {
            return;
        }

        wstring virtualPath;
        if (!BackingToVirtualPath(relPath, virtualPath)) {
            return;
        }
        wstring key = NormalizeVirtualPath(virtualPath.c_str());

        DbgPrintV(L"Watcher: change action=%lu '%s' -> '%s'\n",
            action, relPath.c_str(), virtualPath.c_str());

        // Reconcile open handles unconditionally - never time-filtered. A save
        // through the mount followed within seconds by a sync update of the
        // same file (a classic conflict) must still swap handles; for events
        // caused by our own writes the identity check inside onExternalChange
        // detects the unchanged file and the cache reset is harmless.
        // Holding the registry lock keeps the EncFSFile objects alive: their
        // destructor unregisters and thus blocks on the same lock until we
        // are done.
        {
            lock_guard<mutex> lock(g_registryLock);
            auto range = g_pathToFiles.equal_range(key);
            if (range.first != range.second) {
                wstring physicalPath = BuildPhysicalPath(relPath);
                for (auto it = range.first; it != range.second; ++it) {
                    it->second->onExternalChange(physicalPath.c_str());
                }
            }
        }

        // Kernel notification is pure duplicate noise for our own operations
        // (Dokan already reported them), so only self-events are filtered here.
        if (!IsRecentSelfChange(key)) {
            NotifyKernel(action, relPath, virtualPath);
        }
    }

    // Buffer overflow: the change list was lost, so conservatively drop every
    // per-handle cache. Kernel notification is skipped (no paths available).
    void InvalidateAllOpenFiles() {
        DbgPrintV(L"Watcher: change buffer overflow, invalidating all open files\n");
        lock_guard<mutex> lock(g_registryLock);
        for (auto& entry : g_fileToPath) {
            entry.first->onExternalChange(nullptr);
        }
    }

    void WatcherThreadProc() {
        wstring root(g_efo.RootDirectory);
        HANDLE hDir = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
        if (hDir == INVALID_HANDLE_VALUE) {
            ErrorPrint(L"Watcher: cannot open root directory '%s' (error=%lu)\n",
                root.c_str(), GetLastError());
            return;
        }

        OVERLAPPED ovl = {};
        ovl.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ovl.hEvent) {
            CloseHandle(hDir);
            return;
        }

        // 64KB keeps ReadDirectoryChangesW working even if the root ever
        // resides on a network share; overflow is handled conservatively.
        vector<BYTE> buffer(64 * 1024);
        const DWORD filter =
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

        for (;;) {
            if (!ReadDirectoryChangesW(hDir, buffer.data(), (DWORD)buffer.size(),
                TRUE, filter, NULL, &ovl, NULL)) {
                ErrorPrint(L"Watcher: ReadDirectoryChangesW failed (error=%lu)\n", GetLastError());
                break;
            }

            HANDLE waits[2] = { g_stopEvent, ovl.hEvent };
            DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0 + 1) {
                CancelIoEx(hDir, &ovl);
                DWORD ignored;
                GetOverlappedResult(hDir, &ovl, &ignored, TRUE);
                break;
            }

            DWORD bytes = 0;
            if (!GetOverlappedResult(hDir, &ovl, &bytes, FALSE)) {
                ErrorPrint(L"Watcher: GetOverlappedResult failed (error=%lu)\n", GetLastError());
                break;
            }
            ResetEvent(ovl.hEvent);

            if (bytes == 0) {
                InvalidateAllOpenFiles();
                continue;
            }

            size_t offset = 0;
            for (;;) {
                const FILE_NOTIFY_INFORMATION* fni =
                    reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
                wstring relPath(fni->FileName, fni->FileNameLength / sizeof(WCHAR));
                ProcessChange(fni->Action, relPath);
                if (fni->NextEntryOffset == 0) {
                    break;
                }
                offset += fni->NextEntryOffset;
            }
        }

        CloseHandle(ovl.hEvent);
        CloseHandle(hDir);
    }

} // namespace

//=============================================================================
// Public API
//=============================================================================

void EncFSRegisterOpenFile(LPCWSTR virtualPath, EncFS::EncFSFile* file) {
    if (!virtualPath || !file) {
        return;
    }
    wstring key = NormalizeVirtualPath(virtualPath);
    lock_guard<mutex> lock(g_registryLock);
    g_fileToPath[file] = virtualPath;
    g_pathToFiles.emplace(std::move(key), file);
}

void EncFSUnregisterOpenFile(EncFS::EncFSFile* file) {
    if (!file) {
        return;
    }
    lock_guard<mutex> lock(g_registryLock);
    auto it = g_fileToPath.find(file);
    if (it == g_fileToPath.end()) {
        return;
    }
    wstring key = NormalizeVirtualPath(it->second.c_str());
    auto range = g_pathToFiles.equal_range(key);
    for (auto pit = range.first; pit != range.second; ++pit) {
        if (pit->second == file) {
            g_pathToFiles.erase(pit);
            break;
        }
    }
    g_fileToPath.erase(it);
}

void EncFSRekeyOpenFile(EncFS::EncFSFile* file, LPCWSTR newVirtualPath) {
    if (!file || !newVirtualPath) {
        return;
    }
    lock_guard<mutex> lock(g_registryLock);
    auto it = g_fileToPath.find(file);
    if (it == g_fileToPath.end()) {
        return;
    }
    wstring oldKey = NormalizeVirtualPath(it->second.c_str());
    auto range = g_pathToFiles.equal_range(oldKey);
    for (auto pit = range.first; pit != range.second; ++pit) {
        if (pit->second == file) {
            g_pathToFiles.erase(pit);
            break;
        }
    }
    it->second = newVirtualPath;
    g_pathToFiles.emplace(NormalizeVirtualPath(newVirtualPath), file);
}

void EncFSNoteSelfChange(LPCWSTR virtualPath) {
    if (!virtualPath) {
        return;
    }
    wstring key = NormalizeVirtualPath(virtualPath);
    ULONGLONG now = GetTickCount64();
    lock_guard<mutex> lock(g_selfChangeLock);
    if (g_selfChanges.size() > SELF_CHANGE_PRUNE_SIZE) {
        for (auto it = g_selfChanges.begin(); it != g_selfChanges.end();) {
            if (now - it->second >= SELF_CHANGE_SUPPRESS_MS) {
                it = g_selfChanges.erase(it);
            }
            else {
                ++it;
            }
        }
    }
    g_selfChanges[std::move(key)] = now;
}

void EncFSSetNotifyMountPoint(LPCWSTR mountPoint) {
    if (!mountPoint) {
        return;
    }
    lock_guard<mutex> lock(g_mountPointLock);
    g_notifyMountPoint = mountPoint;
}

bool EncFSStartBackingStoreWatcher(DOKAN_HANDLE dokanInstance) {
    if (g_watcherRunning) {
        return true;
    }
    g_dokanInstance = dokanInstance;
    g_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_stopEvent) {
        return false;
    }
    g_watcherThread = thread(WatcherThreadProc);
    g_watcherRunning = true;
    InfoPrint(L"Backing-store watcher started for '%s'\n", g_efo.RootDirectory);
    return true;
}

void EncFSStopBackingStoreWatcher() {
    if (!g_watcherRunning) {
        return;
    }
    SetEvent(g_stopEvent);
    if (g_watcherThread.joinable()) {
        g_watcherThread.join();
    }
    CloseHandle(g_stopEvent);
    g_stopEvent = NULL;
    g_dokanInstance = nullptr;
    g_watcherRunning = false;

    {
        lock_guard<mutex> lock(g_selfChangeLock);
        g_selfChanges.clear();
    }
    InfoPrint(L"Backing-store watcher stopped\n");
}
