// test_edge_cases.cpp - Edge case tests for previously fixed bugs
// These tests verify fixes for:
// 1. len==0 underflow in read/write operations
// 2. _setLength() boundary block handling
// 3. Block boundary edge cases during file expansion/shrinking
// 4. Multi-handle read/write synchronization
// 5. Thread safety and race conditions

#include "test_common.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

//=============================================================================
// Test: Zero-length read request (fixed underflow bug)
// Previously, len=0 caused (off + len - 1) / blockDataSize to underflow
//=============================================================================
bool Test_ZeroLengthRead(const WCHAR* file)
{
    printf("Testing zero-length read request...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    // Create a file with some data
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const char testData[] = "Test data for zero-length read test";
    DWORD written = 0;
    if (!WriteFile(h, testData, sizeof(testData) - 1, &written, NULL)) {
        PrintLastError("WriteFile");
        CloseHandle(h);
        return false;
    }
    
    // Seek back to beginning
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx");
        CloseHandle(h);
        return false;
    }
    
    // Attempt zero-length read - this should succeed with 0 bytes read
    char buffer[16];
    DWORD bytesRead = 0xDEADBEEF;  // Initialize to detect if it's modified
    
    BOOL result = ReadFile(h, buffer, 0, &bytesRead, NULL);
    if (!result) {
        PrintLastError("ReadFile (zero-length)");
        CloseHandle(h);
        return false;
    }
    
    printf("Zero-length read at offset 0: returned %lu bytes (expected 0)\n", 
           static_cast<unsigned long>(bytesRead));
    
    if (bytesRead != 0) {
        printf("ERROR: Expected 0 bytes, got %lu\n", static_cast<unsigned long>(bytesRead));
        CloseHandle(h);
        return false;
    }
    
    // Try zero-length read at various offsets
    LONGLONG offsets[] = {0, 10, 100, 1000, 1016, 1024, 2048};
    for (LONGLONG offset : offsets) {
        pos.QuadPart = offset;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        
        bytesRead = 0xDEADBEEF;
        result = ReadFile(h, buffer, 0, &bytesRead, NULL);
        if (!result) {
            printf("Zero-length read at offset %lld failed\n", offset);
            PrintLastError("ReadFile");
            CloseHandle(h);
            return false;
        }
        
        if (bytesRead != 0) {
            printf("ERROR: Zero-length read at offset %lld returned %lu bytes\n", 
                   offset, static_cast<unsigned long>(bytesRead));
            CloseHandle(h);
            return false;
        }
        printf("  Offset %lld: OK\n", offset);
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("Zero-length read test PASSED\n");
    return true;
}

//=============================================================================
// Test: Zero-length write request (fixed underflow bug)
//=============================================================================
bool Test_ZeroLengthWrite(const WCHAR* file)
{
    printf("Testing zero-length write request...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    // Create a file
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    // Write some initial data
    const char testData[] = "Initial data";
    DWORD written = 0;
    if (!WriteFile(h, testData, sizeof(testData) - 1, &written, NULL)) {
        PrintLastError("WriteFile (initial)");
        CloseHandle(h);
        return false;
    }
    
    // Get file size
    LARGE_INTEGER fileSize;
    GetFileSizeEx(h, &fileSize);
    printf("Initial file size: %lld bytes\n", fileSize.QuadPart);
    
    // Try zero-length write at various offsets
    char buffer[16] = "DUMMY";
    LONGLONG offsets[] = {0, 5, 10, 100, 1000, 1016, 1024};
    
    for (LONGLONG offset : offsets) {
        LARGE_INTEGER pos;
        pos.QuadPart = offset;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        
        DWORD bytesWritten = 0xDEADBEEF;
        BOOL result = WriteFile(h, buffer, 0, &bytesWritten, NULL);
        if (!result) {
            printf("Zero-length write at offset %lld failed\n", offset);
            PrintLastError("WriteFile");
            CloseHandle(h);
            return false;
        }
        
        if (bytesWritten != 0) {
            printf("ERROR: Zero-length write at offset %lld returned %lu bytes written\n", 
                   offset, static_cast<unsigned long>(bytesWritten));
            CloseHandle(h);
            return false;
        }
        printf("  Offset %lld: OK\n", offset);
    }
    
    // Verify file size unchanged
    GetFileSizeEx(h, &fileSize);
    printf("Final file size: %lld bytes (should be %zu)\n", 
           fileSize.QuadPart, sizeof(testData) - 1);
    
    // Verify data integrity
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
    
    char readBuffer[64] = {0};
    DWORD bytesRead = 0;
    ReadFile(h, readBuffer, sizeof(testData) - 1, &bytesRead, NULL);
    
    if (memcmp(readBuffer, testData, sizeof(testData) - 1) != 0) {
        printf("ERROR: Data integrity check failed after zero-length writes\n");
        printf("Expected: '%s'\n", testData);
        printf("Got: '%.*s'\n", (int)bytesRead, readBuffer);
        CloseHandle(h);
        return false;
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("Zero-length write test PASSED\n");
    return true;
}

//=============================================================================
// Test: SetEndOfFile boundary block handling
// Tests the fix for _setLength() with partial blocks
//=============================================================================
bool Test_SetEndOfFileBoundaryBlock(const WCHAR* file)
{
    printf("Testing SetEndOfFile boundary block handling...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    // EncFS uses 1024-byte blocks with 8-byte header = 1016 bytes of data per block
    const size_t BLOCK_DATA_SIZE = 1016;
    
    // Write data spanning multiple blocks
    std::vector<char> testData(BLOCK_DATA_SIZE * 3, 0);
    for (size_t i = 0; i < testData.size(); i++) {
        testData[i] = 'A' + (i % 26);
    }
    
    DWORD written = 0;
    if (!WriteFile(h, testData.data(), static_cast<DWORD>(testData.size()), &written, NULL)) {
        PrintLastError("WriteFile");
        CloseHandle(h);
        return false;
    }
    printf("Wrote %lu bytes (3 full blocks)\n", static_cast<unsigned long>(written));
    
    // Test 1: Truncate to middle of second block
    size_t truncateSize = BLOCK_DATA_SIZE + 500;  // Middle of second block
    LARGE_INTEGER pos;
    pos.QuadPart = truncateSize;
    
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx");
        CloseHandle(h);
        return false;
    }
    if (!SetEndOfFile(h)) {
        PrintLastError("SetEndOfFile");
        CloseHandle(h);
        return false;
    }
    printf("Truncated to %zu bytes (middle of second block)\n", truncateSize);
    
    // Verify file size
    LARGE_INTEGER fileSize;
    GetFileSizeEx(h, &fileSize);
    if (fileSize.QuadPart != (LONGLONG)truncateSize) {
        printf("ERROR: File size after truncate is %lld, expected %zu\n", 
               fileSize.QuadPart, truncateSize);
        CloseHandle(h);
        return false;
    }
    
    // Read back and verify data integrity
    pos.QuadPart = 0;
    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
    
    std::vector<char> readBuffer(truncateSize, 0);
    DWORD bytesRead = 0;
    if (!ReadFile(h, readBuffer.data(), static_cast<DWORD>(truncateSize), &bytesRead, NULL)) {
        PrintLastError("ReadFile");
        CloseHandle(h);
        return false;
    }
    
    if (bytesRead != truncateSize) {
        printf("ERROR: Read %lu bytes, expected %zu\n", 
               static_cast<unsigned long>(bytesRead), truncateSize);
        CloseHandle(h);
        return false;
    }
    
    // Verify the data matches
    if (memcmp(readBuffer.data(), testData.data(), truncateSize) != 0) {
        printf("ERROR: Data mismatch after truncate!\n");
        // Find first difference
        for (size_t i = 0; i < truncateSize; i++) {
            if (readBuffer[i] != testData[i]) {
                printf("First difference at offset %zu: expected 0x%02X, got 0x%02X\n",
                       i, (unsigned char)testData[i], (unsigned char)readBuffer[i]);
                break;
            }
        }
        CloseHandle(h);
        return false;
    }
    printf("Data integrity verified after truncate\n");
    
    // Test 2: Expand the file
    size_t expandSize = BLOCK_DATA_SIZE * 2 + 100;
    pos.QuadPart = expandSize;
    
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx");
        CloseHandle(h);
        return false;
    }
    if (!SetEndOfFile(h)) {
        PrintLastError("SetEndOfFile (expand)");
        CloseHandle(h);
        return false;
    }
    printf("Expanded to %zu bytes\n", expandSize);
    
    // Verify file size
    GetFileSizeEx(h, &fileSize);
    if (fileSize.QuadPart != (LONGLONG)expandSize) {
        printf("ERROR: File size after expand is %lld, expected %zu\n", 
               fileSize.QuadPart, expandSize);
        CloseHandle(h);
        return false;
    }
    
    // Read and verify original data is preserved
    pos.QuadPart = 0;
    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
    
    readBuffer.assign(truncateSize, 0);
    bytesRead = 0;
    if (!ReadFile(h, readBuffer.data(), static_cast<DWORD>(truncateSize), &bytesRead, NULL)) {
        PrintLastError("ReadFile (after expand)");
        CloseHandle(h);
        return false;
    }
    
    if (memcmp(readBuffer.data(), testData.data(), truncateSize) != 0) {
        printf("ERROR: Original data corrupted after expand!\n");
        CloseHandle(h);
        return false;
    }
    printf("Original data preserved after expand\n");
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("SetEndOfFile boundary block test PASSED\n");
    return true;
}

//=============================================================================
// Test: File expansion with partial block at end
// Tests the fixed _setLength() expansion logic
//=============================================================================
bool Test_FileExpansionPartialBlock(const WCHAR* file)
{
    printf("Testing file expansion with partial block at end...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const size_t BLOCK_DATA_SIZE = 1016;
    
    // Write a small amount of data
    const char initialData[] = "Initial";
    DWORD written = 0;
    WriteFile(h, initialData, sizeof(initialData) - 1, &written, NULL);
    printf("Wrote %lu initial bytes\n", static_cast<unsigned long>(written));
    
    // Expand to various sizes that create partial blocks
    size_t expandSizes[] = {
        100,                          // Small, same block
        BLOCK_DATA_SIZE - 10,         // Almost full block
        BLOCK_DATA_SIZE,              // Exactly one block
        BLOCK_DATA_SIZE + 10,         // Just past block boundary
        BLOCK_DATA_SIZE * 2 - 1,      // Just before second block boundary
        BLOCK_DATA_SIZE * 2,          // Exactly two blocks
        BLOCK_DATA_SIZE * 2 + 500,    // Middle of third block
    };
    
    for (size_t targetSize : expandSizes) {
        // Expand to target size
        LARGE_INTEGER pos;
        pos.QuadPart = targetSize;
        
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            printf("SetFilePointerEx to %zu failed\n", targetSize);
            PrintLastError("SetFilePointerEx");
            CloseHandle(h);
            return false;
        }
        
        if (!SetEndOfFile(h)) {
            printf("SetEndOfFile to %zu failed\n", targetSize);
            PrintLastError("SetEndOfFile");
            CloseHandle(h);
            return false;
        }
        
        // Verify file size
        LARGE_INTEGER fileSize;
        GetFileSizeEx(h, &fileSize);
        
        if (fileSize.QuadPart != (LONGLONG)targetSize) {
            printf("ERROR: Expand to %zu resulted in size %lld\n", 
                   targetSize, fileSize.QuadPart);
            CloseHandle(h);
            return false;
        }
        
        printf("  Expanded to %zu bytes: OK\n", targetSize);
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("File expansion partial block test PASSED\n");
    return true;
}

//=============================================================================
// Test: Rapid truncate and write operations
// Simulates VS build pattern that was causing issues
//=============================================================================
bool Test_RapidTruncateWrite(const WCHAR* file)
{
    printf("Testing rapid truncate and write operations...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    const int ITERATIONS = 50;
    
    for (int i = 0; i < ITERATIONS; i++) {
        // Open with CREATE_ALWAYS (truncates existing)
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Iteration %d: CreateFile failed\n", i);
            return false;
        }
        
        // Write variable amount of data
        size_t dataSize = 100 + (i * 77) % 3000;  // Vary between 100 and ~3000 bytes
        std::vector<char> data(dataSize);
        for (size_t j = 0; j < dataSize; j++) {
            data[j] = 'A' + ((i + j) % 26);
        }
        
        DWORD written = 0;
        if (!WriteFile(h, data.data(), static_cast<DWORD>(dataSize), &written, NULL)) {
            printf("Iteration %d: WriteFile failed\n", i);
            PrintLastError("WriteFile");
            CloseHandle(h);
            return false;
        }
        
        if (written != dataSize) {
            printf("Iterations %d: Wrote %lu, expected %zu\n", 
                   i, static_cast<unsigned long>(written), dataSize);
            CloseHandle(h);
            return false;
        }
        
        // Immediately read back
        LARGE_INTEGER pos;
        pos.QuadPart = 0;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        
        std::vector<char> readBack(dataSize);
        DWORD bytesRead = 0;
        if (!ReadFile(h, readBack.data(), static_cast<DWORD>(dataSize), &bytesRead, NULL)) {
            printf("Iteration %d: ReadFile failed\n", i);
            PrintLastError("ReadFile");
            CloseHandle(h);
            return false;
        }
        
        if (bytesRead != dataSize || memcmp(data.data(), readBack.data(), dataSize) != 0) {
            printf("Iteration %d: Data mismatch!\n", i);
            CloseHandle(h);
            return false;
        }
        
        CloseHandle(h);
        
        if ((i + 1) % 10 == 0) {
            printf("  Completed %d iterations\n", i + 1);
        }
    }
    
    DeleteFileW(file);
    printf("Rapid truncate/write test PASSED (%d iterations)\n", ITERATIONS);
    return true;
}

//=============================================================================
// Test: Read at exact block boundaries
// Verifies correct handling of reads that align with encryption block boundaries
//=============================================================================
bool Test_ReadAtBlockBoundaries(const WCHAR* file)
{
    printf("Testing reads at exact block boundaries...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const size_t BLOCK_DATA_SIZE = 1016;
    const size_t FILE_SIZE = BLOCK_DATA_SIZE * 5;
    
    // Create test data with recognizable pattern
    std::vector<char> testData(FILE_SIZE);
    for (size_t i = 0; i < FILE_SIZE; i++) {
        testData[i] = static_cast<char>('A' + (i / BLOCK_DATA_SIZE));
    }
    
    DWORD written = 0;
    if (!WriteFile(h, testData.data(), static_cast<DWORD>(FILE_SIZE), &written, NULL)) {
        PrintLastError("WriteFile");
        CloseHandle(h);
        return false;
    }
    printf("Wrote %lu bytes spanning 5 blocks\n", static_cast<unsigned long>(written));
    
    // Test reads at various boundary positions
    struct TestCase {
        size_t offset;
        size_t length;
        const char* description;
    };
    
    TestCase tests[] = {
        {0, BLOCK_DATA_SIZE, "Exactly one block from start"},
        {BLOCK_DATA_SIZE, BLOCK_DATA_SIZE, "One block starting at block 2"},
        {BLOCK_DATA_SIZE - 10, 20, "Spanning block 1-2 boundary"},
        {BLOCK_DATA_SIZE * 2, 1, "Single byte at block 3 start"},
        {BLOCK_DATA_SIZE - 1, 2, "Two bytes spanning boundary"},
        {BLOCK_DATA_SIZE * 2 - 1, BLOCK_DATA_SIZE + 2, "Spanning blocks 2-3-4"},
        {0, FILE_SIZE, "Entire file"},
        {BLOCK_DATA_SIZE * 4, BLOCK_DATA_SIZE, "Last block"},
    };
    
    for (const auto& test : tests) {
        LARGE_INTEGER pos;
        pos.QuadPart = test.offset;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        
        std::vector<char> buffer(test.length);
        DWORD bytesRead = 0;
        
        if (!ReadFile(h, buffer.data(), static_cast<DWORD>(test.length), &bytesRead, NULL)) {
            printf("FAILED: %s\n", test.description);
            PrintLastError("ReadFile");
            CloseHandle(h);
            return false;
        }
        
        if (bytesRead != test.length) {
            printf("FAILED: %s - read %lu, expected %zu\n", 
                   test.description, static_cast<unsigned long>(bytesRead), test.length);
            CloseHandle(h);
            return false;
        }
        
        // Verify data
        if (memcmp(buffer.data(), testData.data() + test.offset, test.length) != 0) {
            printf("FAILED: %s - data mismatch\n", test.description);
            CloseHandle(h);
            return false;
        }
        
        printf("  %s: OK\n", test.description);
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("Block boundary read test PASSED\n");
    return true;
}

//=============================================================================
// Test: Write at exact block boundaries
//=============================================================================
bool Test_WriteAtBlockBoundaries(const WCHAR* file)
{
    printf("Testing writes at exact block boundaries...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const size_t BLOCK_DATA_SIZE = 1016;
    
    // Pre-fill file with zeros
    std::vector<char> zeros(BLOCK_DATA_SIZE * 4, 0);
    DWORD written = 0;
    WriteFile(h, zeros.data(), static_cast<DWORD>(zeros.size()), &written, NULL);
    
    // Test writes at various boundary positions
    struct TestCase {
        size_t offset;
        char fillChar;
        size_t length;
        const char* description;
    };
    
    TestCase tests[] = {
        {0, 'A', BLOCK_DATA_SIZE, "Overwrite first block"},
        {BLOCK_DATA_SIZE, 'B', BLOCK_DATA_SIZE, "Overwrite second block"},
        {BLOCK_DATA_SIZE - 10, 'C', 20, "Write spanning block 1-2 boundary"},
        {BLOCK_DATA_SIZE * 2 + 100, 'D', 50, "Write in middle of block 3"},
        {BLOCK_DATA_SIZE * 3 - 5, 'E', 10, "Write spanning block 3-4 boundary"},
    };
    
    for (const auto& test : tests) {
        LARGE_INTEGER pos;
        pos.QuadPart = test.offset;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        
        std::vector<char> data(test.length, test.fillChar);
        DWORD bytesWritten = 0;
        
        if (!WriteFile(h, data.data(), static_cast<DWORD>(test.length), &bytesWritten, NULL)) {
            printf("FAILED: %s\n", test.description);
            PrintLastError("WriteFile");
            CloseHandle(h);
            return false;
        }
        
        if (bytesWritten != test.length) {
            printf("FAILED: %s - wrote %lu, expected %zu\n", 
                   test.description, static_cast<unsigned long>(bytesWritten), test.length);
            CloseHandle(h);
            return false;
        }
        
        // Verify by reading back
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        std::vector<char> readBack(test.length);
        DWORD bytesRead = 0;
        ReadFile(h, readBack.data(), static_cast<DWORD>(test.length), &bytesRead, NULL);
        
        if (memcmp(data.data(), readBack.data(), test.length) != 0) {
            printf("FAILED: %s - data verification failed\n", test.description);
            CloseHandle(h);
            return false;
        }
        
        printf("  %s: OK\n", test.description);
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("Block boundary write test PASSED\n");
    return true;
}

//=============================================================================
// Test: Truncate to zero then immediate rewrite (ZIP file pattern)
// This pattern was causing "failed to read at offset 0" errors
//=============================================================================
bool Test_TruncateZeroImmediateRewrite(const WCHAR* file)
{
    printf("Testing truncate to zero then immediate rewrite (ZIP pattern)...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    const int ITERATIONS = 20;
    
    for (int i = 0; i < ITERATIONS; i++) {
        // Create file with content
        {
            HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
            if (h == INVALID_HANDLE_VALUE) {
                printf("Iteration %d: Initial create failed\n", i);
                return false;
            }
            
            std::vector<char> data(2000 + i * 100, 'X');
            DWORD written = 0;
            WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, NULL);
            CloseHandle(h);
        }
        
        // Open, truncate to zero, then write new content
        {
            HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                    TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL);
            if (h == INVALID_HANDLE_VALUE) {
                printf("Iteration %d: Truncate open failed\n", i);
                return false;
            }
            
            // Verify size is 0
            LARGE_INTEGER fileSize;
            GetFileSizeEx(h, &fileSize);
            if (fileSize.QuadPart != 0) {
                printf("Iteration %d: File not truncated, size=%lld\n", i, fileSize.QuadPart);
                CloseHandle(h);
                return false;
            }
            
            // Write new content
            std::vector<char> newData(1500 + i * 50, 'Y');
            DWORD written = 0;
            if (!WriteFile(h, newData.data(), static_cast<DWORD>(newData.size()), &written, NULL)) {
                printf("Iteration %d: Write after truncate failed\n", i);
                PrintLastError("WriteFile");
                CloseHandle(h);
                return false;
            }
            
            // Immediate read back at offset 0
            LARGE_INTEGER pos;
            pos.QuadPart = 0;
            SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
            
            std::vector<char> readBack(newData.size());
            DWORD bytesRead = 0;
            if (!ReadFile(h, readBack.data(), static_cast<DWORD>(newData.size()), &bytesRead, NULL)) {
                printf("Iteration %d: Read at offset 0 failed (ZIP pattern issue!)\n", i);
                PrintLastError("ReadFile");
                CloseHandle(h);
                return false;
            }
            
            if (bytesRead != newData.size()) {
                printf("Iteration %d: Read %lu bytes, expected %zu\n", 
                       i, static_cast<unsigned long>(bytesRead), newData.size());
                CloseHandle(h);
                return false;
            }
            
            if (memcmp(newData.data(), readBack.data(), newData.size()) != 0) {
                printf("Iteration %d: Data mismatch after truncate+write+read\n", i);
                CloseHandle(h);
                return false;
            }
            
            CloseHandle(h);
        }
        
        if ((i + 1) % 5 == 0) {
            printf("  Completed %d iterations\n", i + 1);
        }
    }
    
    DeleteFileW(file);
    printf("Truncate zero immediate rewrite test PASSED (%d iterations)\n", ITERATIONS);
    return true;
}

//=============================================================================
// Test: Write/read with separate handles (multi-handle sync)
// Verifies that data written via one handle is immediately readable via another
//=============================================================================
bool Test_WriteReadSeparateHandles(const WCHAR* file)
{
    printf("Testing write/read with separate handles...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    const int ITERATIONS = 30;
    
    for (int i = 0; i < ITERATIONS; i++) {
        size_t dataSize = 500 + (i * 137) % 2500;
        std::vector<char> testData(dataSize);
        for (size_t j = 0; j < dataSize; j++) {
            testData[j] = 'A' + ((i + j) % 26);
        }
        
        // Handle 1: Create and write
        {
            HANDLE h1 = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
            if (h1 == INVALID_HANDLE_VALUE) {
                printf("Iteration %d: Create failed\n", i);
                return false;
            }
            
            DWORD written = 0;
            if (!WriteFile(h1, testData.data(), static_cast<DWORD>(dataSize), &written, NULL)) {
                printf("Iteration %d: Write failed\n", i);
                PrintLastError("WriteFile");
                CloseHandle(h1);
                return false;
            }
            
            // Flush to ensure data is visible
            FlushFileBuffers(h1);
            CloseHandle(h1);
        }
        
        // Handle 2: Open and read
        {
            HANDLE h2 = OpenTestFile(file, GENERIC_READ, 
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
            if (h2 == INVALID_HANDLE_VALUE) {
                printf("Iteration %d: Open for read failed\n", i);
                PrintLastError("CreateFile");
                return false;
            }
            
            std::vector<char> readBack(dataSize);
            DWORD bytesRead = 0;
            if (!ReadFile(h2, readBack.data(), static_cast<DWORD>(dataSize), &bytesRead, NULL)) {
                printf("Iteration %d: Read failed\n", i);
                PrintLastError("ReadFile");
                CloseHandle(h2);
                return false;
            }
            
            if (bytesRead != dataSize || memcmp(testData.data(), readBack.data(), dataSize) != 0) {
                printf("Iteration %d: Data mismatch!\n", i);
                CloseHandle(h2);
                return false;
            }
            
            CloseHandle(h2);
        }
        
        // Clean up for next iteration
        DeleteFileW(file);
        
        if ((i + 1) % 10 == 0) {
            printf("  Completed %d iterations\n", i + 1);
        }
    }
    
    printf("Write/read with separate handles test PASSED (%d iterations)\n", ITERATIONS);
    return true;
}

//=============================================================================
// Test: Concurrent read handles while writing
// Verifies that multiple readers can access a file being written
//=============================================================================
bool Test_ConcurrentReadWhileWrite(const WCHAR* file)
{
    printf("Testing concurrent read while writing...\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    // Create initial file
    HANDLE hWrite = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (hWrite == INVALID_HANDLE_VALUE) {
        printf("Failed to create file\n");
        return false;
    }
    
    const size_t BLOCK_DATA_SIZE = 1016;
    const size_t INITIAL_SIZE = BLOCK_DATA_SIZE * 2;
    
    // Write initial data
    std::vector<char> initialData(INITIAL_SIZE, 'I');
    DWORD written = 0;
    if (!WriteFile(hWrite, initialData.data(), static_cast<DWORD>(INITIAL_SIZE), &written, NULL)) {
        PrintLastError("WriteFile (initial)");
        CloseHandle(hWrite);
        return false;
    }
    FlushFileBuffers(hWrite);
    printf("Wrote %lu initial bytes\n", static_cast<unsigned long>(written));
    
    // Open a read handle
    HANDLE hRead = OpenTestFile(file, GENERIC_READ, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (hRead == INVALID_HANDLE_VALUE) {
        printf("Failed to open read handle\n");
        PrintLastError("CreateFile");
        CloseHandle(hWrite);
        return false;
    }
    
    // Verify read handle can read initial data
    std::vector<char> readBuffer(INITIAL_SIZE);
    DWORD bytesRead = 0;
    if (!ReadFile(hRead, readBuffer.data(), static_cast<DWORD>(INITIAL_SIZE), &bytesRead, NULL)) {
        printf("Failed to read initial data\n");
        PrintLastError("ReadFile");
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    
    if (bytesRead != INITIAL_SIZE || memcmp(readBuffer.data(), initialData.data(), INITIAL_SIZE) != 0) {
        printf("Initial data mismatch\n");
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    printf("  Read handle verified initial data\n");
    
    // Write additional data via write handle
    LARGE_INTEGER pos;
    pos.QuadPart = INITIAL_SIZE;
    SetFilePointerEx(hWrite, pos, NULL, FILE_BEGIN);
    
    std::vector<char> additionalData(BLOCK_DATA_SIZE, 'A');
    if (!WriteFile(hWrite, additionalData.data(), static_cast<DWORD>(BLOCK_DATA_SIZE), &written, NULL)) {
        printf("Failed to write additional data\n");
        PrintLastError("WriteFile");
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    FlushFileBuffers(hWrite);
    printf("  Wrote %lu additional bytes via write handle\n", static_cast<unsigned long>(written));
    
    // Read handle should be able to read the new data
    SetFilePointerEx(hRead, pos, NULL, FILE_BEGIN);
    readBuffer.assign(BLOCK_DATA_SIZE, 0);
    bytesRead = 0;
    if (!ReadFile(hRead, readBuffer.data(), static_cast<DWORD>(BLOCK_DATA_SIZE), &bytesRead, NULL)) {
        printf("Failed to read additional data via read handle\n");
        PrintLastError("ReadFile");
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    
    if (bytesRead != BLOCK_DATA_SIZE) {
        printf("Read %lu bytes, expected %zu\n", 
               static_cast<unsigned long>(bytesRead), BLOCK_DATA_SIZE);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    
    if (memcmp(readBuffer.data(), additionalData.data(), BLOCK_DATA_SIZE) != 0) {
        printf("Additional data mismatch!\n");
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    printf("  Read handle verified additional data\n");
    
    CloseHandle(hRead);
    CloseHandle(hWrite);
    DeleteFileW(file);
    
    printf("Concurrent read while write test PASSED\n");
    return true;
}

//=============================================================================
// Test: Read beyond end of file
// Verifies that reading beyond EOF returns 0 bytes without error
//=============================================================================
bool Test_ReadBeyondEOF(const WCHAR* file)
{
    printf("Testing read beyond EOF...\n");
    
    DeleteFileW(file);
    
    // Create file with known size
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const char testData[] = "Short file content";
    DWORD written = 0;
    WriteFile(h, testData, sizeof(testData) - 1, &written, NULL);
    
    size_t fileSize = sizeof(testData) - 1;
    
    // Test reading at various positions beyond EOF
    struct TestCase {
        LONGLONG offset;
        DWORD requestLen;
        const char* description;
    };
    
    TestCase tests[] = {
        {(LONGLONG)fileSize, 100, "At exact EOF"},
        {(LONGLONG)fileSize + 1, 100, "1 byte past EOF"},
        {(LONGLONG)fileSize + 1000, 100, "1000 bytes past EOF"},
        {10000, 100, "Way beyond EOF"},
    };
    
    for (const auto& test : tests) {
        LARGE_INTEGER pos;
        pos.QuadPart = test.offset;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        
        char buffer[256];
        DWORD bytesRead = 0xDEADBEEF;
        
        BOOL result = ReadFile(h, buffer, test.requestLen, &bytesRead, NULL);
        if (!result) {
            printf("FAILED: %s - ReadFile returned error %d\n", 
                   test.description, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        if (bytesRead != 0) {
            printf("FAILED: %s - Expected 0 bytes, got %lu\n", 
                   test.description, static_cast<unsigned long>(bytesRead));
            CloseHandle(h);
            return false;
        }
        
        printf("  %s: OK (0 bytes read)\n", test.description);
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("Read beyond EOF test PASSED\n");
    return true;
}

//=============================================================================
// Test: Empty file operations
// Verifies that operations on empty files work correctly
//=============================================================================
bool Test_EmptyFileOperations(const WCHAR* file)
{
    printf("Testing empty file operations...\n");
    
    DeleteFileW(file);
    
    // Create empty file
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    // Test 1: Read from empty file
    char buffer[256];
    DWORD bytesRead = 0xDEADBEEF;
    if (!ReadFile(h, buffer, sizeof(buffer), &bytesRead, NULL)) {
        printf("FAILED: Read from empty file returned error %d\n", GetLastError());
        CloseHandle(h);
        return false;
    }
    if (bytesRead != 0) {
        printf("FAILED: Read from empty file returned %lu bytes\n", 
               static_cast<unsigned long>(bytesRead));
        CloseHandle(h);
        return false;
    }
    printf("  Read from empty file: OK\n");
    
    // Test 2: GetFileSize on empty file
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(h, &fileSize) || fileSize.QuadPart != 0) {
        printf("FAILED: GetFileSize on empty file: %lld\n", fileSize.QuadPart);
        CloseHandle(h);
        return false;
    }
    printf("  GetFileSize on empty file: OK\n");
    
    // Test 3: SetEndOfFile to 0 on empty file
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
    if (!SetEndOfFile(h)) {
        printf("FAILED: SetEndOfFile(0) on empty file: %d\n", GetLastError());
        CloseHandle(h);
        return false;
    }
    printf("  SetEndOfFile(0) on empty file: OK\n");
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("Empty file operations test PASSED\n");
    return true;
}

//=============================================================================
// Test: High concurrency file access (stress test for thread safety)
// This tests for race conditions in IV handling and block encryption
//=============================================================================
bool Test_HighConcurrencyAccess(const WCHAR* file)
{
    printf("Testing high concurrency file access...\n");
    
    DeleteFileW(file);
    
    const int NUM_THREADS = 8;
    const int ITERATIONS_PER_THREAD = 50;
    const size_t FILE_SIZE = 8192;
    
    std::atomic<bool> testPassed{true};
    std::atomic<int> completedThreads{0};
    
    // Create initial file
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                                0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Failed to create initial file\n");
            return false;
        }
        
        std::vector<char> initialData(FILE_SIZE);
        for (size_t i = 0; i < FILE_SIZE; i++) {
            initialData[i] = (char)('A' + (i % 26));
        }
        
        DWORD written;
        WriteFile(h, initialData.data(), (DWORD)initialData.size(), &written, NULL);
        CloseHandle(h);
    }
    
    // Launch multiple threads doing concurrent read/write
    std::vector<std::thread> threads;
    
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < ITERATIONS_PER_THREAD && testPassed; i++) {
                // Each thread opens its own handle
                HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
                
                if (h == INVALID_HANDLE_VALUE) {
                    // May fail due to sharing conflicts - retry
                    Sleep(10);
                    continue;
                }
                
                // Random operation: read or write
                if ((t + i) % 2 == 0) {
                    // Read at random offset
                    size_t offset = ((t * 1000 + i * 100) % FILE_SIZE);
                    LARGE_INTEGER pos;
                    pos.QuadPart = offset;
                    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
                    
                    char buffer[64];
                    DWORD bytesRead;
                    if (!ReadFile(h, buffer, sizeof(buffer), &bytesRead, NULL)) {
                        printf("Thread %d: Read error %d\n", t, GetLastError());
                        testPassed = false;
                    }
                } else {
                    // Write at random offset
                    size_t offset = ((t * 1000 + i * 100) % (FILE_SIZE - 64));
                    LARGE_INTEGER pos;
                    pos.QuadPart = offset;
                    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
                    
                    char buffer[64];
                    memset(buffer, 'X', sizeof(buffer));
                    DWORD bytesWritten;
                    if (!WriteFile(h, buffer, sizeof(buffer), &bytesWritten, NULL)) {
                        DWORD err = GetLastError();
                        if (err != ERROR_LOCK_VIOLATION) {
                            printf("Thread %d: Write error %d at offset %zu\n", t, err, offset);
                            testPassed = false;
                        }
                    }
                }
                
                CloseHandle(h);
            }
            completedThreads++;
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify file is still readable and consistent
    {
        HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, 
                              NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Failed to reopen file after concurrent access\n");
            return false;
        }
        
        LARGE_INTEGER fileSize;
        GetFileSizeEx(h, &fileSize);
        
        if (fileSize.QuadPart < (LONGLONG)FILE_SIZE) {
            printf("File size shrunk unexpectedly: %lld\n", fileSize.QuadPart);
            CloseHandle(h);
            DeleteFileW(file);
            return false;
        }
        
        // Try to read entire file
        std::vector<char> buffer(FILE_SIZE);
        DWORD bytesRead;
        if (!ReadFile(h, buffer.data(), FILE_SIZE, &bytesRead, NULL)) {
            printf("Failed to read file after concurrent access: %d\n", GetLastError());
            CloseHandle(h);
            DeleteFileW(file);
            return false;
        }
        
        CloseHandle(h);
    }
    
    DeleteFileW(file);
    
    // Allow file system to stabilize after heavy concurrent access
    Sleep(100);
    
    if (testPassed) {
        printf("High concurrency access test PASSED (%d threads, %d iterations each)\n", 
               NUM_THREADS, ITERATIONS_PER_THREAD);
    }
    return testPassed;
}

//=============================================================================
// Test: File IV persistence across handle close/reopen
// This tests that file IV is correctly preserved and readable
//=============================================================================
bool Test_FileIVPersistence(const WCHAR* file)
{
    printf("Testing file IV persistence...\n");
    
    DeleteFileW(file);
    
    const size_t FILE_SIZE = 2048;
    std::vector<char> testData(FILE_SIZE);
    for (size_t i = 0; i < FILE_SIZE; i++) {
        testData[i] = (char)('A' + (i % 26));
    }
    
    // Write with first handle
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Failed to create file\n");
            return false;
        }
        
        DWORD written;
        if (!WriteFile(h, testData.data(), (DWORD)testData.size(), &written, NULL)) {
            printf("Failed to write: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }
        
        CloseHandle(h);
    }
    
    // Verify with multiple reopens
    for (int i = 0; i < 10; i++) {
        HANDLE h = OpenTestFile(file, GENERIC_READ,
                                FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Reopen %d failed: %d\n", i, GetLastError());
            return false;
        }
        
        std::vector<char> readBuffer(FILE_SIZE);
        DWORD bytesRead;
        if (!ReadFile(h, readBuffer.data(), (DWORD)FILE_SIZE, &bytesRead, NULL)) {
            printf("Read %d failed: %d\n", i, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        if (bytesRead != FILE_SIZE) {
            printf("Partial read %d: %lu vs %zu\n", i, (unsigned long)bytesRead, FILE_SIZE);
            CloseHandle(h);
            return false;
        }
        
        if (memcmp(testData.data(), readBuffer.data(), FILE_SIZE) != 0) {
            printf("Data mismatch on reopen %d\n", i);
            // Find first difference
            for (size_t j = 0; j < FILE_SIZE; j++) {
                if (testData[j] != readBuffer[j]) {
                    printf("  First diff at offset %zu: expected 0x%02X, got 0x%02X\n",
                           j, (unsigned char)testData[j], (unsigned char)readBuffer[j]);
                    break;
                }
            }
            CloseHandle(h);
            return false;
        }
        
        CloseHandle(h);
    }
    
    DeleteFileW(file);
    printf("File IV persistence test PASSED\n");
    return true;
}

//=============================================================================
// Test: Rapid file operations (open, write, close, delete) cycle
// Tests for handle/resource leaks
//=============================================================================
bool Test_RapidFileOperationCycle(const WCHAR* file)
{
    printf("Testing rapid file operation cycles...\n");
    
    const int ITERATIONS = 100;
    
    for (int i = 0; i < ITERATIONS; i++) {
        // Create
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE,
                               0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Iteration %d: Create failed: %d\n", i, GetLastError());
            return false;
        }
        
        // Write variable size
        size_t dataSize = 100 + (i % 1000);
        std::vector<char> data(dataSize, (char)('A' + (i % 26)));
        DWORD written;
        if (!WriteFile(h, data.data(), (DWORD)dataSize, &written, NULL)) {
            printf("Iteration %d: Write failed: %d\n", i, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        // Close
        CloseHandle(h);
        
        // Delete
        if (!DeleteFileW(file)) {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND) {
                printf("Iteration %d: Delete failed: %d\n", i, err);
                return false;
            }
        }
        
        if ((i + 1) % 20 == 0) {
            printf("  Completed %d cycles\n", i + 1);
        }
    }
    
    printf("Rapid file operation cycle test PASSED (%d iterations)\n", ITERATIONS);
    return true;
}

//=============================================================================
// Test: Write then immediate read from same handle at offset 0
// This is the core pattern that fails in ZIP operations
//=============================================================================
bool Test_WriteImmediateReadOffset0(const WCHAR* file)
{
    printf("Testing write then immediate read at offset 0...\n");
    
    DeleteFileW(file);
    
    for (int iteration = 0; iteration < 20; iteration++) {
        // Create new file
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Iteration %d: Create failed: %d\n", iteration, GetLastError());
            return false;
        }
        
        // Write data
        size_t dataSize = 500 + (iteration * 100);
        std::vector<char> writeData(dataSize);
        for (size_t i = 0; i < dataSize; i++) {
            writeData[i] = (char)('A' + ((iteration + i) % 26));
        }
        
        DWORD written;
        if (!WriteFile(h, writeData.data(), (DWORD)dataSize, &written, NULL)) {
            printf("Iteration %d: Write failed: %d\n", iteration, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        // Seek back to 0
        LARGE_INTEGER pos;
        pos.QuadPart = 0;
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            printf("Iteration %d: Seek failed: %d\n", iteration, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        // Immediate read at offset 0
        std::vector<char> readData(dataSize);
        DWORD bytesRead;
        if (!ReadFile(h, readData.data(), (DWORD)dataSize, &bytesRead, NULL)) {
            printf("Iteration %d: Read at offset 0 FAILED: %d\n", iteration, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        if (bytesRead != dataSize) {
            printf("Iteration %d: Read returned %lu bytes, expected %zu\n",
                   iteration, (unsigned long)bytesRead, dataSize);
            CloseHandle(h);
            return false;
        }
        
        if (memcmp(writeData.data(), readData.data(), dataSize) != 0) {
            printf("Iteration %d: Data mismatch!\n", iteration);
            CloseHandle(h);
            return false;
        }
        
        CloseHandle(h);
        DeleteFileW(file);
    }
    
    printf("Write then immediate read at offset 0 test PASSED\n");
    return true;
}

//=============================================================================
// Test: Multi-handle concurrent write to the same file
// This reproduces the aapt2 block corruption issue where multiple handles
// write to the same file simultaneously, causing ERROR_INVALID_BLOCK (1392)
//=============================================================================
bool Test_MultiHandleConcurrentWrite(const WCHAR* file)
{
    printf("Testing multi-handle concurrent write (aapt2 pattern)...\n");
    
    DeleteFileW(file);
    
    const size_t FILE_SIZE = 32 * 1024;  // 32KB to span multiple encryption blocks
    const int NUM_THREADS = 4;
    const int ITERATIONS = 20;
    
    std::atomic<bool> testPassed{true};
    std::atomic<int> errorCount{0};
    
    // Create initial file with some content
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Failed to create initial file\n");
            return false;
        }
        
        std::vector<char> initialData(FILE_SIZE, 'X');
        DWORD written;
        WriteFile(h, initialData.data(), (DWORD)initialData.size(), &written, NULL);
        CloseHandle(h);
    }
    
    for (int iter = 0; iter < ITERATIONS && testPassed; iter++) {
        // Open multiple handles to the same file
        std::vector<HANDLE> handles(NUM_THREADS);
        for (int i = 0; i < NUM_THREADS; i++) {
            handles[i] = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
            if (handles[i] == INVALID_HANDLE_VALUE) {
                printf("Iteration %d: Failed to open handle %d\n", iter, i);
                for (int j = 0; j < i; j++) CloseHandle(handles[j]);
                return false;
            }
        }
        
        // Launch threads that write to different offsets simultaneously
        std::vector<std::thread> threads;
        const size_t CHUNK_SIZE = FILE_SIZE / NUM_THREADS;
        
        for (int t = 0; t < NUM_THREADS; t++) {
            threads.emplace_back([&, t]() {
                HANDLE h = handles[t];
                size_t offset = t * CHUNK_SIZE;
                
                // Write to our chunk
                LARGE_INTEGER pos;
                pos.QuadPart = offset;
                if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
                    printf("Thread %d: SetFilePointerEx failed: %d\n", t, GetLastError());
                    testPassed = false;
                    return;
                }
                
                std::vector<char> writeData(CHUNK_SIZE, (char)('A' + t));
                DWORD written;
                if (!WriteFile(h, writeData.data(), (DWORD)CHUNK_SIZE, &written, NULL)) {
                    DWORD err = GetLastError();
                    if (err == 1392) {
                        printf("Thread %d: ERROR_INVALID_BLOCK (1392) during write!\n", t);
                        errorCount++;
                    } else {
                        printf("Thread %d: Write error %d\n", t, err);
                        testPassed = false;
                    }
                    return;
                }
                
                // Small delay to increase interleaving
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            });
        }
        
        // Wait for all threads
        for (auto& t : threads) {
            t.join();
        }
        
        // Close all handles
        for (int i = 0; i < NUM_THREADS; i++) {
            CloseHandle(handles[i]);
        }
        
        // Verify file can be read back correctly
        {
            HANDLE h = OpenTestFile(file, GENERIC_READ,
                                    FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
            if (h == INVALID_HANDLE_VALUE) {
                printf("Iteration %d: Failed to reopen for verification\n", iter);
                return false;
            }
            
            std::vector<char> readBuffer(FILE_SIZE);
            DWORD bytesRead;
            if (!ReadFile(h, readBuffer.data(), (DWORD)FILE_SIZE, &bytesRead, NULL)) {
                DWORD err = GetLastError();
                if (err == 1392) {
                    printf("Iteration %d: ERROR_INVALID_BLOCK (1392) during read!\n", iter);
                    errorCount++;
                } else {
                    printf("Iteration %d: Read verification failed: %d\n", iter, err);
                }
                CloseHandle(h);
                return false;
            }
            
            CloseHandle(h);
            
            // Verify each chunk has correct data
            for (int t = 0; t < NUM_THREADS; t++) {
                size_t offset = t * CHUNK_SIZE;
                char expected = (char)('A' + t);
                for (size_t i = 0; i < CHUNK_SIZE; i++) {
                    if (readBuffer[offset + i] != expected) {
                        printf("Iteration %d: Data corruption at offset %zu (chunk %d)\n",
                               iter, offset + i, t);
                        printf("  Expected 0x%02X ('%c'), got 0x%02X\n",
                               (unsigned char)expected, expected,
                               (unsigned char)readBuffer[offset + i]);
                        return false;
                    }
                }
            }
        }
        
        if ((iter + 1) % 5 == 0) {
            printf("  Completed %d iterations\n", iter + 1);
        }
    }
    
    DeleteFileW(file);
    
    if (errorCount > 0) {
        printf("FAILED: Got %d ERROR_INVALID_BLOCK (1392) errors\n", errorCount.load());
        return false;
    }
    
    printf("Multi-handle concurrent write test PASSED (%d iterations)\n", ITERATIONS);
    return testPassed.load();
}

//=============================================================================
// Test: Multi-handle write then read pattern
// One handle writes, another handle immediately tries to read
//=============================================================================
bool Test_MultiHandleWriteThenRead(const WCHAR* file)
{
    printf("Testing multi-handle write then read pattern...\n");
    
    DeleteFileW(file);
    
    const size_t BLOCK_DATA_SIZE = 1016;  // EncFS block data size
    const size_t FILE_SIZE = BLOCK_DATA_SIZE * 4;
    const int ITERATIONS = 30;
    
    std::atomic<int> error1392Count{0};
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        // Create file
        HANDLE hWrite = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (hWrite == INVALID_HANDLE_VALUE) {
            printf("Iteration %d: Create failed\n", iter);
            return false;
        }
        
        // Open second handle for reading
        HANDLE hRead = OpenTestFile(file, GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (hRead == INVALID_HANDLE_VALUE) {
            printf("Iteration %d: Open read handle failed\n", iter);
            CloseHandle(hWrite);
            DeleteFileW(file);
            return false;
        }
        
        // Write data via write handle
        std::vector<char> writeData(FILE_SIZE);
        for (size_t i = 0; i < FILE_SIZE; i++) {
            writeData[i] = (char)('A' + (i % 26));
        }
        
        DWORD written;
        if (!WriteFile(hWrite, writeData.data(), (DWORD)FILE_SIZE, &written, NULL)) {
            DWORD err = GetLastError();
            printf("Iteration %d: Write failed: %d\n", iter, err);
            CloseHandle(hWrite);
            CloseHandle(hRead);
            DeleteFileW(file);
            return false;
        }
        
        // Immediately try to read via read handle (without closing write handle)
        std::vector<char> readData(FILE_SIZE);
        DWORD bytesRead;
        if (!ReadFile(hRead, readData.data(), (DWORD)FILE_SIZE, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            if (err == 1392) {
                printf("Iteration %d: ERROR_INVALID_BLOCK (1392) during read!\n", iter);
                error1392Count++;
            } else {
                printf("Iteration %d: Read failed: %d\n", iter, err);
            }
            CloseHandle(hWrite);
            CloseHandle(hRead);
            DeleteFileW(file);
            return false;
        }
        
        // Verify data
        if (bytesRead != FILE_SIZE) {
            printf("Iteration %d: Partial read: %lu of %zu\n", 
                   iter, (unsigned long)bytesRead, FILE_SIZE);
            CloseHandle(hWrite);
            CloseHandle(hRead);
            DeleteFileW(file);
            return false;
        }
        
        if (memcmp(writeData.data(), readData.data(), FILE_SIZE) != 0) {
            printf("Iteration %d: Data mismatch\n", iter);
            CloseHandle(hWrite);
            CloseHandle(hRead);
            DeleteFileW(file);
            return false;
        }
        
        CloseHandle(hWrite);
        CloseHandle(hRead);
        DeleteFileW(file);
        
        if ((iter + 1) % 10 == 0) {
            printf("  Completed %d iterations\n", iter + 1);
        }
    }
    
    if (error1392Count > 0) {
        printf("FAILED: Got %d ERROR_INVALID_BLOCK (1392) errors\n", error1392Count.load());
        return false;
    }
    
    printf("Multi-handle write then read test PASSED (%d iterations)\n", ITERATIONS);
    return true;
}

//=============================================================================
// Test: aapt2-like multi-file access pattern
// Simulates aapt2 behavior: multiple .flata files being written/read simultaneously
//=============================================================================
bool Test_Aapt2LikeMultiFileAccess(const WCHAR* file)
{
    printf("Testing aapt2-like multi-file access pattern...\n");
    
    // Create multiple test files
    std::wstring basePath(file);
    std::vector<std::wstring> files;
    for (int i = 0; i < 4; i++) {
        files.push_back(basePath + L"." + std::to_wstring(i) + L".flata");
        DeleteFileW(files.back().c_str());
    }
    
    const size_t FILE_SIZE = 8192;
    const int ITERATIONS = 20;
    
    std::atomic<bool> testPassed{true};
    std::atomic<int> error1392Count{0};
    
    for (int iter = 0; iter < ITERATIONS && testPassed; iter++) {
        std::vector<std::thread> threads;
        
        // Each thread works on its own file, but all run simultaneously
        for (int t = 0; t < 4; t++) {
            threads.emplace_back([&, t]() {
                const std::wstring& filePath = files[t];
                
                // Create and write
                HANDLE hWrite = CreateFileW(filePath.c_str(), 
                                           GENERIC_READ | GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           NULL, CREATE_ALWAYS, 
                                           FILE_ATTRIBUTE_NORMAL, NULL);
                if (hWrite == INVALID_HANDLE_VALUE) {
                    printf("Thread %d: Create failed\n", t);
                    testPassed = false;
                    return;
                }
                
                // Write varying amount of data
                size_t dataSize = FILE_SIZE + (t * 1000);
                std::vector<char> data(dataSize, (char)('A' + t));
                DWORD written;
                if (!WriteFile(hWrite, data.data(), (DWORD)dataSize, &written, NULL)) {
                    DWORD err = GetLastError();
                    if (err == 1392) {
                        error1392Count++;
                    }
                    printf("Thread %d: Write failed: %d\n", t, err);
                    CloseHandle(hWrite);
                    testPassed = false;
                    return;
                }
                
                // Open another handle and read (simulating aapt2's pattern)
                HANDLE hRead = CreateFileW(filePath.c_str(),
                                          GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          NULL, OPEN_EXISTING, 0, NULL);
                if (hRead == INVALID_HANDLE_VALUE) {
                    printf("Thread %d: Open read handle failed\n", t);
                    CloseHandle(hWrite);
                    testPassed = false;
                    return;
                }
                
                std::vector<char> readBuffer(dataSize);
                DWORD bytesRead;
                if (!ReadFile(hRead, readBuffer.data(), (DWORD)dataSize, &bytesRead, NULL)) {
                    DWORD err = GetLastError();
                    if (err == 1392) {
                        error1392Count++;
                        printf("Thread %d: ERROR_INVALID_BLOCK (1392) during read!\n", t);
                    } else {
                        printf("Thread %d: Read failed: %d\n", t, err);
                    }
                    CloseHandle(hRead);
                    CloseHandle(hWrite);
                    testPassed = false;
                    return;
                }
                
                if (bytesRead != dataSize || memcmp(data.data(), readBuffer.data(), dataSize) != 0) {
                    printf("Thread %d: Data verification failed\n", t);
                    CloseHandle(hRead);
                    CloseHandle(hWrite);
                    testPassed = false;
                    return;
                }
                
                CloseHandle(hRead);
                CloseHandle(hWrite);
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        if ((iter + 1) % 5 == 0) {
            printf("  Completed %d iterations\n", iter + 1);
        }
    }
    
    // Cleanup
    for (const auto& f : files) {
        DeleteFileW(f.c_str());
    }
    
    if (error1392Count > 0) {
        printf("FAILED: Got %d ERROR_INVALID_BLOCK (1392) errors\n", error1392Count.load());
        return false;
    }
    
    printf("aapt2-like multi-file access test PASSED (%d iterations)\n", ITERATIONS);
    return testPassed.load();
}

//=============================================================================
// Test: Concurrent multi-offset write from multiple handles
// Multiple handles write to different offsets of the same file simultaneously
//=============================================================================
bool Test_ConcurrentMultiOffsetWrite(const WCHAR* file)
{
    printf("Testing concurrent multi-offset write...\n");
    
    DeleteFileW(file);
    
    const size_t BLOCK_DATA_SIZE = 1016;
    const size_t FILE_SIZE = BLOCK_DATA_SIZE * 8;  // 8 blocks
    const int NUM_HANDLES = 4;
    const int WRITES_PER_HANDLE = 10;
    
    std::atomic<bool> testPassed{true};
    std::atomic<int> error1392Count{0};
    
    // Pre-create file
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 0,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) return false;
        
        std::vector<char> zeros(FILE_SIZE, 0);
        DWORD written;
        WriteFile(h, zeros.data(), (DWORD)FILE_SIZE, &written, NULL);
        CloseHandle(h);
    }
    
    // Open multiple handles
    std::vector<HANDLE> handles(NUM_HANDLES);
    for (int i = 0; i < NUM_HANDLES; i++) {
        handles[i] = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (handles[i] == INVALID_HANDLE_VALUE) {
            printf("Failed to open handle %d\n", i);
            for (int j = 0; j < i; j++) CloseHandle(handles[j]);
            DeleteFileW(file);
            return false;
        }
    }
    
    // Launch threads that write to interleaved offsets
    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_HANDLES; t++) {
        threads.emplace_back([&, t]() {
            HANDLE h = handles[t];
            
            for (int i = 0; i < WRITES_PER_HANDLE && testPassed; i++) {
                // Calculate offset: interleaved pattern
                size_t offset = ((t + i * NUM_HANDLES) % 8) * BLOCK_DATA_SIZE;
                
                LARGE_INTEGER pos;
                pos.QuadPart = offset;
                if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
                    printf("Thread %d: SetFilePointerEx failed: %d\n", t, GetLastError());
                    testPassed = false;
                    return;
                }
                
                // Write partial block (to trigger read-modify-write)
                std::vector<char> data(BLOCK_DATA_SIZE / 2, (char)('A' + t));
                DWORD written;
                if (!WriteFile(h, data.data(), (DWORD)data.size(), &written, NULL)) {
                    DWORD err = GetLastError();
                    if (err == 1392) {
                        printf("Thread %d: ERROR_INVALID_BLOCK (1392) at offset %zu!\n", 
                               t, offset);
                        error1392Count++;
                    } else {
                        printf("Thread %d: Write error %d at offset %zu\n", t, err, offset);
                        testPassed = false;
                    }
                    return;
                }
                
                // Small delay to increase interleaving
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Close handles
    for (int i = 0; i < NUM_HANDLES; i++) {
        CloseHandle(handles[i]);
    }
    
    // Verify file can be read
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ, FILE_SHARE_READ,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Failed to reopen for verification\n");
            DeleteFileW(file);
            return false;
        }
        
        std::vector<char> buffer(FILE_SIZE);
        DWORD bytesRead;
        if (!ReadFile(h, buffer.data(), (DWORD)FILE_SIZE, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            if (err == 1392) {
                error1392Count++;
                printf("ERROR_INVALID_BLOCK (1392) during final read!\n");
            } else {
                printf("Final read failed: %d\n", err);
            }
            CloseHandle(h);
            DeleteFileW(file);
            return false;
        }
        
        CloseHandle(h);
    }
    
    DeleteFileW(file);
    
    if (error1392Count > 0) {
        printf("FAILED: Got %d ERROR_INVALID_BLOCK (1392) errors\n", error1392Count.load());
        return false;
    }
    
    printf("Concurrent multi-offset write test PASSED\n");
    return testPassed.load();
}

//=============================================================================
// Test: Stress test for multi-handle read/write
// High intensity concurrent access to detect subtle race conditions
//=============================================================================
bool Test_StressMultiHandleReadWrite(const WCHAR* file)
{
    printf("Testing stress multi-handle read/write...\n");
    
    DeleteFileW(file);
    
    const size_t FILE_SIZE = 64 * 1024;  // 64KB
    const int NUM_THREADS = 8;
    const int OPERATIONS_PER_THREAD = 50;
    
    std::atomic<bool> testPassed{true};
    std::atomic<int> error1392Count{0};
    std::atomic<int> totalOperations{0};
    
    // Create initial file
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                                0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) return false;
        
        std::vector<char> data(FILE_SIZE, 'X');
        DWORD written;
        WriteFile(h, data.data(), (DWORD)FILE_SIZE, &written, NULL);
        CloseHandle(h);
    }
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD && testPassed; i++) {
                // Each thread opens its own handle
                HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
                
                if (h == INVALID_HANDLE_VALUE) {
                    // Retry on sharing violation
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                
                // Random operation
                bool isWrite = ((t + i) % 2 == 0);
                size_t offset = ((t * 1000 + i * 100) % (FILE_SIZE - 1024));
                size_t opSize = 256 + ((t + i) % 768);
                
                LARGE_INTEGER pos;
                pos.QuadPart = offset;
                SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
                
                if (isWrite) {
                    std::vector<char> data(opSize, (char)('A' + (t % 26)));
                    DWORD written;
                    if (!WriteFile(h, data.data(), (DWORD)opSize, &written, NULL)) {
                        DWORD err = GetLastError();
                        if (err == 1392) {
                            error1392Count++;
                        } else if (err != ERROR_LOCK_VIOLATION) {
                            printf("Thread %d: Write error %d\n", t, err);
                            testPassed = false;
                        }
                    }
                } else {
                    std::vector<char> buffer(opSize);
                    DWORD bytesRead = 0;
                    if (!ReadFile(h, buffer.data(), (DWORD)opSize, &bytesRead, NULL)) {
                        DWORD err = GetLastError();
                        if (err == 1392) {
                            error1392Count++;
                        } else if (err != ERROR_LOCK_VIOLATION) {
                            printf("Thread %d: Read error %d\n", t, err);
                            testPassed = false;
                        }
                    }
                }
                
                CloseHandle(h);
                totalOperations++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Final verification read
    {
        HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, 
                              NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            std::vector<char> buffer(FILE_SIZE);
            DWORD bytesRead;
            if (!ReadFile(h, buffer.data(), (DWORD)FILE_SIZE, &bytesRead, NULL)) {
                DWORD err = GetLastError();
                if (err == 1392) {
                    error1392Count++;
                    printf("ERROR_INVALID_BLOCK (1392) during final verification!\n");
                }
            }
            CloseHandle(h);
        }
    }
    
    DeleteFileW(file);
    
    printf("  Completed %d total operations\n", totalOperations.load());
    
    if (error1392Count > 0) {
        printf("FAILED: Got %d ERROR_INVALID_BLOCK (1392) errors\n", error1392Count.load());
        return false;
    }
    
    printf("Stress multi-handle read/write test PASSED\n");
    return testPassed.load();
}

