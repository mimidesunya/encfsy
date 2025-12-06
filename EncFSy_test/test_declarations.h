// test_declarations.h - Test function declarations
#pragma once

#include <windows.h>

//=============================================================================
// Basic I/O Tests (test_basic_io.cpp)
//=============================================================================
bool Test_DirectoryOps(const WCHAR* rootDir);
bool Test_CreateAndPrintPath(const WCHAR* file);
bool Test_CaseInsensitiveOpen(const WCHAR* fileLowerCase, const WCHAR* fileCaseVariant);
bool Test_BufferedIO(const WCHAR* file);
bool Test_AppendWrite(const WCHAR* file);
bool Test_NoBufferingIO(const WCHAR* file, const WCHAR* drive);
bool Test_AlternateDataStreams(const WCHAR* baseFile);
bool Test_FileAttributesAndTimes(const WCHAR* file);
bool Test_SharingAndLocks(const WCHAR* file);
bool Test_SparseFile(const WCHAR* file);
bool Test_BlockBoundaryIO(const WCHAR* file, DWORD blockSize);

//=============================================================================
// File Size Tests (test_file_size.cpp)
//=============================================================================
bool Test_ExpandShrink(const WCHAR* file);
bool Test_SetEndOfFileThenWrite(const WCHAR* file);
bool Test_TruncateThenPartialWrite(const WCHAR* file);
bool Test_ExpandThenWriteBeyond(const WCHAR* file);
bool Test_MultipleSetEndOfFile(const WCHAR* file);
bool Test_TruncateAtBlockBoundary(const WCHAR* file);
bool Test_TruncateToZeroThenRewrite(const WCHAR* file);
bool Test_WriteImmediatelyAfterSetEndOfFile(const WCHAR* file);
bool Test_TruncateZeroAllocWrite(const WCHAR* file);
bool Test_LargeWriteAfterTruncateZero(const WCHAR* file);
bool Test_RapidOpenCloseReopen(const WCHAR* file);
bool Test_MultipleConcurrentHandles(const WCHAR* file);

//=============================================================================
// Advanced Tests (test_advanced.cpp)
//=============================================================================
bool Test_WordLikeSavePattern(const WCHAR* rootDir);
bool Test_InterleavedReadWriteWithResize(const WCHAR* file);
bool Test_OverwriteThenTruncate(const WCHAR* file);

//=============================================================================
// VS Build-like Tests (test_vs_build.cpp)
//=============================================================================
bool Test_CreateAlwaysThenWrite(const WCHAR* file);
bool Test_TruncateExistingThenWrite(const WCHAR* file);
bool Test_EmptyFilePartialBlockWrite(const WCHAR* file);
bool Test_WriteReadDifferentHandles(const WCHAR* file);
bool Test_JsonFileTruncateRewrite(const WCHAR* file);
bool Test_CacheFilePattern(const WCHAR* file);
bool Test_ParallelWriteRead(const WCHAR* file);
bool Test_CreateWriteCloseReopenRead(const WCHAR* file);

// ZIP/Archive pattern tests
bool Test_ZipLikeReadPattern(const WCHAR* file);
bool Test_SimultaneousMultiOffsetRead(const WCHAR* file);
bool Test_GetFileSizeThenReadOffset0(const WCHAR* file);
bool Test_ReadAfterTruncateAndWrite(const WCHAR* file);
bool Test_RapidSeekReadCycles(const WCHAR* file);

// Android aapt2 pattern tests (large reads from middle of file)
bool Test_LargeReadFromMiddle(const WCHAR* file);
bool Test_MultiBlockSpanningRead(const WCHAR* file);

//=============================================================================
// Edge Case Tests (test_edge_cases.cpp)
// Tests for previously fixed bugs:
// - len==0 underflow in read/write
// - _setLength() boundary block handling
// - Block boundary edge cases
// - Multi-handle synchronization
// - Thread safety
//=============================================================================
bool Test_ZeroLengthRead(const WCHAR* file);
bool Test_ZeroLengthWrite(const WCHAR* file);
bool Test_SetEndOfFileBoundaryBlock(const WCHAR* file);
bool Test_FileExpansionPartialBlock(const WCHAR* file);
bool Test_RapidTruncateWrite(const WCHAR* file);
bool Test_ReadAtBlockBoundaries(const WCHAR* file);
bool Test_WriteAtBlockBoundaries(const WCHAR* file);
bool Test_TruncateZeroImmediateRewrite(const WCHAR* file);
bool Test_WriteReadSeparateHandles(const WCHAR* file);
bool Test_ConcurrentReadWhileWrite(const WCHAR* file);
bool Test_ReadBeyondEOF(const WCHAR* file);
bool Test_EmptyFileOperations(const WCHAR* file);

// New thread safety and race condition tests
bool Test_HighConcurrencyAccess(const WCHAR* file);
bool Test_FileIVPersistence(const WCHAR* file);
bool Test_RapidFileOperationCycle(const WCHAR* file);
bool Test_WriteImmediateReadOffset0(const WCHAR* file);

//=============================================================================
// Multi-Handle Concurrent Access Tests (test_edge_cases.cpp)
// Tests for aapt2-like concurrent file access patterns that caused
// ERROR_INVALID_BLOCK (1392) due to block corruption when multiple
// handles accessed the same file simultaneously.
//=============================================================================
bool Test_MultiHandleConcurrentWrite(const WCHAR* file);
bool Test_MultiHandleWriteThenRead(const WCHAR* file);
bool Test_Aapt2LikeMultiFileAccess(const WCHAR* file);
bool Test_ConcurrentMultiOffsetWrite(const WCHAR* file);
bool Test_StressMultiHandleReadWrite(const WCHAR* file);
