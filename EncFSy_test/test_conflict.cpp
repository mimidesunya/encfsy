// test_conflict.cpp - Cloud sync conflict handling tests
#include "test_common.h"
#include "test_declarations.h"

#include <set>

namespace
{
    const WCHAR* kDefaultEncryptedRoot = L"F:\\work\\encfs\\";
    const WCHAR* kEnvEncryptedRoot = L"ENCFS_ENCRYPTED_ROOT";
    const WCHAR* kPlainBaseName = L"conflict_test.txt";
    const WCHAR* kConflictSuffix = L" (DESKTOP-QRI9EIH conflict 2023-04-02)";
    const char kTestContent[] = "conflict-test-content";

    std::wstring BuildPath(const std::wstring& dir, const std::wstring& leaf)
    {
        if (dir.empty()) {
            return leaf;
        }
        std::wstring result(dir);
        if (result.back() != L'\\') {
            result.push_back(L'\\');
        }
        result.append(leaf);
        return result;
    }

    std::wstring BuildPath(const std::wstring& dir, const WCHAR* leaf)
    {
        return BuildPath(dir, std::wstring(leaf));
    }

    std::wstring BuildPlainConflictName()
    {
        std::wstring base(kPlainBaseName);
        size_t dotPos = base.find_last_of(L'.');
        if (dotPos == std::wstring::npos) {
            return base + kConflictSuffix;
        }
        return base.substr(0, dotPos) + kConflictSuffix + base.substr(dotPos);
    }

    std::wstring ResolveEncryptedRoot()
    {
        WCHAR buffer[MAX_PATH];
        DWORD len = GetEnvironmentVariableW(kEnvEncryptedRoot, buffer, static_cast<DWORD>(_countof(buffer)));
        if (len > 0 && len < _countof(buffer)) {
            std::wstring value(buffer, len);
            if (!value.empty() && value.back() != L'\\') {
                value.push_back(L'\\');
            }
            return value;
        }
        return kDefaultEncryptedRoot;
    }

    bool IsRawFilesystemRun(const WCHAR* rootDir)
    {
        return (rootDir != nullptr) && (wcsstr(rootDir, L"encfs_raw") != nullptr);
    }

    bool EnumerateFiles(const std::wstring& dir, std::set<std::wstring>& files)
    {
        files.clear();
        std::wstring search = dir;
        if (search.empty()) return false;
        if (search.back() != L'\\') search.push_back(L'\\');
        search.append(L"*");

        WIN32_FIND_DATAW data{};
        HANDLE hFind = FindFirstFileW(search.c_str(), &data);
        if (hFind == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
            return false;
        }
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && data.cFileName[0] != L'.') {
                files.insert(data.cFileName);
            }
        } while (FindNextFileW(hFind, &data));
        FindClose(hFind);
        return true;
    }

    std::wstring FindNewFile(const std::wstring& dir, const std::set<std::wstring>& baseline)
    {
        for (int i = 0; i < 100; ++i) {
            std::set<std::wstring> current;
            if (EnumerateFiles(dir, current)) {
                for (const auto& f : current) {
                    if (baseline.count(f) == 0) return f;
                }
            }
            Sleep(50);
        }
        return L"";
    }

    struct ConflictSetup
    {
        std::wstring plainRoot;
        std::wstring encryptedRoot;
        std::wstring plainBasePath;
        std::wstring encBasePath;
        std::wstring encConflictPath;
        std::wstring actualConflictName;
        std::wstring actualConflictPath;
    };

    bool SetupConflictScenario(const WCHAR* rootDir, ConflictSetup& setup)
    {
        if (rootDir == nullptr || rootDir[0] == L'\0') {
            printf("Conflict tests require a valid EncFS mount point.\n");
            return false;
        }
        if (IsRawFilesystemRun(rootDir)) {
            printf("Conflict tests cannot run with -r. Re-run without -r.\n");
            return false;
        }

        setup.plainRoot = rootDir;
        setup.encryptedRoot = ResolveEncryptedRoot();
        setup.plainBasePath = BuildPath(setup.plainRoot, kPlainBaseName);

        // Cleanup any leftovers
        DeleteFileW(setup.plainBasePath.c_str());

        // Snapshot encrypted directory
        std::set<std::wstring> baseline;
        EnumerateFiles(setup.encryptedRoot, baseline);

        // Create plain file with known content
        HANDLE h = CreateFileW(setup.plainBasePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            PrintLastError("CreateFileW (plain)");
            return false;
        }
        DWORD written;
        WriteFile(h, kTestContent, sizeof(kTestContent) - 1, &written, nullptr);
        CloseHandle(h);

        // Find encrypted backing file
        std::wstring encName = FindNewFile(setup.encryptedRoot, baseline);
        if (encName.empty()) {
            printf("Failed to find encrypted backing file.\n");
            DeleteFileW(setup.plainBasePath.c_str());
            return false;
        }

        setup.encBasePath = BuildPath(setup.encryptedRoot, encName);
        setup.encConflictPath = setup.encBasePath + kConflictSuffix;

        // Remove old conflict if exists
        DeleteFileW(setup.encConflictPath.c_str());

        // Copy encrypted file with conflict suffix
        if (!CopyFileW(setup.encBasePath.c_str(), setup.encConflictPath.c_str(), FALSE)) {
            PrintLastError("CopyFileW");
            DeleteFileW(setup.plainBasePath.c_str());
            return false;
        }

        Sleep(200);

        // Find actual conflict file on mount
        std::set<std::wstring> mountedFiles;
        EnumerateFiles(setup.plainRoot, mountedFiles);
        
        for (const auto& f : mountedFiles) {
            if (f.find(L"conflict_test") != std::wstring::npos && 
                f.find(L"conflict") != std::wstring::npos && 
                f != kPlainBaseName) {
                setup.actualConflictName = f;
                setup.actualConflictPath = BuildPath(setup.plainRoot, f);
                break;
            }
        }

        return !setup.actualConflictName.empty();
    }

    void CleanupConflict(const ConflictSetup& setup)
    {
        if (!setup.actualConflictPath.empty()) {
            DeleteFileW(setup.actualConflictPath.c_str());
        }
        DeleteFileW(setup.plainBasePath.c_str());
        DeleteFileW(setup.encConflictPath.c_str());
    }
}

// Test 1: Verify conflict file is visible and readable on mount
bool Test_ConflictShadowCopy(const WCHAR* rootDir)
{
    ConflictSetup setup;
    if (!SetupConflictScenario(rootDir, setup)) {
        return false;
    }

    printf("Expected: %S\n", BuildPlainConflictName().c_str());
    printf("Actual:   %S\n", setup.actualConflictName.c_str());
    printf("Conflict file visible: YES\n");

    // Read content from conflict file
    HANDLE h = CreateFileW(setup.actualConflictPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW (read conflict)");
        CleanupConflict(setup);
        return false;
    }

    char buffer[256] = {};
    DWORD bytesRead = 0;
    BOOL readOk = ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
    CloseHandle(h);

    if (!readOk) {
        PrintLastError("ReadFile (conflict)");
        CleanupConflict(setup);
        return false;
    }

    // Verify content matches
    bool contentMatch = (bytesRead == sizeof(kTestContent) - 1) &&
                        (memcmp(buffer, kTestContent, bytesRead) == 0);
    
    printf("Content read: %zu bytes\n", static_cast<size_t>(bytesRead));
    printf("Content match: %s\n", contentMatch ? "YES" : "NO");

    CleanupConflict(setup);
    return contentMatch;
}

// Test 2: Rename conflict file to replace base file
bool Test_ConflictAutoMerge(const WCHAR* rootDir)
{
    ConflictSetup setup;
    if (!SetupConflictScenario(rootDir, setup)) {
        return false;
    }

    printf("Conflict file: %S\n", setup.actualConflictPath.c_str());
    printf("Target file:   %S\n", setup.plainBasePath.c_str());

    // Rename conflict file to base file (replace)
    BOOL renamed = MoveFileExW(setup.actualConflictPath.c_str(),
                               setup.plainBasePath.c_str(),
                               MOVEFILE_REPLACE_EXISTING);
    if (!renamed) {
        PrintLastError("MoveFileExW (rename)");
        CleanupConflict(setup);
        return false;
    }

    printf("Rename: OK\n");

    // Verify conflict file no longer exists
    bool conflictGone = !PathExists(setup.actualConflictPath.c_str());
    printf("Conflict file removed: %s\n", conflictGone ? "YES" : "NO");

    // Verify base file still exists
    bool baseExists = PathExists(setup.plainBasePath.c_str());
    printf("Base file exists: %s\n", baseExists ? "YES" : "NO");

    // Cleanup
    DeleteFileW(setup.plainBasePath.c_str());
    DeleteFileW(setup.encConflictPath.c_str());

    return conflictGone && baseExists;
}

// Test 3: Delete conflict file
bool Test_ConflictManualResolution(const WCHAR* rootDir)
{
    ConflictSetup setup;
    if (!SetupConflictScenario(rootDir, setup)) {
        return false;
    }

    printf("Deleting conflict file: %S\n", setup.actualConflictPath.c_str());

    // Delete conflict file from mount
    BOOL deleted = DeleteFileW(setup.actualConflictPath.c_str());
    if (!deleted) {
        PrintLastError("DeleteFileW (conflict)");
        CleanupConflict(setup);
        return false;
    }

    printf("Delete: OK\n");

    // Verify conflict file no longer exists on mount
    bool plainGone = !PathExists(setup.actualConflictPath.c_str());
    printf("Plain conflict removed: %s\n", plainGone ? "YES" : "NO");

    // Verify encrypted conflict file no longer exists
    bool encGone = !PathExists(setup.encConflictPath.c_str());
    printf("Encrypted conflict removed: %s\n", encGone ? "YES" : "NO");

    // Verify base file still exists
    bool baseExists = PathExists(setup.plainBasePath.c_str());
    printf("Base file preserved: %s\n", baseExists ? "YES" : "NO");

    // Cleanup
    DeleteFileW(setup.plainBasePath.c_str());

    return plainGone && encGone && baseExists;
}

// Test 4: Copy conflict file to a new location
bool Test_ConflictCopy(const WCHAR* rootDir)
{
    ConflictSetup setup;
    if (!SetupConflictScenario(rootDir, setup)) {
        return false;
    }

    // Build destination path
    std::wstring destName = L"conflict_copy.txt";
    std::wstring destPath = BuildPath(setup.plainRoot, destName);
    
    // Cleanup destination if exists
    DeleteFileW(destPath.c_str());

    printf("Copying conflict file: %S\n", setup.actualConflictPath.c_str());
    printf("Destination: %S\n", destPath.c_str());

    // Copy conflict file to new location
    BOOL copied = CopyFileW(setup.actualConflictPath.c_str(), destPath.c_str(), FALSE);
    if (!copied) {
        PrintLastError("CopyFileW (conflict)");
        CleanupConflict(setup);
        return false;
    }

    printf("Copy: OK\n");

    // Verify destination file exists
    bool destExists = PathExists(destPath.c_str());
    printf("Destination exists: %s\n", destExists ? "YES" : "NO");

    // Verify source conflict file still exists
    bool srcExists = PathExists(setup.actualConflictPath.c_str());
    printf("Source preserved: %s\n", srcExists ? "YES" : "NO");

    // Read content from copied file and verify
    bool contentMatch = false;
    HANDLE h = CreateFileW(destPath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buffer[256] = {};
        DWORD bytesRead = 0;
        if (ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
            contentMatch = (bytesRead == sizeof(kTestContent) - 1) &&
                           (memcmp(buffer, kTestContent, bytesRead) == 0);
        }
        CloseHandle(h);
    }
    printf("Content match: %s\n", contentMatch ? "YES" : "NO");

    // Cleanup
    DeleteFileW(destPath.c_str());
    CleanupConflict(setup);

    return destExists && srcExists && contentMatch;
}

// Test 5: Create a file with conflict-like name normally (not a real conflict)
// This tests that files with parentheses in their names work correctly
bool Test_ConflictLikeNormalFile(const WCHAR* rootDir)
{
    if (rootDir == nullptr || rootDir[0] == L'\0') {
        printf("Test requires a valid mount point.\n");
        return false;
    }
    if (IsRawFilesystemRun(rootDir)) {
        printf("Test cannot run with -r.\n");
        return false;
    }

    // Create a file with conflict-like suffix but as a normal file
    std::wstring conflictLikeName = L"normal_file (copy 1).txt";
    std::wstring filePath = BuildPath(rootDir, conflictLikeName);
    const char content[] = "This is a normal file with parentheses";

    // Cleanup
    DeleteFileW(filePath.c_str());

    printf("Creating normal file with conflict-like name: %S\n", conflictLikeName.c_str());

    // Create the file normally through mount
    HANDLE h = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW");
        return false;
    }
    DWORD written;
    WriteFile(h, content, sizeof(content) - 1, &written, nullptr);
    CloseHandle(h);

    printf("File created: OK\n");

    // Verify file exists
    bool exists = PathExists(filePath.c_str());
    printf("File exists: %s\n", exists ? "YES" : "NO");

    // Read back and verify content
    bool contentMatch = false;
    h = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buffer[256] = {};
        DWORD bytesRead = 0;
        if (ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
            contentMatch = (bytesRead == sizeof(content) - 1) &&
                           (memcmp(buffer, content, bytesRead) == 0);
        }
        CloseHandle(h);
    }
    printf("Content match: %s\n", contentMatch ? "YES" : "NO");

    // Delete and verify deletion
    BOOL deleted = DeleteFileW(filePath.c_str());
    bool deleteOk = deleted && !PathExists(filePath.c_str());
    printf("Delete OK: %s\n", deleteOk ? "YES" : "NO");

    return exists && contentMatch && deleteOk;
}

// Test 6: File without extension with conflict suffix
bool Test_ConflictNoExtension(const WCHAR* rootDir)
{
    if (rootDir == nullptr || rootDir[0] == L'\0') {
        printf("Test requires a valid mount point.\n");
        return false;
    }
    if (IsRawFilesystemRun(rootDir)) {
        printf("Test cannot run with -r.\n");
        return false;
    }

    std::wstring plainRoot(rootDir);
    std::wstring encryptedRoot = ResolveEncryptedRoot();
    
    // Create base file without extension
    std::wstring baseName = L"noext_conflict_test";
    std::wstring basePath = BuildPath(plainRoot, baseName);
    const char content[] = "no-extension-content";

    DeleteFileW(basePath.c_str());

    // Snapshot encrypted directory
    std::set<std::wstring> baseline;
    EnumerateFiles(encryptedRoot, baseline);

    // Create base file
    HANDLE h = CreateFileW(basePath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW (base)");
        return false;
    }
    DWORD written;
    WriteFile(h, content, sizeof(content) - 1, &written, nullptr);
    CloseHandle(h);

    // Find encrypted backing file
    std::wstring encName = FindNewFile(encryptedRoot, baseline);
    if (encName.empty()) {
        printf("Failed to find encrypted backing file.\n");
        DeleteFileW(basePath.c_str());
        return false;
    }

    std::wstring encBasePath = BuildPath(encryptedRoot, encName);
    std::wstring encConflictPath = encBasePath + kConflictSuffix;

    DeleteFileW(encConflictPath.c_str());

    // Copy encrypted file with conflict suffix
    if (!CopyFileW(encBasePath.c_str(), encConflictPath.c_str(), FALSE)) {
        PrintLastError("CopyFileW");
        DeleteFileW(basePath.c_str());
        return false;
    }

    Sleep(200);

    // Find conflict file on mount
    std::set<std::wstring> mountedFiles;
    EnumerateFiles(plainRoot, mountedFiles);

    std::wstring actualConflictName;
    std::wstring actualConflictPath;
    for (const auto& f : mountedFiles) {
        if (f.find(L"noext_conflict_test") != std::wstring::npos &&
            f.find(L"conflict") != std::wstring::npos &&
            f != baseName) {
            actualConflictName = f;
            actualConflictPath = BuildPath(plainRoot, f);
            break;
        }
    }

    // Expected: noext_conflict_test (DESKTOP-QRI9EIH conflict 2023-04-02)
    std::wstring expectedName = baseName + kConflictSuffix;
    printf("Expected: %S\n", expectedName.c_str());
    printf("Actual:   %S\n", actualConflictName.c_str());

    bool visible = !actualConflictName.empty();
    printf("Conflict visible: %s\n", visible ? "YES" : "NO");

    // Read and verify content
    bool contentMatch = false;
    if (visible) {
        h = CreateFileW(actualConflictPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char buffer[256] = {};
            DWORD bytesRead = 0;
            if (ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
                contentMatch = (bytesRead == sizeof(content) - 1) &&
                               (memcmp(buffer, content, bytesRead) == 0);
            }
            CloseHandle(h);
        }
    }
    printf("Content match: %s\n", contentMatch ? "YES" : "NO");

    // Cleanup
    if (!actualConflictPath.empty()) {
        DeleteFileW(actualConflictPath.c_str());
    }
    DeleteFileW(basePath.c_str());
    DeleteFileW(encConflictPath.c_str());

    return visible && contentMatch;
}

// Test 7: Multiple conflict files for the same base
bool Test_ConflictMultiple(const WCHAR* rootDir)
{
    if (rootDir == nullptr || rootDir[0] == L'\0') {
        printf("Test requires a valid mount point.\n");
        return false;
    }
    if (IsRawFilesystemRun(rootDir)) {
        printf("Test cannot run with -r.\n");
        return false;
    }

    std::wstring plainRoot(rootDir);
    std::wstring encryptedRoot = ResolveEncryptedRoot();

    std::wstring baseName = L"multi_conflict.txt";
    std::wstring basePath = BuildPath(plainRoot, baseName);
    const char content[] = "multi-conflict-content";

    DeleteFileW(basePath.c_str());

    // Snapshot
    std::set<std::wstring> baseline;
    EnumerateFiles(encryptedRoot, baseline);

    // Create base file
    HANDLE h = CreateFileW(basePath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW (base)");
        return false;
    }
    DWORD written;
    WriteFile(h, content, sizeof(content) - 1, &written, nullptr);
    CloseHandle(h);

    // Find encrypted backing file
    std::wstring encName = FindNewFile(encryptedRoot, baseline);
    if (encName.empty()) {
        printf("Failed to find encrypted backing file.\n");
        DeleteFileW(basePath.c_str());
        return false;
    }

    std::wstring encBasePath = BuildPath(encryptedRoot, encName);

    // Create multiple conflict files with different suffixes
    const WCHAR* suffixes[] = {
        L" (PC1 conflict 2023-01-01)",
        L" (PC2 conflict 2023-02-02)",
        L" (PC3 conflict 2023-03-03)"
    };
    std::vector<std::wstring> encConflictPaths;

    for (const auto& suffix : suffixes) {
        std::wstring encConflictPath = encBasePath + suffix;
        DeleteFileW(encConflictPath.c_str());
        if (!CopyFileW(encBasePath.c_str(), encConflictPath.c_str(), FALSE)) {
            PrintLastError("CopyFileW");
        } else {
            encConflictPaths.push_back(encConflictPath);
        }
    }

    Sleep(300);

    // Count conflict files visible on mount
    std::set<std::wstring> mountedFiles;
    EnumerateFiles(plainRoot, mountedFiles);

    int conflictCount = 0;
    std::vector<std::wstring> conflictNames;
    for (const auto& f : mountedFiles) {
        if (f.find(L"multi_conflict") != std::wstring::npos &&
            f.find(L"conflict") != std::wstring::npos &&
            f != baseName) {
            conflictCount++;
            conflictNames.push_back(f);
        }
    }

    printf("Expected conflicts: %zu\n", _countof(suffixes));
    printf("Found conflicts: %d\n", conflictCount);
    for (const auto& name : conflictNames) {
        printf("  - %S\n", name.c_str());
    }

    bool allVisible = (conflictCount == static_cast<int>(_countof(suffixes)));
    printf("All conflicts visible: %s\n", allVisible ? "YES" : "NO");

    // Cleanup
    for (const auto& name : conflictNames) {
        DeleteFileW(BuildPath(plainRoot, name).c_str());
    }
    DeleteFileW(basePath.c_str());
    for (const auto& path : encConflictPaths) {
        DeleteFileW(path.c_str());
    }

    return allVisible;
}

// Test 8: Nested parentheses - file with existing parentheses gets conflict suffix
bool Test_ConflictNestedParentheses(const WCHAR* rootDir)
{
    if (rootDir == nullptr || rootDir[0] == L'\0') {
        printf("Test requires a valid mount point.\n");
        return false;
    }
    if (IsRawFilesystemRun(rootDir)) {
        printf("Test cannot run with -r.\n");
        return false;
    }

    std::wstring plainRoot(rootDir);
    std::wstring encryptedRoot = ResolveEncryptedRoot();

    // Use a unique filename to avoid confusion with other tests
    std::wstring baseName = L"nestparen_doc (version1).txt";
    std::wstring basePath = BuildPath(plainRoot, baseName);
    const char content[] = "nested-paren-content-unique";

    // Thorough cleanup - delete base file and any existing conflict files
    DeleteFileW(basePath.c_str());
    
    // Also cleanup any leftover conflict files from previous runs on mount
    std::set<std::wstring> existingFiles;
    EnumerateFiles(plainRoot, existingFiles);
    for (const auto& f : existingFiles) {
        if (f.find(L"nestparen_doc") != std::wstring::npos) {
            DeleteFileW(BuildPath(plainRoot, f).c_str());
        }
    }
    
    Sleep(200);

    // Snapshot after cleanup
    std::set<std::wstring> baseline;
    EnumerateFiles(encryptedRoot, baseline);
    
    printf("Baseline encrypted files: %zu\n", baseline.size());

    // Create base file
    HANDLE h = CreateFileW(basePath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW (base)");
        return false;
    }
    DWORD written;
    WriteFile(h, content, sizeof(content) - 1, &written, nullptr);
    CloseHandle(h);

    Sleep(100);

    // Find encrypted backing file
    std::wstring encName = FindNewFile(encryptedRoot, baseline);
    if (encName.empty()) {
        printf("Failed to find encrypted backing file.\n");
        DeleteFileW(basePath.c_str());
        return false;
    }

    printf("Encrypted base name: %S\n", encName.c_str());
    
    // Sanity check: encrypted name should not contain plain text like "(version1)"
    if (encName.find(L"(version1)") != std::wstring::npos || 
        encName.find(L"nestparen") != std::wstring::npos) {
        printf("ERROR: Encrypted filename contains plaintext - encryption broken.\n");
        DeleteFileW(basePath.c_str());
        return false;
    }

    std::wstring encBasePath = BuildPath(encryptedRoot, encName);
    std::wstring encConflictPath = encBasePath + kConflictSuffix;

    printf("Encrypted conflict path: %S\n", encConflictPath.c_str());

    DeleteFileW(encConflictPath.c_str());

    // Copy with conflict suffix
    if (!CopyFileW(encBasePath.c_str(), encConflictPath.c_str(), FALSE)) {
        PrintLastError("CopyFileW");
        DeleteFileW(basePath.c_str());
        return false;
    }

    Sleep(300);

    // Find conflict file - list all files for debug
    std::set<std::wstring> mountedFiles;
    EnumerateFiles(plainRoot, mountedFiles);

    printf("Files on mount (%zu):\n", mountedFiles.size());
    for (const auto& f : mountedFiles) {
        if (f.find(L"nestparen") != std::wstring::npos) {
            printf("  - %S\n", f.c_str());
        }
    }

    std::wstring actualConflictName;
    std::wstring actualConflictPath;
    for (const auto& f : mountedFiles) {
        // Look for our specific file with conflict suffix
        if (f.find(L"nestparen_doc") != std::wstring::npos &&
            f.find(L"version1") != std::wstring::npos &&
            f.find(L"conflict") != std::wstring::npos &&
            f != baseName) {
            actualConflictName = f;
            actualConflictPath = BuildPath(plainRoot, f);
            break;
        }
    }

    // Expected: nestparen_doc (version1) (DESKTOP-QRI9EIH conflict 2023-04-02).txt
    std::wstring expectedName = L"nestparen_doc (version1)" + std::wstring(kConflictSuffix) + L".txt";
    printf("Expected: %S\n", expectedName.c_str());
    printf("Actual:   %S\n", actualConflictName.c_str());

    bool visible = !actualConflictName.empty();
    printf("Conflict visible: %s\n", visible ? "YES" : "NO");

    // Read and verify
    bool contentMatch = false;
    if (visible) {
        h = CreateFileW(actualConflictPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char buffer[256] = {};
            DWORD bytesRead = 0;
            if (ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
                contentMatch = (bytesRead == sizeof(content) - 1) &&
                               (memcmp(buffer, content, bytesRead) == 0);
            }
            CloseHandle(h);
        }
    }
    printf("Content match: %s\n", contentMatch ? "YES" : "NO");

    // Test delete
    bool deleteOk = false;
    if (visible) {
        deleteOk = DeleteFileW(actualConflictPath.c_str()) && !PathExists(actualConflictPath.c_str());
    }
    printf("Delete OK: %s\n", deleteOk ? "YES" : "NO");

    // Cleanup
    DeleteFileW(basePath.c_str());
    DeleteFileW(encConflictPath.c_str());

    return visible && contentMatch && deleteOk;
}

// Test 9: Google Drive conflict pattern (filename_conf(N).ext)
bool Test_ConflictGoogleDrive(const WCHAR* rootDir)
{
    if (rootDir == nullptr || rootDir[0] == L'\0') {
        printf("Test requires a valid mount point.\n");
        return false;
    }
    if (IsRawFilesystemRun(rootDir)) {
        printf("Test cannot run with -r.\n");
        return false;
    }

    std::wstring plainRoot(rootDir);
    std::wstring encryptedRoot = ResolveEncryptedRoot();

    // Google Drive pattern: filename_conf(N).ext
    std::wstring baseName = L"gdrive_test.docx";
    std::wstring basePath = BuildPath(plainRoot, baseName);
    const char content[] = "google-drive-conflict-content";

    DeleteFileW(basePath.c_str());

    // Cleanup any leftover files
    std::set<std::wstring> existingFiles;
    EnumerateFiles(plainRoot, existingFiles);
    for (const auto& f : existingFiles) {
        if (f.find(L"gdrive_test") != std::wstring::npos) {
            DeleteFileW(BuildPath(plainRoot, f).c_str());
        }
    }

    Sleep(200);

    // Snapshot
    std::set<std::wstring> baseline;
    EnumerateFiles(encryptedRoot, baseline);

    // Create base file
    HANDLE h = CreateFileW(basePath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW (base)");
        return false;
    }
    DWORD written;
    WriteFile(h, content, sizeof(content) - 1, &written, nullptr);
    CloseHandle(h);

    Sleep(100);

    // Find encrypted backing file
    std::wstring encName = FindNewFile(encryptedRoot, baseline);
    if (encName.empty()) {
        printf("Failed to find encrypted backing file.\n");
        DeleteFileW(basePath.c_str());
        return false;
    }

    printf("Encrypted base name: %S\n", encName.c_str());

    // Need to insert _conf(1) before extension in encrypted filename
    // Find the extension position in encrypted name
    std::wstring encBasePath = BuildPath(encryptedRoot, encName);
    
    // Google Drive suffix: _conf(1)
    std::wstring gDriveSuffix = L"_conf(1)";
    std::wstring encConflictPath = encBasePath + gDriveSuffix;

    printf("Google Drive conflict path: %S\n", encConflictPath.c_str());

    DeleteFileW(encConflictPath.c_str());

    // Copy with Google Drive conflict suffix
    if (!CopyFileW(encBasePath.c_str(), encConflictPath.c_str(), FALSE)) {
        PrintLastError("CopyFileW");
        DeleteFileW(basePath.c_str());
        return false;
    }

    Sleep(300);

    // Find conflict file
    std::set<std::wstring> mountedFiles;
    EnumerateFiles(plainRoot, mountedFiles);

    printf("Files on mount (%zu):\n", mountedFiles.size());
    for (const auto& f : mountedFiles) {
        printf("  - %S\n", f.c_str());
    }

    std::wstring actualConflictName;
    std::wstring actualConflictPath;
    for (const auto& f : mountedFiles) {
        if (f.find(L"gdrive_test") != std::wstring::npos &&
            f.find(L"_conf") != std::wstring::npos &&
            f != baseName) {
            actualConflictName = f;
            actualConflictPath = BuildPath(plainRoot, f);
            break;
        }
    }

    // Expected: gdrive_test_conf(1).docx
    std::wstring expectedName = L"gdrive_test_conf(1).docx";
    printf("Expected: %S\n", expectedName.c_str());
    printf("Actual:   %S\n", actualConflictName.c_str());

    bool visible = !actualConflictName.empty();
    printf("Google Drive conflict visible: %s\n", visible ? "YES" : "NO");

    // Read and verify
    bool contentMatch = false;
    if (visible) {
        h = CreateFileW(actualConflictPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char buffer[256] = {};
            DWORD bytesRead = 0;
            if (ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
                contentMatch = (bytesRead == sizeof(content) - 1) &&
                               (memcmp(buffer, content, bytesRead) == 0);
            }
            CloseHandle(h);
        }
    }
    printf("Content match: %s\n", contentMatch ? "YES" : "NO");

    // Test delete
    bool deleteOk = false;
    if (visible) {
        deleteOk = DeleteFileW(actualConflictPath.c_str()) && !PathExists(actualConflictPath.c_str());
    }
    printf("Delete OK: %s\n", deleteOk ? "YES" : "NO");

    // Cleanup
    DeleteFileW(basePath.c_str());
    DeleteFileW(encConflictPath.c_str());

    return visible && contentMatch && deleteOk;
}
