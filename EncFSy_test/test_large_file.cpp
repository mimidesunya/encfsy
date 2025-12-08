// test_large_file.cpp - Large file size tests
// Tests for potential overflow issues when handling large files (>2GB)
// These tests verify that size_t/int32_t type mixing doesn't cause issues

#include "test_common.h"
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

//=============================================================================
// Test: Large file offset read/write (>2GB boundary)
// Verifies that offsets larger than INT32_MAX work correctly
//=============================================================================
bool Test_LargeFileOffset(const WCHAR* file)
{
    printf("Testing large file offset (>2GB boundary)...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    // Create file with GENERIC_READ | GENERIC_WRITE
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to create file\n");
        return false;
    }
    
    // Test offset just past 2GB (2GB + 1MB)
    const LONGLONG testOffset = (2LL * 1024 * 1024 * 1024) + (1024 * 1024);
    const size_t testDataSize = 4096;
    
    printf("  Testing write at offset %lld (%.2f GB)...\n", 
           testOffset, static_cast<double>(testOffset) / (1024.0 * 1024.0 * 1024.0));
    
    // First, expand the file to the required size using SetEndOfFile
    LARGE_INTEGER pos;
    pos.QuadPart = testOffset + testDataSize;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        DWORD err = GetLastError();
        printf("SetFilePointerEx failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (!SetEndOfFile(h)) {
        DWORD err = GetLastError();
        printf("SetEndOfFile failed: error %lu\n", err);
        // This might fail due to disk space - skip test gracefully
        if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL) {
            printf("  SKIPPED: Not enough disk space for large file test\n");
            CloseHandle(h);
            DeleteFileW(file);
            return true;  // Not a failure, just skip
        }
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Seek to test offset
    pos.QuadPart = testOffset;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx to write position failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Prepare test pattern
    std::vector<char> writeData(testDataSize);
    for (size_t i = 0; i < testDataSize; i++) {
        writeData[i] = static_cast<char>('A' + (i % 26));
    }
    
    // Write data at large offset
    DWORD written = 0;
    if (!WriteFile(h, writeData.data(), static_cast<DWORD>(testDataSize), &written, NULL)) {
        DWORD err = GetLastError();
        printf("WriteFile at large offset failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (written != testDataSize) {
        printf("Partial write: expected %zu, wrote %lu\n", testDataSize, written);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    printf("  Wrote %lu bytes at offset %lld\n", written, testOffset);
    
    // Seek back and read
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx for read failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    std::vector<char> readData(testDataSize);
    DWORD bytesRead = 0;
    if (!ReadFile(h, readData.data(), static_cast<DWORD>(testDataSize), &bytesRead, NULL)) {
        DWORD err = GetLastError();
        printf("ReadFile at large offset failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (bytesRead != testDataSize) {
        printf("Partial read: expected %zu, read %lu\n", testDataSize, bytesRead);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Verify data
    if (memcmp(writeData.data(), readData.data(), testDataSize) != 0) {
        printf("Data mismatch at large offset!\n");
        // Find first mismatch
        for (size_t i = 0; i < testDataSize; i++) {
            if (writeData[i] != readData[i]) {
                printf("  First mismatch at relative offset %zu: expected 0x%02x, got 0x%02x\n",
                       i, static_cast<unsigned char>(writeData[i]), 
                       static_cast<unsigned char>(readData[i]));
                break;
            }
        }
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    printf("  Data verification OK at large offset\n");
    
    CloseHandle(h);
    DeleteFileW(file);
    
    printf("Large file offset test PASSED\n");
    return true;
}

//=============================================================================
// Test: Block number overflow check
// Verifies that block number calculations don't overflow for large files
//=============================================================================
bool Test_BlockNumberOverflow(const WCHAR* file)
{
    printf("Testing block number overflow prevention...\n");
    
    // EncFS block data size is typically 1016 bytes
    // At 2GB, block number would be ~2,000,000 which fits in int32
    // But we should verify the calculation doesn't overflow
    
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to create file\n");
        return false;
    }
    
    // Test with offset that would require large block number
    // 1016 bytes per block data, so 1GB = ~1,000,000 blocks
    const LONGLONG testOffset = 1LL * 1024 * 1024 * 1024;  // 1GB
    const size_t testDataSize = 2048;  // Span 2 blocks
    
    printf("  Testing at offset %lld (1 GB, ~1M blocks)...\n", testOffset);
    
    // Expand file
    LARGE_INTEGER pos;
    pos.QuadPart = testOffset + testDataSize;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (!SetEndOfFile(h)) {
        DWORD err = GetLastError();
        if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL) {
            printf("  SKIPPED: Not enough disk space\n");
            CloseHandle(h);
            DeleteFileW(file);
            return true;
        }
        printf("SetEndOfFile failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Seek to test offset
    pos.QuadPart = testOffset;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Write pattern
    std::vector<char> writeData(testDataSize);
    for (size_t i = 0; i < testDataSize; i++) {
        writeData[i] = static_cast<char>((i * 7 + 13) & 0xFF);
    }
    
    DWORD written = 0;
    if (!WriteFile(h, writeData.data(), static_cast<DWORD>(testDataSize), &written, NULL)) {
        DWORD err = GetLastError();
        printf("WriteFile failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    printf("  Wrote %lu bytes\n", written);
    
    // Read back and verify
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx for read failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    std::vector<char> readData(testDataSize);
    DWORD bytesRead = 0;
    if (!ReadFile(h, readData.data(), static_cast<DWORD>(testDataSize), &bytesRead, NULL)) {
        DWORD err = GetLastError();
        printf("ReadFile failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (bytesRead != testDataSize || memcmp(writeData.data(), readData.data(), testDataSize) != 0) {
        printf("Data verification failed!\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    printf("  Data verification OK\n");
    
    CloseHandle(h);
    DeleteFileW(file);
    
    printf("Block number overflow test PASSED\n");
    return true;
}

//=============================================================================
// Test: Return value overflow check
// read() and write() return int32_t, which limits single operation to ~2GB
// This test verifies that large reads/writes are handled correctly
//=============================================================================
bool Test_ReturnValueOverflow(const WCHAR* file)
{
    printf("Testing return value handling for large operations...\n");
    
    // Note: This test doesn't actually request >2GB in a single operation
    // because that would require massive memory. Instead, we verify that
    // the return type handling is correct for operations close to the limit.
    
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to create file\n");
        return false;
    }
    
    // Test with a size close to but not exceeding practical limits
    // We'll use 100MB which is large enough to stress test but practical
    const size_t testSize = 100 * 1024 * 1024;  // 100MB
    
    printf("  Testing %zu byte (%.0f MB) write/read...\n", 
           testSize, static_cast<double>(testSize) / (1024.0 * 1024.0));
    
    // Allocate buffers - if this fails, skip the test
    std::vector<char> writeData;
    try {
        writeData.resize(testSize);
    } catch (const std::bad_alloc&) {
        printf("  SKIPPED: Not enough memory for %zu byte buffer\n", testSize);
        CloseHandle(h);
        DeleteFileW(file);
        return true;
    }
    
    // Fill with pattern
    for (size_t i = 0; i < testSize; i++) {
        writeData[i] = static_cast<char>((i * 31 + 17) & 0xFF);
    }
    
    // Write in chunks (Windows API limits single write)
    const DWORD chunkSize = 64 * 1024 * 1024;  // 64MB chunks
    size_t totalWritten = 0;
    
    while (totalWritten < testSize) {
        DWORD toWrite = static_cast<DWORD>(std::min<size_t>(chunkSize, testSize - totalWritten));
        DWORD written = 0;
        
        if (!WriteFile(h, writeData.data() + totalWritten, toWrite, &written, NULL)) {
            DWORD err = GetLastError();
            if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL) {
                printf("  SKIPPED: Not enough disk space\n");
                CloseHandle(h);
                DeleteFileW(file);
                return true;
            }
            printf("WriteFile failed: error %lu\n", err);
            CloseHandle(h);
            DeleteFileW(file);
            return false;
        }
        
        totalWritten += written;
    }
    
    printf("  Wrote %zu bytes total\n", totalWritten);
    
    // Seek back to beginning
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Read back in chunks
    std::vector<char> readData;
    try {
        readData.resize(testSize);
    } catch (const std::bad_alloc&) {
        printf("  SKIPPED: Not enough memory for read buffer\n");
        CloseHandle(h);
        DeleteFileW(file);
        return true;
    }
    
    size_t totalRead = 0;
    while (totalRead < testSize) {
        DWORD toRead = static_cast<DWORD>(std::min<size_t>(chunkSize, testSize - totalRead));
        DWORD bytesRead = 0;
        
        if (!ReadFile(h, readData.data() + totalRead, toRead, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            printf("ReadFile failed: error %lu\n", err);
            CloseHandle(h);
            DeleteFileW(file);
            return false;
        }
        
        if (bytesRead == 0) {
            break;  // EOF
        }
        
        totalRead += bytesRead;
    }
    
    printf("  Read %zu bytes total\n", totalRead);
    
    // Verify
    if (totalRead != testSize) {
        printf("Size mismatch: wrote %zu, read %zu\n", testSize, totalRead);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (memcmp(writeData.data(), readData.data(), testSize) != 0) {
        printf("Data mismatch!\n");
        // Find first mismatch
        for (size_t i = 0; i < testSize; i++) {
            if (writeData[i] != readData[i]) {
                printf("  First mismatch at offset %zu: expected 0x%02x, got 0x%02x\n",
                       i, static_cast<unsigned char>(writeData[i]), 
                       static_cast<unsigned char>(readData[i]));
                break;
            }
        }
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    printf("  Data verification OK\n");
    
    CloseHandle(h);
    DeleteFileW(file);
    
    printf("Return value overflow test PASSED\n");
    return true;
}

//=============================================================================
// Test: Sparse file with large gaps
// Tests that sparse file handling works correctly with large offsets
//=============================================================================
bool Test_SparseFileWithLargeGaps(const WCHAR* file)
{
    printf("Testing sparse file with large gaps...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to create file\n");
        return false;
    }
    
    // Set sparse attribute
    DWORD bytesReturned;
    if (!DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        printf("  Note: Could not set sparse attribute (may not be supported)\n");
        // Continue anyway - test should still work
    }
    
    // Write at beginning
    const char startData[] = "START_DATA";
    const DWORD startLen = static_cast<DWORD>(sizeof(startData) - 1);
    DWORD written = 0;
    
    if (!WriteFile(h, startData, startLen, &written, NULL)) {
        printf("WriteFile at start failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    printf("  Wrote '%s' at offset 0\n", startData);
    
    // Write at large offset (500MB)
    const LONGLONG farOffset = 500LL * 1024 * 1024;
    const char farData[] = "FAR_DATA_AT_500MB";
    const DWORD farLen = static_cast<DWORD>(sizeof(farData) - 1);
    
    LARGE_INTEGER pos;
    pos.QuadPart = farOffset;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (!WriteFile(h, farData, farLen, &written, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL) {
            printf("  SKIPPED: Not enough disk space for sparse file test\n");
            CloseHandle(h);
            DeleteFileW(file);
            return true;
        }
        printf("WriteFile at far offset failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    printf("  Wrote '%s' at offset %lld\n", farData, farOffset);
    
    // Verify start data
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    char readBuf[32] = {0};
    DWORD bytesRead = 0;
    if (!ReadFile(h, readBuf, startLen, &bytesRead, NULL)) {
        printf("ReadFile at start failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (bytesRead != startLen || memcmp(readBuf, startData, startLen) != 0) {
        printf("Start data verification failed!\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    printf("  Start data verified OK\n");
    
    // Verify far data
    pos.QuadPart = farOffset;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    memset(readBuf, 0, sizeof(readBuf));
    if (!ReadFile(h, readBuf, farLen, &bytesRead, NULL)) {
        printf("ReadFile at far offset failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (bytesRead != farLen || memcmp(readBuf, farData, farLen) != 0) {
        printf("Far data verification failed!\n");
        printf("  Expected: '%s'\n", farData);
        printf("  Got: '%.*s'\n", static_cast<int>(bytesRead), readBuf);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    printf("  Far data verified OK\n");
    
    // Verify that middle is zeros (sparse region)
    const LONGLONG midOffset = 250LL * 1024 * 1024;  // 250MB
    pos.QuadPart = midOffset;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    char midBuf[64];
    if (!ReadFile(h, midBuf, sizeof(midBuf), &bytesRead, NULL)) {
        printf("ReadFile at middle failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    bool allZeros = true;
    for (DWORD i = 0; i < bytesRead; i++) {
        if (midBuf[i] != 0) {
            allZeros = false;
            printf("  Middle region not zero at offset %lld + %lu: got 0x%02x\n",
                   midOffset, i, static_cast<unsigned char>(midBuf[i]));
            break;
        }
    }
    
    if (allZeros) {
        printf("  Middle sparse region is correctly zeroed\n");
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    
    printf("Sparse file with large gaps test PASSED\n");
    return true;
}

//=============================================================================
// Test: reverseRead type consistency
// Tests that reverseRead handles large offsets correctly
// (This specifically tests the int32_t vs size_t type mixing in reverseRead)
//=============================================================================
bool Test_ReverseReadTypeConsistency(const WCHAR* file)
{
    printf("Testing type consistency in file operations...\n");
    
    // This test verifies that operations near type boundaries work correctly
    // The key boundaries are:
    // - 2^31 - 1 = 2,147,483,647 (INT32_MAX)
    // - Block boundaries near these values
    
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to create file\n");
        return false;
    }
    
    // Test with offsets that might cause issues with type mixing
    // Block size is typically 1024 bytes, data size 1016
    const size_t blockDataSize = 1016;
    
    // Test offset that would give a large shift value
    const LONGLONG testOffset = 100LL * 1024 * 1024 + 500;  // 100MB + 500 bytes (odd offset)
    const size_t testSize = 2048;  // Span multiple blocks
    
    printf("  Testing at offset %lld with size %zu...\n", testOffset, testSize);
    
    // Expand file
    LARGE_INTEGER pos;
    pos.QuadPart = testOffset + testSize;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (!SetEndOfFile(h)) {
        DWORD err = GetLastError();
        if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL) {
            printf("  SKIPPED: Not enough disk space\n");
            CloseHandle(h);
            DeleteFileW(file);
            return true;
        }
        printf("SetEndOfFile failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Seek to test offset
    pos.QuadPart = testOffset;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Write pattern
    std::vector<char> writeData(testSize);
    for (size_t i = 0; i < testSize; i++) {
        writeData[i] = static_cast<char>('A' + (i % 26));
    }
    
    DWORD written = 0;
    if (!WriteFile(h, writeData.data(), static_cast<DWORD>(testSize), &written, NULL)) {
        printf("WriteFile failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Read back
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("SetFilePointerEx for read failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    std::vector<char> readData(testSize);
    DWORD bytesRead = 0;
    if (!ReadFile(h, readData.data(), static_cast<DWORD>(testSize), &bytesRead, NULL)) {
        printf("ReadFile failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (bytesRead != testSize || memcmp(writeData.data(), readData.data(), testSize) != 0) {
        printf("Data verification failed!\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    printf("  Data verification OK\n");
    
    CloseHandle(h);
    DeleteFileW(file);
    
    printf("Type consistency test PASSED\n");
    return true;
}

//=============================================================================
// Test: Random access to file larger than 4GB
// Verifies that random access at various offsets works correctly
// and measures performance at different file positions
//=============================================================================
bool Test_RandomAccessLargeFile(const WCHAR* file)
{
    printf("Testing random access to large file (>4GB)...\n");
    
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  Failed to create file\n");
        return false;
    }
    
    // Create a sparse file of 5GB
    const LONGLONG fileSize = 5LL * 1024 * 1024 * 1024;  // 5GB
    
    // Enable sparse file to avoid actually allocating 5GB
    DWORD bytesReturned;
    if (!DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        printf("  Note: Could not set sparse attribute\n");
    }
    
    // Set file size to 5GB
    LARGE_INTEGER pos;
    pos.QuadPart = fileSize;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        printf("  SetFilePointerEx failed\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    if (!SetEndOfFile(h)) {
        DWORD err = GetLastError();
        if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL) {
            printf("  SKIPPED: Not enough disk space for 5GB sparse file\n");
            CloseHandle(h);
            DeleteFileW(file);
            return true;
        }
        printf("  SetEndOfFile failed: error %lu\n", err);
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Verify filesystem supports the requested file size
    LARGE_INTEGER actualSize;
    if (!GetFileSizeEx(h, &actualSize) || actualSize.QuadPart < fileSize) {
        printf("  SKIPPED: Filesystem does not support large files (requested %lld, got %lld)\n", fileSize, actualSize.QuadPart);
        CloseHandle(h);
        DeleteFileW(file);
        return true;
    }

    printf("  Created 5GB sparse file\n");
    
    // Test offsets at various positions including beyond 4GB
    const LONGLONG testOffsets[] = {
        0LL,                                    // Start
        1LL * 1024 * 1024 * 1024,              // 1GB
        2LL * 1024 * 1024 * 1024,              // 2GB
        3LL * 1024 * 1024 * 1024,              // 3GB
        4LL * 1024 * 1024 * 1024,              // 4GB (32-bit boundary)
        4LL * 1024 * 1024 * 1024 + 1,          // 4GB + 1 byte
        4LL * 1024 * 1024 * 1024 + 512,        // 4GB + 512 bytes
        4LL * 1024 * 1024 * 1024 + 4096,       // 4GB + 4KB
        fileSize - 4096                         // Near end
    };
    
    const size_t dataSize = 4096;  // 4KB test data
    std::vector<char> writeData(dataSize);
    std::vector<char> readData(dataSize);
    
    bool allOk = true;
    
    for (size_t i = 0; i < sizeof(testOffsets) / sizeof(testOffsets[0]); i++) {
        LONGLONG offset = testOffsets[i];
        
        // Skip if offset would exceed file size
        if (offset + dataSize > fileSize) {
            continue;
        }
        
        // Generate unique pattern for this offset
        for (size_t j = 0; j < dataSize; j++) {
            writeData[j] = static_cast<char>((offset + j) & 0xFF);
        }
        
        // Write at offset
        pos.QuadPart = offset;
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            printf("  FAILED: SetFilePointerEx to %lld\n", offset);
            allOk = false;
            continue;
        }
        
        DWORD written = 0;
        if (!WriteFile(h, writeData.data(), static_cast<DWORD>(dataSize), &written, NULL)) {
            printf("  FAILED: Write at offset %lld (error %lu)\n", offset, GetLastError());
            allOk = false;
            continue;
        }
        if (written != dataSize) {
            printf("  FAILED: Partial write at offset %lld (wrote %lu of %zu)\n", offset, written, dataSize);
            allOk = false;
            continue;
        }

        // Write at offset using overlapped I/O to ensure large offsets are handled
        {
            HANDLE hw = CreateFileW(file, GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING,
                                    FILE_FLAG_OVERLAPPED, NULL);
            if (hw == INVALID_HANDLE_VALUE) {
                printf("  FAILED: Could not open write handle for overlapped write (error %lu)\n", GetLastError());
                allOk = false;
                continue;
            }

            OVERLAPPED ov;
            ZeroMemory(&ov, sizeof(ov));
            ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);
            ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (!ov.hEvent) {
                CloseHandle(hw);
                printf("  FAILED: Could not create event for overlapped write\n");
                allOk = false;
                continue;
            }

            DWORD written = 0;
            BOOL ok = WriteFile(hw, writeData.data(), static_cast<DWORD>(dataSize), NULL, &ov);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    if (!GetOverlappedResult(hw, &ov, &written, TRUE)) {
                        printf("  FAILED: Overlapped write failed at offset %lld (error %lu)\n", offset, GetLastError());
                        CloseHandle(ov.hEvent);
                        CloseHandle(hw);
                        allOk = false;
                        continue;
                    }
                } else {
                    printf("  FAILED: WriteFile failed at offset %lld (error %lu)\n", offset, err);
                    CloseHandle(ov.hEvent);
                    CloseHandle(hw);
                    allOk = false;
                    continue;
                }
            } else {
                // Synchronous completion, retrieve result
                if (!GetOverlappedResult(hw, &ov, &written, TRUE)) {
                    printf("  FAILED: GetOverlappedResult failed after synchronous write at %lld (error %lu)\n", offset, GetLastError());
                    CloseHandle(ov.hEvent);
                    CloseHandle(hw);
                    allOk = false;
                    continue;
                }
            }

            CloseHandle(ov.hEvent);
            CloseHandle(hw);

            if (written != dataSize) {
                printf("  FAILED: Partial overlapped write at offset %lld (wrote %lu of %zu)\n", offset, written, dataSize);
                allOk = false;
                continue;
            }
        }

        // Ensure data is flushed to underlying filesystem before reading
        FlushFileBuffers(h);
        
        // Use a separate handle for reading to avoid caching/visibility issues
        HANDLE hr = OpenTestFile(file, GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (hr == INVALID_HANDLE_VALUE) {
            printf("  FAILED: Could not open separate read handle (error %lu)\n", GetLastError());
            allOk = false;
            continue;
        }
        
        // Read back using separate handle
        if (!SetFilePointerEx(hr, pos, NULL, FILE_BEGIN)) {
            printf("  FAILED: SetFilePointerEx for read at %lld\n", offset);
            CloseHandle(hr);
            allOk = false;
            continue;
        }
        
        DWORD bytesRead = 0;
        if (!ReadFile(hr, readData.data(), static_cast<DWORD>(dataSize), &bytesRead, NULL)) {
            printf("  FAILED: Read at offset %lld (error %lu)\n", offset, GetLastError());
            CloseHandle(hr);
            allOk = false;
            continue;
        }
        if (bytesRead != dataSize) {
            printf("  FAILED: Partial read at offset %lld (read %lu of %zu)\n", offset, bytesRead, dataSize);
            CloseHandle(hr);
            allOk = false;
            continue;
        }
        CloseHandle(hr);
        
        // Verify
        if (bytesRead != dataSize || memcmp(writeData.data(), readData.data(), dataSize) != 0) {
            printf("  FAILED: Data mismatch at offset %lld\n", offset);
            // Dump first 64 bytes for diagnosis
            size_t dumpLen = std::min<size_t>(64, dataSize);
            printf("    Written: ");
            for (size_t k = 0; k < dumpLen; k++) printf("%02x ", static_cast<unsigned char>(writeData[k]));
            printf("\n");
            printf("    Read   : ");
            for (size_t k = 0; k < dumpLen; k++) printf("%02x ", static_cast<unsigned char>(readData[k]));
            printf("\n");
            allOk = false;
            continue;
        }
        
        // Print offset in human-readable format
        if (offset >= 1024LL * 1024 * 1024) {
            printf("  Offset %.2f GB: OK\n", static_cast<double>(offset) / (1024.0 * 1024.0 * 1024.0));
        } else if (offset >= 1024 * 1024) {
            printf("  Offset %.2f MB: OK\n", static_cast<double>(offset) / (1024.0 * 1024.0));
        } else {
            printf("  Offset %lld bytes: OK\n", offset);
        }
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    
    if (allOk) {
        printf("Random access to large file test PASSED\n");
    } else {
        printf("Random access to large file test FAILED\n");
    }
    
    return allOk;
}

//=============================================================================
// Test: Performance at different file positions
// Measures if there's any performance degradation at high offsets
//=============================================================================
bool Test_PerformanceAtHighOffsets(const WCHAR* file)
{
    printf("Testing performance at different file offsets...\n");
    
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  Failed to create file\n");
        return false;
    }
    
    // Create sparse file
    const LONGLONG fileSize = 5LL * 1024 * 1024 * 1024;  // 5GB
    
    DWORD bytesReturned;
    DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &bytesReturned, NULL);
    
    LARGE_INTEGER pos;
    pos.QuadPart = fileSize;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
        DWORD err = GetLastError();
        if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL) {
            printf("  SKIPPED: Not enough disk space\n");
            CloseHandle(h);
            DeleteFileW(file);
            return true;
        }
        printf("  Failed to create sparse file\n");
        CloseHandle(h);
        DeleteFileW(file);
        return false;
    }
    
    // Test positions
    const LONGLONG testPositions[] = {
        0LL,
        1LL * 1024 * 1024 * 1024,   // 1GB
        4LL * 1024 * 1024 * 1024,   // 4GB
    };
    
    const size_t ioSize = 64 * 1024;  // 64KB I/O
    const int iterations = 100;
    std::vector<char> buffer(ioSize);
    
    // Fill buffer with pattern
    for (size_t i = 0; i < ioSize; i++) {
        buffer[i] = static_cast<char>(i & 0xFF);
    }
    
    for (LONGLONG testPos : testPositions) {
        // Write test
        pos.QuadPart = testPos;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        
        auto writeStart = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; i++) {
            DWORD written;
            WriteFile(h, buffer.data(), static_cast<DWORD>(ioSize), &written, NULL);
        }
        FlushFileBuffers(h);
        
        auto writeEnd = std::chrono::high_resolution_clock::now();
        auto writeDuration = std::chrono::duration_cast<std::chrono::microseconds>(writeEnd - writeStart);
        
        // Read test
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        
        auto readStart = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; i++) {
            DWORD bytesRead;
            ReadFile(h, buffer.data(), static_cast<DWORD>(ioSize), &bytesRead, NULL);
        }
        
        auto readEnd = std::chrono::high_resolution_clock::now();
        auto readDuration = std::chrono::duration_cast<std::chrono::microseconds>(readEnd - readStart);
        
        double writeMBps = (iterations * ioSize) / (writeDuration.count() / 1000000.0) / (1024 * 1024);
        double readMBps = (iterations * ioSize) / (readDuration.count() / 1000000.0) / (1024 * 1024);
        
        if (testPos >= 1024LL * 1024 * 1024) {
            printf("  At %.0f GB: Write %.1f MB/s, Read %.1f MB/s\n",
                   static_cast<double>(testPos) / (1024.0 * 1024.0 * 1024.0),
                   writeMBps, readMBps);
        } else {
            printf("  At 0 GB: Write %.1f MB/s, Read %.1f MB/s\n", writeMBps, readMBps);
        }
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    
    printf("Performance at high offsets test completed\n");
    return true;
}
