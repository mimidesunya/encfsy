// test_common.h - Common test utilities and helpers
#pragma once

#include <windows.h>
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

//=============================================================================
// Command Line Configuration
//=============================================================================

struct TestConfig {
    std::wstring testDir;      // Test directory (e.g., "O:\\" or "F:\\work\\encfs_raw\\")
    std::wstring testFile;     // Full path to test file
    std::wstring nestedLower;  // Nested path for case test (lowercase)
    std::wstring nestedUpper;  // Nested path for case test (uppercase)
    bool showHelp;
    bool isRawFilesystem;      // True if testing on raw filesystem (not EncFS)
    bool stopOnFailure;        // True to stop immediately when a test fails
    bool caseInsensitive;      // True if --case-insensitive option is enabled
    bool cloudConflict;        // True if --cloud-conflict option is enabled
    std::vector<std::string> selectedCategories; // Categories to run (empty = all)
    
    TestConfig() : showHelp(false), isRawFilesystem(false), stopOnFailure(false),
                   caseInsensitive(false), cloudConflict(false) {}
};

struct TestCategoryInfo {
    const char* key;          // Option key (e.g., "edge")
    const char* description;  // Human-readable description
};

inline const std::vector<TestCategoryInfo>& GetAvailableCategories()
{
    static const std::vector<TestCategoryInfo> categories = {
        {"edge",        "Edge case tests (bug regression)"},
        {"thread",      "Thread safety tests"},
        {"multi",       "Multi-handle concurrent access tests"},
        {"basic",       "Basic I/O tests"},
        {"filesize",    "File size tests"},
        {"vsproject",   "VS project-like tests"},
        {"vsbuild",     "VS build pattern tests"},
        {"zip",         "ZIP/archive pattern tests"},
        {"aapt2",       "Android aapt2 pattern tests"},
        {"advanced",    "Advanced tests"},
        {"rename",      "File rename pattern tests"},
        {"conflict",    "Cloud sync conflict tests (requires --cloud-conflict)"},
        {"large",       "Large file tests"},
        {"windows",     "Windows filesystem feature tests"},
        {"performance", "Performance tests"}
    };
    return categories;
}

inline void PrintUsage(const char* programName) {
    printf("Usage: %s [options]\n", programName);
    printf("\n");
    printf("Options:\n");
    printf("  -d, --dir <path>          Test directory (default: O:\\)\n");
    printf("  -r, --raw                 Test on raw filesystem (F:\\work\\encfs_raw\\)\n");
    printf("  -s, --stop-on-failure     Stop immediately when a test fails\n");
    printf("  -c, --category <name>[,<name>...]  Run only specified categories\n");
    printf("  --case-insensitive        Mount EncFS with --case-insensitive option\n");
    printf("  --cloud-conflict          Mount EncFS with --cloud-conflict option\n");
    printf("                            (enables conflict category tests)\n");
    printf("  -h, --help                Show this help message and category list\n");
    printf("\n");
    printf("Categories:\n");
    for (const auto& cat : GetAvailableCategories()) {
        printf("  %-12s %s\n", cat.key, cat.description);
    }
    printf("\n");
    printf("Examples:\n");
    printf("  %s                          # Test on EncFS mount at O:\\ (all categories except conflict)\n", programName);
    printf("  %s -r                       # Test on raw filesystem at F:\\work\\encfs_raw\\\n", programName);
    printf("  %s --case-insensitive       # Test with case-insensitive mode\n", programName);
    printf("  %s --cloud-conflict         # Test with cloud conflict support\n", programName);
    printf("  %s --cloud-conflict -c conflict  # Run only conflict tests\n", programName);
    printf("  %s -d C:\\temp\\test      # Test in custom directory\n", programName);
    printf("  %s -s                       # Stop on first failure\n", programName);
    printf("  %s -c edge,basic            # Run only edge and basic categories\n", programName);
    printf("\n");
    printf("Note: Run with -r first to verify tests work correctly on a raw filesystem,\n");
    printf("      then run without -r to test the EncFS implementation.\n");
    printf("\n");
    printf("Note: The 'conflict' category requires --cloud-conflict option to be enabled.\n");
    printf("      Case-insensitive specific tests in 'basic' category require --case-insensitive.\n");
}

inline bool ParseCommandLine(int argc, char* argv[], TestConfig& config) {
    // Default values
    config.testDir = L"O:\\";
    config.showHelp = false;
    config.isRawFilesystem = false;
    config.stopOnFailure = false;
    config.caseInsensitive = false;
    config.cloudConflict = false;
    config.selectedCategories.clear();
    
    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(tolower(ch)); });
        return s;
    };

    auto addCategories = [&](const std::string& value) {
        size_t start = 0;
        while (start <= value.size()) {
            size_t pos = value.find(',', start);
            std::string token = value.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
            if (!token.empty()) {
                config.selectedCategories.push_back(toLower(token));
            }
            if (pos == std::string::npos) break;
            start = pos + 1;
        }
    };
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            config.showHelp = true;
            return true;
        }
        else if (arg == "-r" || arg == "--raw") {
            config.testDir = L"F:\\work\\encfs_raw\\";
            config.isRawFilesystem = true;
        }
        else if (arg == "-s" || arg == "--stop-on-failure") {
            config.stopOnFailure = true;
        }
        else if (arg == "--case-insensitive") {
            config.caseInsensitive = true;
        }
        else if (arg == "--cloud-conflict") {
            config.cloudConflict = true;
        }
        else if ((arg == "-d" || arg == "--dir") && i + 1 < argc) {
            i++;
            // Convert char* to wstring
            std::string dirArg = argv[i];
            config.testDir = std::wstring(dirArg.begin(), dirArg.end());
            
            // Ensure trailing backslash
            if (!config.testDir.empty() && config.testDir.back() != L'\\') {
                config.testDir += L'\\';
            }
        }
        else if ((arg == "-c" || arg == "--category") && i + 1 < argc) {
            i++;
            addCategories(argv[i]);
        }
        else {
            printf("Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    
    // Build derived paths
    config.testFile = config.testDir + L"TEST_FILE.txt";
    config.nestedLower = config.testDir + L"path\\case\\test_file.txt";
    config.nestedUpper = config.testDir + L"PATH\\CASE\\TEST_FILE.txt";
    
    return true;
}

//=============================================================================
// Test Result Management
//=============================================================================

struct TestResult {
    int testNumber;
    std::string testName;
    bool passed;
};

class TestRunner {
private:
    std::vector<TestResult> results;
    int currentTestNumber;
    std::wstring drive;
    std::wstring rootDir;
    std::wstring testFile;
    bool isRawFilesystem;
    bool stopOnFailure;
    bool shouldStop;

public:
    TestRunner(const WCHAR* drv, const WCHAR* root, const WCHAR* file, bool isRaw = false, bool stopOnFail = false)
        : currentTestNumber(0), drive(drv), rootDir(root), testFile(file), 
          isRawFilesystem(isRaw), stopOnFailure(stopOnFail), shouldStop(false) {}

    const WCHAR* getDrive() const { return drive.c_str(); }
    const WCHAR* getRootDir() const { return rootDir.c_str(); }
    const WCHAR* getTestFile() const { return testFile.c_str(); }
    bool isRaw() const { return isRawFilesystem; }
    bool stopped() const { return shouldStop; }

    void runTest(const char* name, bool (*fn)(const WCHAR*), const WCHAR* arg) {
        if (shouldStop) return;
        runTestImpl(name, fn(arg));
    }

    void runTest(const char* name, bool (*fn)(const WCHAR*, const WCHAR*), const WCHAR* a1, const WCHAR* a2) {
        if (shouldStop) return;
        runTestImpl(name, fn(a1, a2));
    }

    void runTest(const char* name, bool (*fn)(const WCHAR*, DWORD), const WCHAR* a1, DWORD a2) {
        if (shouldStop) return;
        runTestImpl(name, fn(a1, a2));
    }

    void printSummary() const {
        printf("\n");
        printf("================================================================================\n");
        printf("                              TEST SUMMARY\n");
        printf("================================================================================\n\n");

        int passed = 0;
        int failed = 0;
        std::vector<const TestResult*> failedTests;

        for (const auto& r : results) {
            if (r.passed) {
                passed++;
            } else {
                failed++;
                failedTests.push_back(&r);
            }
        }

        printf("Total: %d tests | Passed: %d | Failed: %d\n\n", 
               static_cast<int>(results.size()), passed, failed);

        if (failed > 0) {
            printf("--------------------------------------------------------------------------------\n");
            printf("                            FAILED TESTS\n");
            printf("--------------------------------------------------------------------------------\n");
            for (const auto* r : failedTests) {
                printf("  [%02d] %s\n", r->testNumber, r->testName.c_str());
            }
            printf("--------------------------------------------------------------------------------\n");
            printf("\n*** %d TEST(S) FAILED ***\n\n", failed);
        } else {
            printf("*** ALL TESTS PASSED ***\n\n");
        }
    }

    bool allPassed() const {
        for (const auto& r : results) {
            if (!r.passed) return false;
        }
        return true;
    }

private:
    void runTestImpl(const char* name, bool result) {
        currentTestNumber++;
        
        printf("\n");
        printf("========== [%02d] %s ==========\n", currentTestNumber, name);
        
        results.push_back({currentTestNumber, name, result});
        
        if (result) {
            printf("[%02d] PASS: %s\n", currentTestNumber, name);
        } else {
            printf("\n");
            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            printf("!!!  [%02d] FAILED: %s\n", currentTestNumber, name);
            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
            
            if (stopOnFailure) {
                printf("\n*** STOPPING: --stop-on-failure is enabled ***\n");
                shouldStop = true;
            }
        }
    }
};

//=============================================================================
// Helper Functions
//=============================================================================;

// Simple helper to print Windows API errors consistently
inline void PrintLastError(const char* api)
{
    DWORD lastError = GetLastError();
    printf("%s ERROR: %lu\n", api, static_cast<unsigned long>(lastError));
}

// Helper to print final path of a handle
inline bool PrintFinalPath(HANDLE h)
{
    WCHAR name[260];
    if (!GetFinalPathNameByHandleW(h, name, static_cast<DWORD>(sizeof(name) / sizeof(name[0])), FILE_NAME_NORMALIZED)) {
        PrintLastError("GetFinalPathNameByHandleW");
        return false;
    }
    wprintf(L"FinalPath: %s\n", name);
    return true;
}

// Helper to create a file handle with common params
inline HANDLE OpenTestFile(const WCHAR* path,
                           DWORD desiredAccess,
                           DWORD shareMode,
                           DWORD creationDisposition,
                           DWORD flagsAndAttributes)
{
    HANDLE h = CreateFileW(path, desiredAccess, shareMode, NULL, creationDisposition, flagsAndAttributes, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW");
    }
    return h;
}

// Check whether a path exists (file or directory)
inline bool PathExists(const WCHAR* path)
{
    DWORD attrs = GetFileAttributesW(path);
    return (attrs != INVALID_FILE_ATTRIBUTES);
}

// Helper to create parent directories recursively
inline bool CreateParentDirectories(const WCHAR* path)
{
    std::wstring pathStr(path);
    size_t pos = 0;
    
    // Skip drive letter or UNC prefix
    if (pathStr.length() >= 2 && pathStr[1] == L':') {
        pos = 3;  // Skip "X:\"
    }
    
    while ((pos = pathStr.find(L'\\', pos)) != std::wstring::npos) {
        std::wstring subpath = pathStr.substr(0, pos);
        if (!subpath.empty() && !PathExists(subpath.c_str())) {
            if (!CreateDirectoryW(subpath.c_str(), NULL)) {
                DWORD err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS) {
                    return false;
                }
            }
        }
        pos++;
    }
    return true;
}

// Helper to delete a file and its parent directories if empty
inline void CleanupTestFile(const WCHAR* path)
{
    DeleteFileW(path);
    
    std::wstring pathStr(path);
    size_t pos = pathStr.rfind(L'\\');
    
    while (pos != std::wstring::npos && pos > 3) {
        std::wstring dir = pathStr.substr(0, pos);
        if (!RemoveDirectoryW(dir.c_str())) {
            break;  // Directory not empty or other error
        }
        pos = dir.rfind(L'\\');
    }
}
