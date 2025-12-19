// EncFSy_test.cpp - Main test entry point
//
// =============================================================================
// TEST SETUP REQUIREMENTS:
// =============================================================================
// 1. Build output: test.exe (this program)
// 2. encfs.exe must be in the same directory as test.exe
// 3. Test configuration (hardcoded):
//      - Encrypted Root Dir: F:\work\encfs
//      - Mount Point: O:
//      - Password: TEST
//      - Options: --dokan-mount-manager --alt-stream (+ optional --case-insensitive --cloud-conflict)
//
// =============================================================================
// USAGE:
// =============================================================================
//   test.exe                  - Run tests on EncFS (auto mount/unmount)
//   test.exe -r               - Run tests on raw filesystem (baseline verification)
//   test.exe -s               - Stop on first failure
//   test.exe -c edge,basic    - Run only specified categories
//   test.exe --case-insensitive  - Mount with --case-insensitive option
//   test.exe --cloud-conflict    - Mount with --cloud-conflict option (enables conflict tests)
//   test.exe -h               - Show help
//
// =============================================================================
// AUTOMATIC BEHAVIOR:
// =============================================================================
// - If .encfs6.xml doesn't exist, it will be auto-created with password "TEST"
// - EncFS volume is auto-mounted before tests
// - EncFS volume is auto-unmounted after tests
// =============================================================================

#include "test_common.h"
#include "test_declarations.h"
#include <process.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>

//=============================================================================
// EncFS Mount/Unmount Configuration
//=============================================================================
static const WCHAR* ENCFS_EXE = L".\\encfs.exe";
static const WCHAR* ENCFS_ROOT_DIR = L"F:\\work\\encfs";
static const WCHAR* ENCFS_MOUNT_POINT = L"O:";
static const char* ENCFS_PASSWORD = "TEST";
static const WCHAR* ENCFS_CONFIG_FILE = L"F:\\work\\encfs\\.encfs6.xml";

//=============================================================================
// Helper: Check if drive is mounted
//=============================================================================
static bool IsDriveMounted(const WCHAR* mountPoint)
{
    WCHAR rootPath[8];
    wsprintfW(rootPath, L"%s\\", mountPoint);
    return GetDriveTypeW(rootPath) != DRIVE_NO_ROOT_DIR;
}

//=============================================================================
// Helper: Wait for drive to be mounted/unmounted
//=============================================================================
static bool WaitForDriveState(const WCHAR* mountPoint, bool shouldBeMounted, int timeoutSeconds)
{
    for (int i = 0; i < timeoutSeconds * 10; i++) {
        if (IsDriveMounted(mountPoint) == shouldBeMounted) {
            return true;
        }
        Sleep(100);
    }
    return false;
}

//=============================================================================
// Helper: Check if EncFS config file exists
//=============================================================================
static bool IsEncFSInitialized()
{
    return GetFileAttributesW(ENCFS_CONFIG_FILE) != INVALID_FILE_ATTRIBUTES;
}

//=============================================================================
// Initialize EncFS volume (create .encfs6.xml)
//=============================================================================
static bool InitializeEncFS(bool caseInsensitive)
{
    printf("================================================================================\n");
    printf("Initializing EncFS volume (creating .encfs6.xml)...\n");
    wprintf(L"  Root Dir: %s\n", ENCFS_ROOT_DIR);
    printf("  Password: %s\n", ENCFS_PASSWORD);
    printf("================================================================================\n");

    // Create root directory if it doesn't exist
    if (GetFileAttributesW(ENCFS_ROOT_DIR) == INVALID_FILE_ATTRIBUTES) {
        printf("Creating root directory...\n");
        if (!CreateDirectoryW(ENCFS_ROOT_DIR, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                printf("ERROR: Failed to create root directory (error=%lu)\n", static_cast<unsigned long>(err));
                return false;
            }
        }
    }

    // Create a pipe to send password to encfs.exe
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        printf("ERROR: Failed to create pipe\n");
        return false;
    }

    // Ensure the write handle is not inherited
    SetHandleInformation(hWritePipe, HANDLE_FLAG_INHERIT, 0);

    // Start encfs.exe to initialize (it will prompt for password twice for new volume)
    // Use a temporary mount point that we'll immediately unmount
    WCHAR initCmdLine[1024];
    wsprintfW(initCmdLine, L"\"%s\" \"%s\" %s --dokan-mount-manager --alt-stream%s",
              ENCFS_EXE, ENCFS_ROOT_DIR, ENCFS_MOUNT_POINT,
              caseInsensitive ? L" --case-insensitive" : L"");

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hReadPipe;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};

    wprintf(L"Executing: %s\n", initCmdLine);
    printf("(This will create a new EncFS volume with standard security settings)\n");

    if (!CreateProcessW(NULL, initCmdLine, NULL, NULL, TRUE,
                         CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        printf("ERROR: Failed to start encfs.exe (error=%lu)\n", static_cast<unsigned long>(err));
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return false;
    }

    // For new volume initialization, encfs.exe prompts:
    // 1. "Enter password:" (first time)
    // 2. "Confirm password:" (second time)
    // We need to send the password twice
    
    Sleep(2000);  // Wait for encfs to start and show initial prompts
    
    char passwordWithNewline[64];
    sprintf_s(passwordWithNewline, "%s\r\n", ENCFS_PASSWORD);
    DWORD written;
    
    // Send password first time
    printf("Sending password (1/2)...\n");
    WriteFile(hWritePipe, passwordWithNewline, static_cast<DWORD>(strlen(passwordWithNewline)), &written, NULL);
    FlushFileBuffers(hWritePipe);
    
    Sleep(500);  // Wait for confirmation prompt
    
    // Send password second time (confirmation)
    printf("Sending password confirmation (2/2)...\n");
    WriteFile(hWritePipe, passwordWithNewline, static_cast<DWORD>(strlen(passwordWithNewline)), &written, NULL);
    FlushFileBuffers(hWritePipe);
    
    CloseHandle(hWritePipe);
    CloseHandle(hReadPipe);

    // Wait for volume to be mounted (indicates successful initialization)
    printf("Waiting for initialization to complete...\n");
    if (!WaitForDriveState(ENCFS_MOUNT_POINT, true, 30)) {
        printf("ERROR: Timeout waiting for initialization\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Verify config file was created
    if (!IsEncFSInitialized()) {
        printf("ERROR: Config file was not created\n");
        return false;
    }

    printf("EncFS volume initialized successfully!\n");
    wprintf(L"Config file created: %s\n", ENCFS_CONFIG_FILE);
    printf("================================================================================\n\n");

    // Unmount the volume (we'll remount it properly in MountEncFS)
    printf("Unmounting after initialization...\n");
    
    WCHAR unmountCmd[256];
    wsprintfW(unmountCmd, L"\"%s\" -u %s", ENCFS_EXE, ENCFS_MOUNT_POINT);
    
    STARTUPINFOW siUnmount = { sizeof(siUnmount) };
    PROCESS_INFORMATION piUnmount = {};
    
    if (CreateProcessW(NULL, unmountCmd, NULL, NULL, FALSE, 0, NULL, NULL, &siUnmount, &piUnmount)) {
        WaitForSingleObject(piUnmount.hProcess, 10000);
        CloseHandle(piUnmount.hProcess);
        CloseHandle(piUnmount.hThread);
    }
    
    WaitForDriveState(ENCFS_MOUNT_POINT, false, 10);
    
    return true;
}

//=============================================================================
// Mount EncFS volume
//=============================================================================
static bool MountEncFS(bool caseInsensitive, bool cloudConflict)
{
    printf("================================================================================\n");
    printf("Mounting EncFS volume...\n");
    wprintf(L"  Root Dir: %s\n", ENCFS_ROOT_DIR);
    wprintf(L"  Mount Point: %s\n", ENCFS_MOUNT_POINT);
    printf("  Password: %s\n", ENCFS_PASSWORD);
    printf("  Case-insensitive: %s\n", caseInsensitive ? "YES" : "NO");
    printf("  Cloud-conflict: %s\n", cloudConflict ? "YES" : "NO");
    printf("================================================================================\n");

    // Check if already mounted
    if (IsDriveMounted(ENCFS_MOUNT_POINT)) {
        wprintf(L"Drive %s is already mounted.\n", ENCFS_MOUNT_POINT);
        return true;
    }

    // Check if root directory exists, create if not
    if (GetFileAttributesW(ENCFS_ROOT_DIR) == INVALID_FILE_ATTRIBUTES) {
        printf("Creating root directory...\n");
        if (!CreateDirectoryW(ENCFS_ROOT_DIR, NULL)) {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
                wprintf(L"ERROR: Failed to create root directory: %s (error=%lu)\n", 
                        ENCFS_ROOT_DIR, static_cast<unsigned long>(err));
                return false;
            }
        }
    }

    // Check if EncFS is initialized, initialize if not
    if (!IsEncFSInitialized()) {
        printf("EncFS config file not found. Initializing new volume...\n\n");
        if (!InitializeEncFS(caseInsensitive)) {
            printf("ERROR: Failed to initialize EncFS volume\n");
            return false;
        }
    }

    // Create a pipe to send password to encfs.exe
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        printf("ERROR: Failed to create pipe\n");
        return false;
    }

    // Ensure the write handle is not inherited
    SetHandleInformation(hWritePipe, HANDLE_FLAG_INHERIT, 0);

    // Build mount command line with options
    WCHAR mountCmdLine[1024];
    std::wstring options = L"--dokan-mount-manager --alt-stream";
    if (caseInsensitive) {
        options += L" --case-insensitive";
    }
    if (cloudConflict) {
        options += L" --cloud-conflict";
    }
    wsprintfW(mountCmdLine, L"\"%s\" \"%s\" %s %s",
              ENCFS_EXE, ENCFS_ROOT_DIR, ENCFS_MOUNT_POINT, options.c_str());

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hReadPipe;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};

    wprintf(L"Executing: %s\n", mountCmdLine);

    if (!CreateProcessW(NULL, mountCmdLine, NULL, NULL, TRUE,
                         CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        printf("ERROR: Failed to start encfs.exe (error=%lu)\n", static_cast<unsigned long>(err));
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return false;
    }

    // Write password to pipe
    Sleep(1000);  // Wait for encfs to start and prompt for password
    
    char passwordWithNewline[64];
    sprintf_s(passwordWithNewline, "%s\r\n", ENCFS_PASSWORD);
    DWORD written;
    WriteFile(hWritePipe, passwordWithNewline, static_cast<DWORD>(strlen(passwordWithNewline)), &written, NULL);
    FlushFileBuffers(hWritePipe);
    
    CloseHandle(hWritePipe);
    CloseHandle(hReadPipe);

    // Wait for mount (encfs.exe stays running, so we just wait for the drive)
    printf("Waiting for drive to be mounted...\n");
    if (!WaitForDriveState(ENCFS_MOUNT_POINT, true, 30)) {
        printf("ERROR: Timeout waiting for drive to mount\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    wprintf(L"EncFS mounted successfully at %s\n", ENCFS_MOUNT_POINT);
    printf("================================================================================\n\n");
    return true;
}

//=============================================================================
// Unmount EncFS volume
//=============================================================================
static bool UnmountEncFS()
{
    printf("\n================================================================================\n");
    printf("Unmounting EncFS volume...\n");
    printf("================================================================================\n");

    if (!IsDriveMounted(ENCFS_MOUNT_POINT)) {
        wprintf(L"Drive %s is not mounted.\n", ENCFS_MOUNT_POINT);
        return true;
    }

    // Build unmount command
    WCHAR cmdLine[256];
    wsprintfW(cmdLine, L"\"%s\" -u %s", ENCFS_EXE, ENCFS_MOUNT_POINT);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    wprintf(L"Executing: %s\n", cmdLine);

    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE,
                         0, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        printf("ERROR: Failed to start encfs.exe for unmount (error=%lu)\n", static_cast<unsigned long>(err));
        return false;
    }

    // Wait for unmount process to complete
    WaitForSingleObject(pi.hProcess, 10000);
    
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Wait for drive to be unmounted
    if (!WaitForDriveState(ENCFS_MOUNT_POINT, false, 10)) {
        printf("WARNING: Drive may still be mounted\n");
    } else {
        wprintf(L"EncFS unmounted successfully from %s\n", ENCFS_MOUNT_POINT);
    }

    printf("================================================================================\n");
    return true;
}

//=============================================================================
// Helper: Decide whether a category should run based on selection
//=============================================================================
static bool ShouldRunCategory(const std::vector<std::string>& selectedCategories, const char* key)
{
    if (selectedCategories.empty()) {
        return true; // No filter specified => run all
    }

    std::string loweredKey(key);
    std::transform(loweredKey.begin(), loweredKey.end(), loweredKey.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });

    for (const auto& cat : selectedCategories) {
        if (cat == loweredKey) {
            return true;
        }
    }
    return false;
}

//=============================================================================
// Test Suite Runner
//=============================================================================
static void RunAllTests(TestRunner& runner,
                        bool caseInsensitiveMode,
                        bool cloudConflictMode,
                        const WCHAR* drive,
                        const WCHAR* rootDir,
                        const WCHAR* file,
                        const WCHAR* fileLowerNested,
                        const WCHAR* fileCaseVariant,
                        const std::vector<std::string>& selectedCategories,
                        bool isRawFilesystem = false)
{
    (void)drive; // currently unused but kept for clarity

    //=====================================================================
    // Edge Case Tests (for previously fixed bugs)
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "edge")) {
        printf("\n--- EDGE CASE TESTS (Bug Regression) ---\n");
        runner.runTest("Zero-length read request", Test_ZeroLengthRead, file);
        runner.runTest("Zero-length write request", Test_ZeroLengthWrite, file);
        runner.runTest("SetEndOfFile boundary block", Test_SetEndOfFileBoundaryBlock, file);
        runner.runTest("File expansion partial block", Test_FileExpansionPartialBlock, file);
        runner.runTest("Rapid truncate and write", Test_RapidTruncateWrite, file);
        runner.runTest("Read at block boundaries", Test_ReadAtBlockBoundaries, file);
        runner.runTest("Write at block boundaries", Test_WriteAtBlockBoundaries, file);
        runner.runTest("Truncate zero immediate rewrite (ZIP pattern)", Test_TruncateZeroImmediateRewrite, file);
        runner.runTest("Write/read with separate handles", Test_WriteReadSeparateHandles, file);
        runner.runTest("Concurrent read while writing", Test_ConcurrentReadWhileWrite, file);
        runner.runTest("Read beyond EOF", Test_ReadBeyondEOF, file);
        runner.runTest("Empty file operations", Test_EmptyFileOperations, file);
    } else {
        printf("\n--- EDGE CASE TESTS (skipped; include with -c edge) ---\n");
    }

    //=====================================================================
    // Thread Safety and Race Condition Tests
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "thread")) {
        printf("\n--- THREAD SAFETY TESTS ---\n");
        runner.runTest("High concurrency file access", Test_HighConcurrencyAccess, file);
        runner.runTest("File IV persistence across reopens", Test_FileIVPersistence, file);
        runner.runTest("Rapid file operation cycle", Test_RapidFileOperationCycle, file);
        runner.runTest("Write then immediate read at offset 0", Test_WriteImmediateReadOffset0, file);
    } else {
        printf("\n--- THREAD SAFETY TESTS (skipped; include with -c thread) ---\n");
    }

    //=====================================================================
    // Multi-Handle Concurrent Access Tests (aapt2 block corruption regression)
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "multi")) {
        printf("\n--- MULTI-HANDLE CONCURRENT ACCESS TESTS (aapt2 regression) ---\n");
        runner.runTest("Multi-handle concurrent write", Test_MultiHandleConcurrentWrite, file);
        runner.runTest("Multi-handle write then read", Test_MultiHandleWriteThenRead, file);
        runner.runTest("aapt2-like multi-file access", Test_Aapt2LikeMultiFileAccess, file);
        runner.runTest("Concurrent multi-offset write", Test_ConcurrentMultiOffsetWrite, file);
        runner.runTest("Stress multi-handle read/write", Test_StressMultiHandleReadWrite, file);
    } else {
        printf("\n--- MULTI-HANDLE CONCURRENT ACCESS TESTS (skipped; include with -c multi) ---\n");
    }

    //=====================================================================
    // Basic I/O Tests
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "basic")) {
        printf("\n--- BASIC I/O TESTS ---\n");
        runner.runTest("Directory operations", Test_DirectoryOps, rootDir);
        runner.runTest("Create and print final path", Test_CreateAndPrintPath, file);
        if (caseInsensitiveMode) {
            runner.runTest("Case-insensitive open", Test_CaseInsensitiveOpen, fileLowerNested, fileCaseVariant);
        } else {
            printf("Skipping case-insensitive-only test: Case-insensitive open (requires --case-insensitive)\n");
        }
        runner.runTest("Buffered IO (seek, read, write)", Test_BufferedIO, file);
        runner.runTest("Append-only write", Test_AppendWrite, file);
        runner.runTest("No-buffering IO (sector aligned)", Test_NoBufferingIO, file, drive);
        runner.runTest("File attributes and times", Test_FileAttributesAndTimes, file);
        runner.runTest("Sharing and byte-range locks", Test_SharingAndLocks, file);
        runner.runTest("Sparse file", Test_SparseFile, file);
        runner.runTest("Block boundary IO (512)", Test_BlockBoundaryIO, file, (DWORD)512);
        runner.runTest("Block boundary IO (4096)", Test_BlockBoundaryIO, file, (DWORD)4096);
    } else {
        printf("\n--- BASIC I/O TESTS (skipped; include with -c basic) ---\n");
    }

    //=====================================================================
    // File Size Tests
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "filesize")) {
        printf("\n--- FILE SIZE TESTS ---\n");
        runner.runTest("Expand and shrink file size", Test_ExpandShrink, file);
        runner.runTest("SetEndOfFile then write (cache test)", Test_SetEndOfFileThenWrite, file);
        runner.runTest("Truncate then partial write", Test_TruncateThenPartialWrite, file);
        runner.runTest("Expand then write beyond", Test_ExpandThenWriteBeyond, file);
        runner.runTest("Multiple SetEndOfFile operations", Test_MultipleSetEndOfFile, file);
        runner.runTest("Truncate at block boundary", Test_TruncateAtBlockBoundary, file);
        runner.runTest("Truncate to zero then rewrite", Test_TruncateToZeroThenRewrite, file);
        runner.runTest("Write immediately after SetEndOfFile", Test_WriteImmediatelyAfterSetEndOfFile, file);
    } else {
        printf("\n--- FILE SIZE TESTS (skipped; include with -c filesize) ---\n");
    }

    //=====================================================================
    // VS Project-like Tests (from debug log analysis)
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "vsproject")) {
        printf("\n--- VS PROJECT-LIKE TESTS ---\n");
        runner.runTest("SetEndOfFile(0) + SetAllocationSize(0) + Write", Test_TruncateZeroAllocWrite, file);
        runner.runTest("Large write after truncate to zero", Test_LargeWriteAfterTruncateZero, file);
        runner.runTest("Rapid open-close-reopen cycle", Test_RapidOpenCloseReopen, file);
        runner.runTest("Multiple concurrent file handles", Test_MultipleConcurrentHandles, file);
    } else {
        printf("\n--- VS PROJECT-LIKE TESTS (skipped; include with -c vsproject) ---\n");
    }

    //=====================================================================
    // VS Build Pattern Tests (based on actual failure analysis)
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "vsbuild")) {
        printf("\n--- VS BUILD PATTERN TESTS ---\n");
        runner.runTest("CREATE_ALWAYS then write (json pattern)", Test_CreateAlwaysThenWrite, file);
        runner.runTest("TRUNCATE_EXISTING then write", Test_TruncateExistingThenWrite, file);
        runner.runTest("Empty file partial block write", Test_EmptyFilePartialBlockWrite, file);
        runner.runTest("Write/read from different handles", Test_WriteReadDifferentHandles, file);
        runner.runTest("JSON file truncate/rewrite cycles", Test_JsonFileTruncateRewrite, file);
        runner.runTest("Cache file pattern", Test_CacheFilePattern, file);
        runner.runTest("Parallel write and read", Test_ParallelWriteRead, file);
        runner.runTest("Create-write-close-reopen-read cycle", Test_CreateWriteCloseReopenRead, file);
    } else {
        printf("\n--- VS BUILD PATTERN TESTS (skipped; include with -c vsbuild) ---\n");
    }

    //=====================================================================
    // ZIP/Archive Pattern Tests (based on ziparchive failure)
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "zip")) {
        printf("\n--- ZIP/ARCHIVE PATTERN TESTS ---\n");
        runner.runTest("ZIP-like read pattern (seek end, start)", Test_ZipLikeReadPattern, file);
        runner.runTest("Simultaneous multi-offset read", Test_SimultaneousMultiOffsetRead, file);
        runner.runTest("GetFileSize then read at offset 0", Test_GetFileSizeThenReadOffset0, file);
        runner.runTest("Read after truncate and write", Test_ReadAfterTruncateAndWrite, file);
        runner.runTest("Rapid seek-read cycles (overlay parsing)", Test_RapidSeekReadCycles, file);
    } else {
        printf("\n--- ZIP/ARCHIVE PATTERN TESTS (skipped; include with -c zip) ---\n");
    }

    //=====================================================================
    // Android aapt2 Pattern Tests (large reads from middle of file)
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "aapt2")) {
        printf("\n--- ANDROID AAPT2 PATTERN TESTS ---\n");
        runner.runTest("Large read from middle of file", Test_LargeReadFromMiddle, file);
        runner.runTest("Multi-block spanning read", Test_MultiBlockSpanningRead, file);
    } else {
        printf("\n--- ANDROID AAPT2 PATTERN TESTS (skipped; include with -c aapt2) ---\n");
    }

    //=====================================================================
    // Advanced Tests
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "advanced")) {
        printf("\n--- ADVANCED TESTS ---\n");
        runner.runTest("Word-like save pattern", Test_WordLikeSavePattern, rootDir);
        runner.runTest("Interleaved read-write with resize", Test_InterleavedReadWriteWithResize, file);
        runner.runTest("Overwrite then truncate", Test_OverwriteThenTruncate, file);
    } else {
        printf("\n--- ADVANCED TESTS (skipped; include with -c advanced) ---\n");
    }

    //=====================================================================
    // File Rename Pattern Tests (Adobe Reader regression)
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "rename")) {
        printf("\n--- FILE RENAME PATTERN TESTS (Adobe Reader regression) ---\n");
        runner.runTest("Adobe Reader-like save pattern", Test_AdobeReaderSavePattern, rootDir);
        runner.runTest("Rapid rename to same target", Test_RapidRenameToSameTarget, rootDir);
        runner.runTest("Rename chain cycle (A->B->C->A)", Test_RenameChainCycle, rootDir);
        runner.runTest("Write-rename-read immediate", Test_WriteRenameReadImmediate, rootDir);
        runner.runTest("Rapid rename cycle", Test_RapidRenameCycle, rootDir);
        runner.runTest("Rename with concurrent access", Test_RenameWithConcurrentAccess, rootDir);
    } else {
        printf("\n--- FILE RENAME PATTERN TESTS (skipped; include with -c rename) ---\n");
    }

    //=====================================================================
    // Cloud Conflict Tests
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "conflict")) {
        // Conflict tests require --cloud-conflict option
        if (!cloudConflictMode) {
            printf("\n--- CLOUD CONFLICT TESTS (skipped; requires --cloud-conflict option) ---\n");
        } else if (isRawFilesystem) {
            printf("\n--- CLOUD CONFLICT TESTS (skipped; requires EncFS mount, not raw filesystem) ---\n");
        } else {
            printf("\n--- CLOUD CONFLICT TESTS ---\n");
            runner.runTest("Shadow copy conflict detection", Test_ConflictShadowCopy, rootDir);
            runner.runTest("Automatic merge resolution", Test_ConflictAutoMerge, rootDir);
            runner.runTest("Manual resolution prompt", Test_ConflictManualResolution, rootDir);
            runner.runTest("Conflict file copy", Test_ConflictCopy, rootDir);
            runner.runTest("Normal file with conflict-like name", Test_ConflictLikeNormalFile, rootDir);
            runner.runTest("Conflict file without extension", Test_ConflictNoExtension, rootDir);
            runner.runTest("Multiple conflict files", Test_ConflictMultiple, rootDir);
            runner.runTest("Nested parentheses conflict", Test_ConflictNestedParentheses, rootDir);
            runner.runTest("Google Drive conflict pattern", Test_ConflictGoogleDrive, rootDir);
        }
    } else {
        printf("\n--- CLOUD CONFLICT TESTS (skipped; include with -c conflict and --cloud-conflict) ---\n");
    }

    //=====================================================================
    // Large File Tests (potential overflow issues)
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "large")) {
        printf("\n--- LARGE FILE TESTS (overflow prevention) ---\n");
        runner.runTest("Large file offset (>2GB)", Test_LargeFileOffset, file);
        runner.runTest("Block number overflow prevention", Test_BlockNumberOverflow, file);
        runner.runTest("Return value overflow handling", Test_ReturnValueOverflow, file);
        runner.runTest("Sparse file with large gaps", Test_SparseFileWithLargeGaps, file);
        runner.runTest("Type consistency in file ops", Test_ReverseReadTypeConsistency, file);
        runner.runTest("Random access to 5GB file", Test_RandomAccessLargeFile, file);
        runner.runTest("Performance at high offsets", Test_PerformanceAtHighOffsets, file);
    } else {
        printf("\n--- LARGE FILE TESTS (skipped; include with -c large) ---\n");
    }

    //=====================================================================
    // Windows-Specific Filesystem Tests
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "windows")) {
        printf("\n--- WINDOWS FILESYSTEM TESTS (Alternate Data Streams) ---\n");
        runner.runTest("Alternate Data Streams", Test_AlternateDataStreams, file);
        runner.runTest("Multiple Alternate Streams", Test_MultipleAlternateStreams, file);
        runner.runTest("Large Alternate Stream (1MB)", Test_LargeAlternateStream, file);
        runner.runTest("ADS survives file rename", Test_ADSSurvivesRename, rootDir);

        printf("\n--- WINDOWS FILESYSTEM TESTS (Links) ---\n");
        runner.runTest("Symbolic link to file", Test_SymbolicLinkFile, rootDir);
        runner.runTest("Symbolic link to directory", Test_SymbolicLinkDirectory, rootDir);
        runner.runTest("Hard link", Test_HardLink, rootDir);
        runner.runTest("Multiple hard links", Test_MultipleHardLinks, rootDir);

        printf("\n--- WINDOWS FILESYSTEM TESTS (Memory-Mapped Files) ---\n");
        runner.runTest("Memory-mapped file (read)", Test_MemoryMappedFileRead, file);
        runner.runTest("Memory-mapped file (write)", Test_MemoryMappedFileWrite, file);

        printf("\n--- WINDOWS FILESYSTEM TESTS (Notifications & Special Names) ---\n");
        runner.runTest("File change notification", Test_FileChangeNotification, rootDir);
        runner.runTest("Reserved filenames", Test_ReservedFilenames, rootDir);
        runner.runTest("Trailing spaces and dots", Test_TrailingSpacesAndDots, rootDir);
        runner.runTest("Long filenames (255 char)", Test_LongFilenames, rootDir);
        runner.runTest("Unicode filenames", Test_UnicodeFilenames, rootDir);

        printf("\n--- WINDOWS FILESYSTEM TESTS (Shortcuts) ---\n");
        runner.runTest("Shortcut to file (.lnk)", Test_ShortcutFile, rootDir);
        runner.runTest("Shortcut to directory (.lnk)", Test_ShortcutToDirectory, rootDir);
        runner.runTest("Shortcut mmap read (.lnk)", Test_ShortcutMemoryMappedRead, rootDir);

        printf("\n--- WINDOWS FILESYSTEM TESTS (Volume Information) ---\n");
        runner.runTest("Volume information", Test_VolumeInformation, rootDir);
        runner.runTest("Disk free space", Test_DiskFreeSpace, rootDir);
    } else {
        printf("\n--- WINDOWS FILESYSTEM TESTS (skipped; include with -c windows) ---\n");
    }

    //=====================================================================
    // Performance Tests
    //=====================================================================
    if (ShouldRunCategory(selectedCategories, "performance")) {
        printf("\n--- PERFORMANCE TESTS ---\n");
        runner.runTest("Sequential write performance", Test_SequentialWritePerformance, file);
        runner.runTest("Sequential read performance", Test_SequentialReadPerformance, file);
        runner.runTest("Random access read performance", Test_RandomReadPerformance, file);
        runner.runTest("Large single read performance", Test_LargeSingleReadPerformance, file);
        runner.runTest("File resize performance", Test_FileResizePerformance, file);
        runner.runTest("Memory allocation impact", Test_MemoryAllocationImpact, file);
        runner.runTest("Concurrent I/O performance", Test_ConcurrentIOPerformance, file);
    } else {
        printf("\n--- PERFORMANCE TESTS (skipped; include with -c performance) ---\n");
    }
}

//=============================================================================
// Main Entry Point
//=============================================================================
int main(int argc, char* argv[])
{
    auto updateTestPaths = [](TestConfig& cfg)
    {
        cfg.testFile = cfg.testDir + L"TEST_FILE.txt";
        cfg.nestedLower = cfg.testDir + L"path\\case\\test_file.txt";
        cfg.nestedUpper = cfg.testDir + L"PATH\\CASE\\TEST_FILE.txt";
    };

    // Parse command line arguments
    TestConfig config;
    if (!ParseCommandLine(argc, argv, config)) {
        PrintUsage(argv[0]);
        return -1;
    }

    if (config.showHelp) {
        PrintUsage(argv[0]);
        return 0;
    }

    updateTestPaths(config);

    // Raw filesystem path: run once
    if (config.isRawFilesystem) {
        const WCHAR* drive = config.testDir.c_str();
        const WCHAR* rootDir = config.testDir.c_str();
        const WCHAR* file = config.testFile.c_str();
        const WCHAR* fileLowerNested = config.nestedLower.c_str();
        const WCHAR* fileCaseVariant = config.nestedUpper.c_str();

        printf("================================================================================\n");
        printf("                        EncFSy File System Test Suite\n");
        printf("================================================================================\n");
        wprintf(L"Test Directory: %s\n", drive);
        wprintf(L"Test File: %s\n", file);
        printf("Mode: RAW FILESYSTEM (baseline verification)\n");
        printf("Stop on failure: %s\n", config.stopOnFailure ? "YES" : "NO");
        printf("================================================================================\n\n");

        printf("*** RUNNING ON RAW FILESYSTEM FOR BASELINE VERIFICATION ***\n\n");

        if (!PathExists(rootDir)) {
            wprintf(L"ERROR: Test directory does not exist: %s\n", rootDir);
            printf("Please create the directory or specify a different one with -d option.\n");
            return -1;
        }

        TestRunner runner(drive, rootDir, file, true, config.stopOnFailure);
        // For raw filesystem, assume case-insensitive (NTFS default) and no cloud conflict
        RunAllTests(runner, true, false, drive, rootDir, file, fileLowerNested, fileCaseVariant, config.selectedCategories, true);
        runner.printSummary();

        if (runner.allPassed()) {
            printf("================================================================================\n");
            printf("Raw filesystem baseline verification PASSED.\n");
            printf("You can now run without -r to test the EncFS implementation.\n");
            printf("================================================================================\n");
        } else {
            printf("================================================================================\n");
            printf("WARNING: Some tests failed on raw filesystem!\n");
            printf("This indicates bugs in the tests themselves, not EncFS.\n");
            printf("Please fix the failing tests before testing EncFS.\n");
            printf("================================================================================\n");
        }

        return runner.allPassed() ? 0 : -1;
    }

    // EncFS mode: single test pass with specified options
    config.testDir = L"O:\\";
    updateTestPaths(config);

    const WCHAR* drive = config.testDir.c_str();
    const WCHAR* rootDir = config.testDir.c_str();
    const WCHAR* file = config.testFile.c_str();
    const WCHAR* fileLowerNested = config.nestedLower.c_str();
    const WCHAR* fileCaseVariant = config.nestedUpper.c_str();

    printf("================================================================================\n");
    printf("                        EncFSy File System Test Suite\n");
    printf("================================================================================\n");
    wprintf(L"Test Directory: %s\n", drive);
    wprintf(L"Test File: %s\n", file);
    printf("Mode: EncFS Mount\n");
    printf("Stop on failure: %s\n", config.stopOnFailure ? "YES" : "NO");
    printf("Case-insensitive: %s\n", config.caseInsensitive ? "YES" : "NO");
    printf("Cloud-conflict: %s\n", config.cloudConflict ? "YES" : "NO");
    printf("================================================================================\n\n");

    if (!MountEncFS(config.caseInsensitive, config.cloudConflict)) {
        printf("ERROR: Failed to mount EncFS\n");
        return -1;
    }

    if (!PathExists(rootDir)) {
        wprintf(L"ERROR: Test directory does not exist after mount: %s\n", rootDir);
        UnmountEncFS();
        return -1;
    }

    TestRunner runner(drive, rootDir, file, false, config.stopOnFailure);
    RunAllTests(runner, config.caseInsensitive, config.cloudConflict, drive, rootDir, file, 
                fileLowerNested, fileCaseVariant, config.selectedCategories);
    runner.printSummary();

    UnmountEncFS();

    return runner.allPassed() ? 0 : -1;
}
