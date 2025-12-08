// EncFSy_test.cpp - Main test entry point
//
// ./encfs.exe F:\work\encfs O: --dokan-mount-manager --alt-stream --case-insensitive

#include "test_common.h"
#include "test_declarations.h"

int main(int argc, char* argv[])
{
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
    printf("Mode: %s\n", config.isRawFilesystem ? "RAW FILESYSTEM (baseline verification)" : "EncFS");
    printf("Stop on failure: %s\n", config.stopOnFailure ? "YES" : "NO");
    printf("================================================================================\n");
    
    if (config.isRawFilesystem) {
        printf("\n");
        printf("*** RUNNING ON RAW FILESYSTEM FOR BASELINE VERIFICATION ***\n");
        printf("*** All tests should pass on raw filesystem. If they fail here,\n");
        printf("*** the test itself has a bug, not the EncFS implementation.\n");
        printf("\n");
    }

    // Verify test directory exists
    if (!PathExists(rootDir)) {
        wprintf(L"ERROR: Test directory does not exist: %s\n", rootDir);
        printf("Please create the directory or specify a different one with -d option.\n");
        return -1;
    }

    TestRunner runner(drive, rootDir, file, config.isRawFilesystem, config.stopOnFailure);

    //=========================================================================
    // Edge Case Tests (for previously fixed bugs)
    //=========================================================================
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

    //=========================================================================
    // Thread Safety and Race Condition Tests
    //=========================================================================
    printf("\n--- THREAD SAFETY TESTS ---\n");
    
    runner.runTest("High concurrency file access", Test_HighConcurrencyAccess, file);
    runner.runTest("File IV persistence across reopens", Test_FileIVPersistence, file);
    runner.runTest("Rapid file operation cycle", Test_RapidFileOperationCycle, file);
    runner.runTest("Write then immediate read at offset 0", Test_WriteImmediateReadOffset0, file);

    //=========================================================================
    // Multi-Handle Concurrent Access Tests (aapt2 block corruption regression)
    //=========================================================================
    printf("\n--- MULTI-HANDLE CONCURRENT ACCESS TESTS (aapt2 regression) ---\n");
    
    runner.runTest("Multi-handle concurrent write", Test_MultiHandleConcurrentWrite, file);
    runner.runTest("Multi-handle write then read", Test_MultiHandleWriteThenRead, file);
    runner.runTest("aapt2-like multi-file access", Test_Aapt2LikeMultiFileAccess, file);
    runner.runTest("Concurrent multi-offset write", Test_ConcurrentMultiOffsetWrite, file);
    runner.runTest("Stress multi-handle read/write", Test_StressMultiHandleReadWrite, file);

    //=========================================================================
    // Basic I/O Tests
    //=========================================================================
    printf("\n--- BASIC I/O TESTS ---\n");
    
    runner.runTest("Directory operations", Test_DirectoryOps, rootDir);
    runner.runTest("Create and print final path", Test_CreateAndPrintPath, file);
    runner.runTest("Case-insensitive open", Test_CaseInsensitiveOpen, fileLowerNested, fileCaseVariant);
    runner.runTest("Buffered IO (seek, read, write)", Test_BufferedIO, file);
    runner.runTest("Append-only write", Test_AppendWrite, file);
    runner.runTest("No-buffering IO (sector aligned)", Test_NoBufferingIO, file, drive);
    runner.runTest("Alternate Data Streams", Test_AlternateDataStreams, file);
    runner.runTest("File attributes and times", Test_FileAttributesAndTimes, file);
    runner.runTest("Sharing and byte-range locks", Test_SharingAndLocks, file);
    runner.runTest("Sparse file", Test_SparseFile, file);
    runner.runTest("Block boundary IO (512)", Test_BlockBoundaryIO, file, (DWORD)512);
    runner.runTest("Block boundary IO (4096)", Test_BlockBoundaryIO, file, (DWORD)4096);

    //=========================================================================
    // File Size Tests
    //=========================================================================
    printf("\n--- FILE SIZE TESTS ---\n");
    
    runner.runTest("Expand and shrink file size", Test_ExpandShrink, file);
    runner.runTest("SetEndOfFile then write (cache test)", Test_SetEndOfFileThenWrite, file);
    runner.runTest("Truncate then partial write", Test_TruncateThenPartialWrite, file);
    runner.runTest("Expand then write beyond", Test_ExpandThenWriteBeyond, file);
    runner.runTest("Multiple SetEndOfFile operations", Test_MultipleSetEndOfFile, file);
    runner.runTest("Truncate at block boundary", Test_TruncateAtBlockBoundary, file);
    runner.runTest("Truncate to zero then rewrite", Test_TruncateToZeroThenRewrite, file);
    runner.runTest("Write immediately after SetEndOfFile", Test_WriteImmediatelyAfterSetEndOfFile, file);

    //=========================================================================
    // VS Project-like Tests (from debug log analysis)
    //=========================================================================
    printf("\n--- VS PROJECT-LIKE TESTS ---\n");
    
    runner.runTest("SetEndOfFile(0) + SetAllocationSize(0) + Write", Test_TruncateZeroAllocWrite, file);
    runner.runTest("Large write after truncate to zero", Test_LargeWriteAfterTruncateZero, file);
    runner.runTest("Rapid open-close-reopen cycle", Test_RapidOpenCloseReopen, file);
    runner.runTest("Multiple concurrent file handles", Test_MultipleConcurrentHandles, file);

    //=========================================================================
    // VS Build Pattern Tests (based on actual failure analysis)
    //=========================================================================
    printf("\n--- VS BUILD PATTERN TESTS ---\n");
    
    runner.runTest("CREATE_ALWAYS then write (json pattern)", Test_CreateAlwaysThenWrite, file);
    runner.runTest("TRUNCATE_EXISTING then write", Test_TruncateExistingThenWrite, file);
    runner.runTest("Empty file partial block write", Test_EmptyFilePartialBlockWrite, file);
    runner.runTest("Write/read from different handles", Test_WriteReadDifferentHandles, file);
    runner.runTest("JSON file truncate/rewrite cycles", Test_JsonFileTruncateRewrite, file);
    runner.runTest("Cache file pattern", Test_CacheFilePattern, file);
    runner.runTest("Parallel write and read", Test_ParallelWriteRead, file);
    runner.runTest("Create-write-close-reopen-read cycle", Test_CreateWriteCloseReopenRead, file);

    //=========================================================================
    // ZIP/Archive Pattern Tests (based on ziparchive failure)
    //=========================================================================
    printf("\n--- ZIP/ARCHIVE PATTERN TESTS ---\n");
    
    runner.runTest("ZIP-like read pattern (seek end, start)", Test_ZipLikeReadPattern, file);
    runner.runTest("Simultaneous multi-offset read", Test_SimultaneousMultiOffsetRead, file);
    runner.runTest("GetFileSize then read at offset 0", Test_GetFileSizeThenReadOffset0, file);
    runner.runTest("Read after truncate and write", Test_ReadAfterTruncateAndWrite, file);
    runner.runTest("Rapid seek-read cycles (overlay parsing)", Test_RapidSeekReadCycles, file);

    //=========================================================================
    // Android aapt2 Pattern Tests (large reads from middle of file)
    //=========================================================================
    printf("\n--- ANDROID AAPT2 PATTERN TESTS ---\n");
    
    runner.runTest("Large read from middle of file", Test_LargeReadFromMiddle, file);
    runner.runTest("Multi-block spanning read", Test_MultiBlockSpanningRead, file);

    //=========================================================================
    // Advanced Tests
    //=========================================================================
    printf("\n--- ADVANCED TESTS ---\n");
    
    runner.runTest("Word-like save pattern", Test_WordLikeSavePattern, rootDir);
    runner.runTest("Interleaved read-write with resize", Test_InterleavedReadWriteWithResize, file);
    runner.runTest("Overwrite then truncate", Test_OverwriteThenTruncate, file);

    //=========================================================================
    // Summary
    //=========================================================================
    runner.printSummary();
    
    if (config.isRawFilesystem) {
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
    }

    return runner.allPassed() ? 0 : -1;
}
