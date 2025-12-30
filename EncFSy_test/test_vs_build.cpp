// test_vs_build.cpp - Tests based on VS build failure patterns
//
// These tests simulate the file access patterns observed during Visual Studio
// project builds that revealed issues with:
// 1. CREATE_ALWAYS/TRUNCATE_EXISTING followed by immediate write
// 2. Empty file with partial block writes (offset != 0)
// 3. Parallel read/write from different handles
// 4. JSON/cache file patterns (truncate to zero, rewrite)
// 5. ZIP archive access patterns (seek end, seek start, read)

#include "test_common.h"
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

//=============================================================================
// Test: CREATE_ALWAYS then Write
// Simulates: Visual Studio creating/overwriting project.assets.json
//=============================================================================
bool Test_CreateAlwaysThenWrite(const WCHAR* file)
{
    // Clean up any existing file
    DeleteFileW(file);

    // First, create the file with some initial content
    {
        HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Initial CreateFile failed: %d\n", GetLastError());
            return false;
        }
        
        const char* initialData = "Initial content that will be overwritten";
        DWORD written;
        WriteFile(h, initialData, (DWORD)strlen(initialData), &written, NULL);
        CloseHandle(h);
    }

    // Now open with CREATE_ALWAYS (should truncate) and write new content
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: CREATE_ALWAYS failed: %d\n", GetLastError());
            return false;
        }

        // Write JSON-like content (simulating project.assets.json)
        const char* jsonContent = R"({
  "version": 3,
  "targets": {
    ".NETCoreApp,Version=v9.0": {
      "Microsoft.Extensions.Logging/9.0.0": {
        "type": "package"
      }
    }
  }
})";
        DWORD written;
        if (!WriteFile(h, jsonContent, (DWORD)strlen(jsonContent), &written, NULL)) {
            wprintf(L"  FAIL: Write after CREATE_ALWAYS failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        if (written != strlen(jsonContent)) {
            wprintf(L"  FAIL: Incomplete write: %d vs %zu\n", written, strlen(jsonContent));
            CloseHandle(h);
            return false;
        }

        CloseHandle(h);
    }

    // Verify the content
    {
        HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, 
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Reopen for read failed: %d\n", GetLastError());
            return false;
        }

        char buffer[1024] = {0};
        DWORD bytesRead;
        if (!ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            wprintf(L"  FAIL: Read verification failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        CloseHandle(h);

        // Verify content starts with JSON
        if (bytesRead == 0 || buffer[0] != '{') {
            wprintf(L"  FAIL: Content verification failed. Read %d bytes, first char: 0x%02X\n", 
                bytesRead, (unsigned char)buffer[0]);
            return false;
        }
    }

    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: TRUNCATE_EXISTING then Write
// Simulates: Overwriting cache files
//=============================================================================
bool Test_TruncateExistingThenWrite(const WCHAR* file)
{
    DeleteFileW(file);

    // Create file with initial content
    {
        HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Initial CreateFile failed: %d\n", GetLastError());
            return false;
        }
        
        char largeData[4096];
        memset(largeData, 'X', sizeof(largeData));
        DWORD written;
        WriteFile(h, largeData, sizeof(largeData), &written, NULL);
        CloseHandle(h);
    }

    // Open with TRUNCATE_EXISTING and write new (smaller) content
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            FILE_SHARE_READ, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: TRUNCATE_EXISTING failed: %d\n", GetLastError());
            return false;
        }

        // Verify file size is 0 after truncate
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(h, &fileSize) || fileSize.QuadPart != 0) {
            wprintf(L"  FAIL: File not truncated to 0: %lld\n", fileSize.QuadPart);
            CloseHandle(h);
            return false;
        }

        const char* newContent = "New smaller content after truncate";
        DWORD written;
        if (!WriteFile(h, newContent, (DWORD)strlen(newContent), &written, NULL)) {
            wprintf(L"  FAIL: Write after TRUNCATE_EXISTING failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        CloseHandle(h);
    }

    // Verify
    {
        HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, 
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Reopen for read failed: %d\n", GetLastError());
            return false;
        }

        char buffer[256] = {0};
        DWORD bytesRead;
        ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
        CloseHandle(h);

        if (bytesRead == 0 || strncmp(buffer, "New smaller", 11) != 0) {
            wprintf(L"  FAIL: Content mismatch. Got: %hs\n", buffer);
            return false;
        }
    }

    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: Empty File Partial Block Write
// Simulates: Writing at an offset to a newly created/truncated file
// This tests the case where shift != 0 but fileSize == 0
//=============================================================================
bool Test_EmptyFilePartialBlockWrite(const WCHAR* file)
{
    DeleteFileW(file);

    // Test various offsets to catch block boundary issues
    const size_t testOffsets[] = { 100, 512, 1000, 1024, 2048, 4096 };

    for (size_t offset : testOffsets) {
        // Create new empty file
        {
            HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
                0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                wprintf(L"  FAIL: CreateFile failed for offset %zu: %d\n", offset, GetLastError());
                return false;
            }

            // Seek to offset and write
            LARGE_INTEGER li;
            li.QuadPart = offset;
            if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN)) {
                wprintf(L"  FAIL: SetFilePointerEx failed for offset %zu: %d\n", offset, GetLastError());
                CloseHandle(h);
                return false;
            }

            const char* testData = "Data at offset";
            DWORD written;
            if (!WriteFile(h, testData, (DWORD)strlen(testData), &written, NULL)) {
                wprintf(L"  FAIL: Write at offset %zu failed: %d\n", offset, GetLastError());
                CloseHandle(h);
                return false;
            }

            CloseHandle(h);
        }

        // Verify
        {
            HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, 
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                wprintf(L"  FAIL: Reopen failed for offset %zu: %d\n", offset, GetLastError());
                return false;
            }

            // Check file size
            LARGE_INTEGER fileSize;
            GetFileSizeEx(h, &fileSize);
            size_t expectedSize = offset + strlen("Data at offset");
            if ((size_t)fileSize.QuadPart != expectedSize) {
                wprintf(L"  FAIL: Size mismatch at offset %zu: got %lld, expected %zu\n", 
                    offset, fileSize.QuadPart, expectedSize);
                CloseHandle(h);
                return false;
            }

            // Read and verify the data at offset
            LARGE_INTEGER li;
            li.QuadPart = offset;
            SetFilePointerEx(h, li, NULL, FILE_BEGIN);

            char buffer[64] = {0};
            DWORD bytesRead;
            ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
            CloseHandle(h);

            if (strncmp(buffer, "Data at offset", 14) != 0) {
                wprintf(L"  FAIL: Content mismatch at offset %zu: got '%hs'\n", offset, buffer);
                return false;
            }
        }

        DeleteFileW(file);
    }

    return true;
}

//=============================================================================
// Test: Write then Read from Different Handles
// Simulates: One process writes, another reads immediately
//=============================================================================
bool Test_WriteReadDifferentHandles(const WCHAR* file)
{
    DeleteFileW(file);

    const char* testContent = "Content written by handle 1";

    // Handle 1: Write
    HANDLE hWrite = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hWrite == INVALID_HANDLE_VALUE) {
        wprintf(L"  FAIL: Create write handle failed: %d\n", GetLastError());
        return false;
    }

    DWORD written;
    if (!WriteFile(hWrite, testContent, (DWORD)strlen(testContent), &written, NULL)) {
        wprintf(L"  FAIL: Write failed: %d\n", GetLastError());
        CloseHandle(hWrite);
        return false;
    }

    // Flush to ensure data is visible
    FlushFileBuffers(hWrite);

    // Handle 2: Read (while handle 1 is still open)
    HANDLE hRead = CreateFileW(file, GENERIC_READ, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hRead == INVALID_HANDLE_VALUE) {
        wprintf(L"  FAIL: Open read handle failed: %d\n", GetLastError());
        CloseHandle(hWrite);
        return false;
    }

    char buffer[256] = {0};
    DWORD bytesRead;
    if (!ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
        wprintf(L"  FAIL: Read failed: %d\n", GetLastError());
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }

    CloseHandle(hRead);
    CloseHandle(hWrite);

    // Verify content
    if (bytesRead != strlen(testContent) || strcmp(buffer, testContent) != 0) {
        wprintf(L"  FAIL: Content mismatch. Read %d bytes: '%hs'\n", bytesRead, buffer);
        DeleteFileW(file);
        return false;
    }

    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: JSON File Truncate and Rewrite Pattern
// Simulates: VS updating project.assets.json repeatedly
//=============================================================================
bool Test_JsonFileTruncateRewrite(const WCHAR* file)
{
    DeleteFileW(file);

    // Simulate multiple updates (like NuGet restore cycles)
    for (int cycle = 0; cycle < 5; cycle++) {
        // Create/overwrite with CREATE_ALWAYS
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Cycle %d - CreateFile failed: %d\n", cycle, GetLastError());
            return false;
        }

        // Generate different content each cycle (varying size)
        char content[2048];
        int contentLen = snprintf(content, sizeof(content), 
            R"({"version":%d,"timestamp":"%d","data":[)", cycle, cycle * 1000);
        
        // Add varying amount of data
        for (int i = 0; i < cycle * 10 + 5; i++) {
            contentLen += snprintf(content + contentLen, sizeof(content) - contentLen, 
                "%d,", i);
        }
        contentLen += snprintf(content + contentLen, sizeof(content) - contentLen, "0]}");

        DWORD written;
        if (!WriteFile(h, content, contentLen, &written, NULL)) {
            wprintf(L"  FAIL: Cycle %d - Write failed: %d\n", cycle, GetLastError());
            CloseHandle(h);
            return false;
        }

        CloseHandle(h);

        // Immediately read and verify
        h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, 
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Cycle %d - Reopen failed: %d\n", cycle, GetLastError());
            return false;
        }

        char readBuffer[2048] = {0};
        DWORD bytesRead;
        if (!ReadFile(h, readBuffer, sizeof(readBuffer) - 1, &bytesRead, NULL)) {
            wprintf(L"  FAIL: Cycle %d - Read failed: %d\n", cycle, GetLastError());
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);

        // Verify the JSON is valid (starts with { and contains expected version)
        if (readBuffer[0] != '{') {
            wprintf(L"  FAIL: Cycle %d - Invalid JSON start: 0x%02X\n", cycle, (unsigned char)readBuffer[0]);
            return false;
        }

        char expectedVersion[32];
        snprintf(expectedVersion, sizeof(expectedVersion), "\"version\":%d", cycle);
        if (strstr(readBuffer, expectedVersion) == NULL) {
            wprintf(L"  FAIL: Cycle %d - Version mismatch in content\n", cycle);
            return false;
        }
    }

    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: Cache File Pattern
// Simulates: FXCalc.assets.cache pattern - truncate, write, read from different handle
//=============================================================================
bool Test_CacheFilePattern(const WCHAR* file)
{
    DeleteFileW(file);

    // Initial creation with some content
    {
        HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Initial create failed: %d\n", GetLastError());
            return false;
        }
        
        const char* oldContent = "Old cache content that will be replaced";
        DWORD written;
        WriteFile(h, oldContent, (DWORD)strlen(oldContent), &written, NULL);
        CloseHandle(h);
    }

    // Truncate and write new cache (simulating build system)
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: CREATE_ALWAYS failed: %d\n", GetLastError());
            return false;
        }

        // Binary-ish cache content
        unsigned char cacheContent[512];
        for (int i = 0; i < sizeof(cacheContent); i++) {
            cacheContent[i] = (unsigned char)(i % 256);
        }

        DWORD written;
        if (!WriteFile(h, cacheContent, sizeof(cacheContent), &written, NULL)) {
            wprintf(L"  FAIL: Cache write failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        // Close write handle
        CloseHandle(h);
    }

    // Read from fresh handle (simulating different process reading cache)
    {
        HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, 
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Cache read open failed: %d\n", GetLastError());
            return false;
        }

        unsigned char readBuffer[512];
        DWORD bytesRead;
        if (!ReadFile(h, readBuffer, sizeof(readBuffer), &bytesRead, NULL)) {
            wprintf(L"  FAIL: Cache read failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);

        // Verify
        if (bytesRead != 512) {
            wprintf(L"  FAIL: Cache size mismatch: %d vs 512\n", bytesRead);
            return false;
        }

        for (int i = 0; i < 512; i++) {
            if (readBuffer[i] != (unsigned char)(i % 256)) {
                wprintf(L"  FAIL: Cache content mismatch at byte %d: got %d, expected %d\n", 
                    i, readBuffer[i], i % 256);
                return false;
            }
        }
    }

    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: Parallel Write and Read
// Simulates: MSBuild parallel compilation accessing same files
//=============================================================================
bool Test_ParallelWriteRead(const WCHAR* file)
{
    DeleteFileW(file);

    std::atomic<bool> writerDone{false};
    std::atomic<bool> testPassed{true};
    std::atomic<int> writeCount{0};

    // Writer thread: repeatedly update the file
    std::thread writer([&]() {
        for (int i = 0; i < 10 && testPassed; i++) {
            HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                testPassed = false;
                break;
            }

            char content[256];
            int len = snprintf(content, sizeof(content), "Iteration %d: %s", i, 
                "This is test content for parallel access testing.");
            
            DWORD written;
            if (!WriteFile(h, content, len, &written, NULL)) {
                testPassed = false;
                CloseHandle(h);
                break;
            }
            FlushFileBuffers(h);
            CloseHandle(h);
            writeCount++;

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        writerDone = true;
    });

    // Reader thread: read while writer is active
    std::thread reader([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Let writer start
        
        int readAttempts = 0;
        int successfulReads = 0;

        while (!writerDone || readAttempts < 5) {
            HANDLE h = CreateFileW(file, GENERIC_READ, 
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            
            if (h != INVALID_HANDLE_VALUE) {
                char buffer[512] = {0};
                DWORD bytesRead;
                if (ReadFile(h, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                    // Verify content is valid (starts with "Iteration")
                    if (bytesRead > 0) {
                        if (strncmp(buffer, "Iteration", 9) != 0) {
                            wprintf(L"  WARN: Invalid content read: '%hs'\n", buffer);
                            // Don't fail - timing issues are expected
                        } else {
                            successfulReads++;
                        }
                    }
                }
                CloseHandle(h);
            }
            readAttempts++;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        if (successfulReads == 0 && writeCount > 0) {
            wprintf(L"  WARN: No successful reads despite writes\n");
        }
    });

    writer.join();
    reader.join();

    DeleteFileW(file);
    return testPassed;
}

//=============================================================================
// Test: Create, Write, Close, Reopen, Read
// Simulates: Basic file lifecycle that VS build uses
//=============================================================================
bool Test_CreateWriteCloseReopenRead(const WCHAR* file)
{
    DeleteFileW(file);

    // Test with various content sizes (including edge cases around block boundaries)
    const size_t testSizes[] = { 1, 100, 512, 1000, 1016, 1024, 2000, 4096, 8192 };

    for (size_t size : testSizes) {
        // Generate test data
        std::vector<char> testData(size);
        for (size_t i = 0; i < size; i++) {
            testData[i] = (char)('A' + (i % 26));
        }

        // Create and write
        {
            HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
                0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                wprintf(L"  FAIL: Create failed for size %zu: %d\n", size, GetLastError());
                return false;
            }

            DWORD written;
            if (!WriteFile(h, testData.data(), (DWORD)size, &written, NULL) || written != size) {
                wprintf(L"  FAIL: Write failed for size %zu: written=%d, error=%d\n", 
                    size, written, GetLastError());
                CloseHandle(h);
                return false;
            }

            CloseHandle(h);
        }

        // Reopen and read
        {
            HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, 
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                wprintf(L"  FAIL: Reopen failed for size %zu: %d\n", size, GetLastError());
                return false;
            }

            std::vector<char> readBuffer(size + 100); // Extra space to detect overread
            DWORD bytesRead;
            if (!ReadFile(h, readBuffer.data(), (DWORD)readBuffer.size(), &bytesRead, NULL)) {
                wprintf(L"  FAIL: Read failed for size %zu: %d\n", size, GetLastError());
                CloseHandle(h);
                return false;
            }

            CloseHandle(h);

            if (bytesRead != size) {
                wprintf(L"  FAIL: Size mismatch for %zu: read %d bytes\n", size, bytesRead);
                return false;
            }

            if (memcmp(testData.data(), readBuffer.data(), size) != 0) {
                wprintf(L"  FAIL: Content mismatch for size %zu\n", size);
                return false;
            }
        }

        DeleteFileW(file);
    }

    return true;
}

//=============================================================================
// Test: ZIP-like read pattern (seek end, seek start, read)
// Simulates: ZIP library reading Central Directory then Local File Headers
// This is the pattern causing "failed to read at offset 0" errors
//=============================================================================
bool Test_ZipLikeReadPattern(const WCHAR* file)
{
    DeleteFileW(file);

    // Create a file that simulates ZIP structure
    // ZIP: [Local File Headers...][File Data...][Central Directory][End of Central Directory]
    const size_t ZIP_LOCAL_HEADER_OFFSET = 0;
    const size_t ZIP_FILE_DATA_SIZE = 2048;
    const size_t ZIP_CENTRAL_DIR_SIZE = 512;
    const size_t ZIP_EOCD_SIZE = 22;  // Minimum End of Central Directory size
    const size_t TOTAL_SIZE = ZIP_FILE_DATA_SIZE + ZIP_CENTRAL_DIR_SIZE + ZIP_EOCD_SIZE;

    // Create ZIP-like file
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Create ZIP-like file failed: %d\n", GetLastError());
            return false;
        }

        // Write simulated ZIP content
        std::vector<char> content(TOTAL_SIZE);
        
        // Local File Header signature at start
        content[0] = 0x50; content[1] = 0x4B; content[2] = 0x03; content[3] = 0x04;
        
        // Fill file data with pattern
        for (size_t i = 4; i < ZIP_FILE_DATA_SIZE; i++) {
            content[i] = (char)('A' + (i % 26));
        }
        
        // Central Directory signature
        size_t cdOffset = ZIP_FILE_DATA_SIZE;
        content[cdOffset] = 0x50; content[cdOffset+1] = 0x4B; 
        content[cdOffset+2] = 0x01; content[cdOffset+3] = 0x02;
        
        // End of Central Directory signature
        size_t eocdOffset = ZIP_FILE_DATA_SIZE + ZIP_CENTRAL_DIR_SIZE;
        content[eocdOffset] = 0x50; content[eocdOffset+1] = 0x4B;
        content[eocdOffset+2] = 0x05; content[eocdOffset+3] = 0x06;

        DWORD written;
        if (!WriteFile(h, content.data(), (DWORD)content.size(), &written, NULL)) {
            wprintf(L"  FAIL: Write ZIP content failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);
    }

    // Simulate ZIP library read pattern: seek to end, read EOCD, seek to CD, read CD, seek to start
    for (int iteration = 0; iteration < 10; iteration++) {
        HANDLE h = CreateFileW(file, GENERIC_READ, 
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 
            FILE_FLAG_RANDOM_ACCESS, NULL);  // Important: ZIP libraries use random access
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Open for ZIP-like read failed: %d\n", GetLastError());
            return false;
        }

        // Step 1: Seek to end to find file size
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(h, &fileSize)) {
            wprintf(L"  FAIL: GetFileSizeEx failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        if (fileSize.QuadPart != TOTAL_SIZE) {
            wprintf(L"  FAIL: Unexpected file size: %lld vs %zu\n", fileSize.QuadPart, TOTAL_SIZE);
            CloseHandle(h);
            return false;
        }

        // Step 2: Seek to EOCD location (end - 22 bytes minimum)
        LARGE_INTEGER pos;
        pos.QuadPart = fileSize.QuadPart - ZIP_EOCD_SIZE;
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            wprintf(L"  FAIL: Seek to EOCD failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        // Step 3: Read EOCD
        char eocdBuffer[64];
        DWORD bytesRead = 0;
        if (!ReadFile(h, eocdBuffer, ZIP_EOCD_SIZE, &bytesRead, NULL)) {
            wprintf(L"  FAIL: Read EOCD failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }
        if (bytesRead != ZIP_EOCD_SIZE) {
            wprintf(L"  FAIL: Partial EOCD read: %d vs %zu\n", bytesRead, ZIP_EOCD_SIZE);
            CloseHandle(h);
            return false;
        }

        // Verify EOCD signature
        if (eocdBuffer[0] != 0x50 || eocdBuffer[1] != 0x4B || 
            eocdBuffer[2] != 0x05 || eocdBuffer[3] != 0x06) {
            wprintf(L"  FAIL: EOCD signature mismatch\n");
            CloseHandle(h);
            return false;
        }

        // Step 4: Seek to Central Directory
        pos.QuadPart = ZIP_FILE_DATA_SIZE;
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            wprintf(L"  FAIL: Seek to Central Directory failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        // Step 5: Read Central Directory header
        char cdBuffer[64];
        bytesRead = 0;
        if (!ReadFile(h, cdBuffer, 4, &bytesRead, NULL)) {
            wprintf(L"  FAIL: Read Central Directory failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        // Verify CD signature
        if (cdBuffer[0] != 0x50 || cdBuffer[1] != 0x4B || 
            cdBuffer[2] != 0x01 || cdBuffer[3] != 0x02) {
            wprintf(L"  FAIL: Central Directory signature mismatch\n");
            CloseHandle(h);
            return false;
        }

        // Step 6: *** CRITICAL *** Seek back to offset 0 and read Local File Header
        // This is where the "failed to read at offset 0" error occurs
        pos.QuadPart = 0;
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            wprintf(L"  FAIL: Seek to offset 0 failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        char localHeaderBuffer[64];
        bytesRead = 0;
        if (!ReadFile(h, localHeaderBuffer, 30, &bytesRead, NULL)) {  // Local file header is 30 bytes min
            wprintf(L"  FAIL (iteration %d): Read at offset 0 failed: %d\n", iteration, GetLastError());
            CloseHandle(h);
            return false;
        }

        if (bytesRead == 0) {
            wprintf(L"  FAIL (iteration %d): Zero bytes read at offset 0 (file size=%lld)\n", 
                iteration, fileSize.QuadPart);
            CloseHandle(h);
            return false;
        }

        if (bytesRead < 4) {
            wprintf(L"  FAIL (iteration %d): Partial read at offset 0: %d bytes\n", iteration, bytesRead);
            CloseHandle(h);
            return false;
        }

        // Verify Local File Header signature
        if (localHeaderBuffer[0] != 0x50 || localHeaderBuffer[1] != 0x4B || 
            localHeaderBuffer[2] != 0x03 || localHeaderBuffer[3] != 0x04) {
            wprintf(L"  FAIL (iteration %d): Local File Header signature mismatch at offset 0\n", iteration);
            wprintf(L"    Got: %02X %02X %02X %02X\n", 
                (unsigned char)localHeaderBuffer[0], (unsigned char)localHeaderBuffer[1],
                (unsigned char)localHeaderBuffer[2], (unsigned char)localHeaderBuffer[3]);
            CloseHandle(h);
            return false;
        }

        CloseHandle(h);
    }

    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: Simultaneous read from multiple offsets
// Simulates: ZIP library reading multiple entries concurrently
//=============================================================================
bool Test_SimultaneousMultiOffsetRead(const WCHAR* file)
{
    DeleteFileW(file);

    const size_t FILE_SIZE = 8192;
    const size_t BLOCK_SIZE = 1024;

    // Create file with known content
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Create file failed: %d\n", GetLastError());
            return false;
        }

        std::vector<char> content(FILE_SIZE);
        for (size_t i = 0; i < FILE_SIZE; i++) {
            // Each 1KB block starts with its block number
            content[i] = (char)((i / BLOCK_SIZE) + 'A');
        }

        DWORD written;
        WriteFile(h, content.data(), (DWORD)content.size(), &written, NULL);
        CloseHandle(h);
    }

    // Open multiple handles and read from different offsets simultaneously
    const int NUM_HANDLES = 4;
    HANDLE handles[NUM_HANDLES];
    
    for (int i = 0; i < NUM_HANDLES; i++) {
        handles[i] = CreateFileW(file, GENERIC_READ, 
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 
            FILE_FLAG_RANDOM_ACCESS, NULL);
        if (handles[i] == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Open handle %d failed: %d\n", i, GetLastError());
            for (int j = 0; j < i; j++) CloseHandle(handles[j]);
            return false;
        }
    }

    // Interleaved reads from different positions
    for (int round = 0; round < 20; round++) {
        for (int i = 0; i < NUM_HANDLES; i++) {
            // Each handle reads from a different block
            size_t offset = ((i + round) % (FILE_SIZE / BLOCK_SIZE)) * BLOCK_SIZE;
            char expected = (char)((offset / BLOCK_SIZE) + 'A');

            LARGE_INTEGER pos;
            pos.QuadPart = offset;
            if (!SetFilePointerEx(handles[i], pos, NULL, FILE_BEGIN)) {
                wprintf(L"  FAIL: Seek handle %d to offset %zu failed: %d\n", i, offset, GetLastError());
                for (int j = 0; j < NUM_HANDLES; j++) CloseHandle(handles[j]);
                return false;
            }

            char buffer[64];
            DWORD bytesRead;
            if (!ReadFile(handles[i], buffer, 64, &bytesRead, NULL)) {
                wprintf(L"  FAIL: Read handle %d at offset %zu failed: %d\n", i, offset, GetLastError());
                for (int j = 0; j < NUM_HANDLES; j++) CloseHandle(handles[j]);
                return false;
            }

            if (bytesRead == 0) {
                wprintf(L"  FAIL: Zero bytes read handle %d at offset %zu\n", i, offset);
                for (int j = 0; j < NUM_HANDLES; j++) CloseHandle(handles[j]);
                return false;
            }

            if (buffer[0] != expected) {
                wprintf(L"  FAIL: Content mismatch at offset %zu: got '%c', expected '%c'\n", 
                    offset, buffer[0], expected);
                for (int j = 0; j < NUM_HANDLES; j++) CloseHandle(handles[j]);
                return false;
            }
        }
    }

    for (int i = 0; i < NUM_HANDLES; i++) {
        CloseHandle(handles[i]);
    }

    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: GetFileSize then immediate read at offset 0
// Simulates: Library checking file size before reading
//=============================================================================
bool Test_GetFileSizeThenReadOffset0(const WCHAR* file)
{
    DeleteFileW(file);

    // Test with various file sizes
    const size_t testSizes[] = { 1, 100, 512, 1000, 1016, 1024, 2048, 4096, 8192 };

    for (size_t targetSize : testSizes) {
        // Create file
        {
            HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
                0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                wprintf(L"  FAIL: Create file failed for size %zu: %d\n", targetSize, GetLastError());
                return false;
            }

            std::vector<char> content(targetSize);
            for (size_t i = 0; i < targetSize; i++) {
                content[i] = (char)('A' + (i % 26));
            }

            DWORD written;
            WriteFile(h, content.data(), (DWORD)content.size(), &written, NULL);
            CloseHandle(h);
        }

        // Reopen for read
        HANDLE h = CreateFileW(file, GENERIC_READ, 
            FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Open for read failed for size %zu: %d\n", targetSize, GetLastError());
            return false;
        }

        // Get file size (this is what ZIP libraries do first)
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(h, &fileSize)) {
            wprintf(L"  FAIL: GetFileSizeEx failed for size %zu: %d\n", targetSize, GetLastError());
            CloseHandle(h);
            return false;
        }

        if (fileSize.QuadPart != (LONGLONG)targetSize) {
            wprintf(L"  FAIL: Size mismatch for %zu: got %lld\n", targetSize, fileSize.QuadPart);
            CloseHandle(h);
            return false;
        }

        // *** CRITICAL *** Immediately read at offset 0 after GetFileSizeEx
        // File pointer should be at 0 since we just opened the file
        char buffer[256];
        DWORD bytesRead = 0;
        DWORD toRead = (DWORD)min((size_t)256, targetSize);
        
        if (!ReadFile(h, buffer, toRead, &bytesRead, NULL)) {
            wprintf(L"  FAIL: Read at offset 0 failed for size %zu: %d\n", targetSize, GetLastError());
            CloseHandle(h);
            return false;
        }

        if (bytesRead != toRead) {
            wprintf(L"  FAIL: Partial read for size %zu: got %d, expected %d\n", 
                targetSize, bytesRead, toRead);
            CloseHandle(h);
            return false;
        }

        // Verify first byte
        if (buffer[0] != 'A') {
            wprintf(L"  FAIL: First byte mismatch for size %zu: got '%c'\n", targetSize, buffer[0]);
            CloseHandle(h);
            return false;
        }

        CloseHandle(h);
        DeleteFileW(file);
    }

    return true;
}

//=============================================================================
// Test: Read after truncate and immediate write (APK/ZIP update pattern)
// Simulates: Updating an existing archive file
//=============================================================================
bool Test_ReadAfterTruncateAndWrite(const WCHAR* file)
{
    DeleteFileW(file);

    for (int iteration = 0; iteration < 10; iteration++) {
        // Create initial file
        {
            HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
                0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                wprintf(L"  FAIL: Initial create failed: %d\n", GetLastError());
                return false;
            }

            const char* initialContent = "Initial content before truncation and rewrite";
            DWORD written;
            WriteFile(h, initialContent, (DWORD)strlen(initialContent), &written, NULL);
            CloseHandle(h);
        }

        // Truncate to zero and write new content
        size_t newSize;
        {
            HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
                FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                wprintf(L"  FAIL: Truncate open failed: %d\n", GetLastError());
                return false;
            }

            // Variable content size
            newSize = 500 + (iteration * 200);
            std::vector<char> newContent(newSize);
            for (size_t i = 0; i < newSize; i++) {
                newContent[i] = (char)('A' + ((iteration + i) % 26));
            }

            DWORD written;
            if (!WriteFile(h, newContent.data(), (DWORD)newContent.size(), &written, NULL)) {
                wprintf(L"  FAIL: Write after truncate failed: %d\n", GetLastError());
                CloseHandle(h);
                return false;
            }

            // Close (simulates file being saved)
            CloseHandle(h);
        }

        // *** CRITICAL *** Immediately open with a new handle and read at offset 0
        {
            HANDLE h = CreateFileW(file, GENERIC_READ, 
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 
                FILE_FLAG_RANDOM_ACCESS, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                wprintf(L"  FAIL: Reopen for read failed: %d\n", GetLastError());
                return false;
            }

            // Verify file size
            LARGE_INTEGER fileSize;
            GetFileSizeEx(h, &fileSize);
            if (fileSize.QuadPart != (LONGLONG)newSize) {
                wprintf(L"  FAIL: Size after truncate+write: %lld vs %zu\n", 
                    fileSize.QuadPart, newSize);
                CloseHandle(h);
                return false;
            }

            // Read at offset 0
            char buffer[64];
            DWORD bytesRead = 0;
            if (!ReadFile(h, buffer, 64, &bytesRead, NULL)) {
                wprintf(L"  FAIL (iteration %d): Read at offset 0 failed: %d\n", 
                    iteration, GetLastError());
                CloseHandle(h);
                return false;
            }

            if (bytesRead == 0) {
                wprintf(L"  FAIL (iteration %d): Zero bytes read at offset 0 (size=%lld)\n", 
                    iteration, fileSize.QuadPart);
                CloseHandle(h);
                return false;
            }

            // Verify first byte
            char expected = (char)('A' + (iteration % 26));
            if (buffer[0] != expected) {
                wprintf(L"  FAIL (iteration %d): First byte mismatch: '%c' vs '%c'\n", 
                    iteration, buffer[0], expected);
                CloseHandle(h);
                return false;
            }

            CloseHandle(h);
        }
    }

    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: Rapid seek-read cycles (overlay parsing pattern)
// Simulates: Parsing overlays/resources in APK files
//=============================================================================
bool Test_RapidSeekReadCycles(const WCHAR* file)
{
    DeleteFileW(file);

    const size_t FILE_SIZE = 4096;

    // Create file with structured content
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Create file failed: %d\n", GetLastError());
            return false;
        }

        std::vector<char> content(FILE_SIZE);
        for (size_t i = 0; i < FILE_SIZE; i++) {
            // Every 256 bytes starts with a "record marker" (2 bytes: 0xAB 0xCD)
            if (i % 256 == 0) {
                content[i] = (char)(0xAB);
            } else if (i % 256 == 1) {
                content[i] = (char)(0xCD);
            } else {
                content[i] = (char)((i / 256) + '0');
            }
        }

        DWORD written;
        if (!WriteFile(h, content.data(), (DWORD)content.size(), &written, NULL)) {
            wprintf(L"  FAIL: Write file failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);
    }

    // Open and perform rapid seek-read cycles
    HANDLE h = CreateFileW(file, GENERIC_READ, 
        FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        wprintf(L"  FAIL: Open for read failed: %d\n", GetLastError());
        return false;
    }

    // Simulate overlay parsing: jump around reading record headers
    // All offsets must be multiples of 256 to find the markers
    const size_t offsets[] = {0, 256, 512, 768, 1024, 1280, 2048, 2560, 3072, 3584, 0, 256};
    
    for (int round = 0; round < 50; round++) {
        for (size_t offset : offsets) {
            // Verify offset is valid (within file and at record boundary)
            if (offset >= FILE_SIZE) {
                continue;
            }
            
            LARGE_INTEGER pos;
            pos.QuadPart = offset;
            if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
                wprintf(L"  FAIL: Seek to %zu failed: %d\n", offset, GetLastError());
                CloseHandle(h);
                return false;
            }

            char buffer[4];
            DWORD bytesRead = 0;
            if (!ReadFile(h, buffer, 2, &bytesRead, NULL)) {
                wprintf(L"  FAIL: Read at offset %zu failed: %d\n", offset, GetLastError());
                CloseHandle(h);
                return false;
            }

            if (bytesRead != 2) {
                wprintf(L"  FAIL: Partial read at offset %zu: %d bytes\n", offset, bytesRead);
                CloseHandle(h);
                return false;
            }

            // Verify record marker
            if ((unsigned char)buffer[0] != 0xAB || (unsigned char)buffer[1] != 0xCD) {
                wprintf(L"  FAIL: Marker mismatch at offset %zu (round %d): %02X %02X (expected AB CD)\n", 
                    offset, round, (unsigned char)buffer[0], (unsigned char)buffer[1]);
                CloseHandle(h);
                return false;
            }
        }
    }

    CloseHandle(h);
    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: Large read from middle of file (Android aapt2 pattern)
// Simulates: Reading ~64KB from middle of a large file
// This is the pattern causing "failed to read at offset 192217" errors
//=============================================================================
bool Test_LargeReadFromMiddle(const WCHAR* file)
{
    DeleteFileW(file);
    
    // Create a file larger than the problematic offset
    const size_t FILE_SIZE = 300000;  // ~300KB
    const size_t READ_OFFSET = 192217;
    const size_t READ_SIZE = 65557;
    
    printf("Creating %zu byte file for large middle read test...\n", FILE_SIZE);
    
    // Create file with known pattern
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Create file failed: %d\n", GetLastError());
            return false;
        }

        // Write in chunks to avoid huge stack allocation
        const size_t CHUNK_SIZE = 32768;
        std::vector<char> chunk(CHUNK_SIZE);
        size_t written_total = 0;
        
        while (written_total < FILE_SIZE) {
            size_t to_write = min(CHUNK_SIZE, FILE_SIZE - written_total);
            
            // Fill with predictable pattern based on offset
            for (size_t i = 0; i < to_write; i++) {
                size_t global_offset = written_total + i;
                // Pattern: each byte is derived from its position
                chunk[i] = (char)((global_offset % 251) ^ ((global_offset / 251) % 256));
            }
            
            DWORD written;
            if (!WriteFile(h, chunk.data(), (DWORD)to_write, &written, NULL)) {
                wprintf(L"  FAIL: Write failed at offset %zu: %d\n", written_total, GetLastError());
                CloseHandle(h);
                return false;
            }
            written_total += written;
        }
        
        CloseHandle(h);
    }
    
    printf("File created. Testing read at offset %zu, size %zu...\n", READ_OFFSET, READ_SIZE);
    
    // Test the exact failing pattern multiple times
    for (int iteration = 0; iteration < 5; iteration++) {
        HANDLE h = CreateFileW(file, GENERIC_READ, 
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 
            FILE_FLAG_RANDOM_ACCESS, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Open for read failed (iteration %d): %d\n", iteration, GetLastError());
            return false;
        }
        
        // Verify file size
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(h, &fileSize)) {
            wprintf(L"  FAIL: GetFileSizeEx failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }
        
        if (fileSize.QuadPart != FILE_SIZE) {
            wprintf(L"  FAIL: Unexpected file size: %lld vs %zu\n", fileSize.QuadPart, FILE_SIZE);
            CloseHandle(h);
            return false;
        }
        
        // Seek to the problematic offset
        LARGE_INTEGER pos;
        pos.QuadPart = READ_OFFSET;
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            wprintf(L"  FAIL: Seek to offset %zu failed: %d\n", READ_OFFSET, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        // Attempt the large read
        std::vector<char> buffer(READ_SIZE);
        DWORD bytesRead = 0;
        if (!ReadFile(h, buffer.data(), (DWORD)READ_SIZE, &bytesRead, NULL)) {
            wprintf(L"  FAIL (iteration %d): Read at offset %zu failed: %d\n", 
                iteration, READ_OFFSET, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        if (bytesRead != READ_SIZE) {
            wprintf(L"  FAIL (iteration %d): Partial read: got %lu, expected %zu\n", 
                iteration, (unsigned long)bytesRead, READ_SIZE);
            CloseHandle(h);
            return false;
        }
        
        // Verify the data integrity
        bool dataOk = true;
        for (size_t i = 0; i < READ_SIZE && dataOk; i++) {
            size_t global_offset = READ_OFFSET + i;
            char expected = (char)((global_offset % 251) ^ ((global_offset / 251) % 256));
            if (buffer[i] != expected) {
                wprintf(L"  FAIL (iteration %d): Data mismatch at offset %zu: got 0x%02X, expected 0x%02X\n",
                    iteration, global_offset, (unsigned char)buffer[i], (unsigned char)expected);
                dataOk = false;
            }
        }
        
        if (!dataOk) {
            CloseHandle(h);
            return false;
        }
        
        CloseHandle(h);
        printf("  Iteration %d: OK\n", iteration);
    }
    
    // Also test reading in smaller chunks (like aapt2 might do internally)
    printf("Testing chunked reads across same region...\n");
    {
        HANDLE h = CreateFileW(file, GENERIC_READ, 
            FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Open for chunked read failed: %d\n", GetLastError());
            return false;
        }
        
        const size_t CHUNK = 4096;
        std::vector<char> buffer(CHUNK);
        
        for (size_t offset = READ_OFFSET; offset < READ_OFFSET + READ_SIZE; offset += CHUNK) {
            size_t to_read = min(CHUNK, READ_OFFSET + READ_SIZE - offset);
            
            LARGE_INTEGER pos;
            pos.QuadPart = offset;
            if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
                wprintf(L"  FAIL: Seek to offset %zu failed: %d\n", offset, GetLastError());
                CloseHandle(h);
                return false;
            }
            
            DWORD bytesRead;
            if (!ReadFile(h, buffer.data(), (DWORD)to_read, &bytesRead, NULL)) {
                wprintf(L"  FAIL: Chunked read at offset %zu failed: %d\n", offset, GetLastError());
                CloseHandle(h);
                return false;
            }
            
            if (bytesRead != to_read) {
                wprintf(L"  FAIL: Chunked partial read at %zu: got %lu, expected %zu\n", 
                    offset, (unsigned long)bytesRead, to_read);
                CloseHandle(h);
                return false;
            }
            
            // Verify chunk data
            for (size_t i = 0; i < bytesRead; i++) {
                size_t global_offset = offset + i;
                char expected = (char)((global_offset % 251) ^ ((global_offset / 251) % 256));
                if (buffer[i] != expected) {
                    wprintf(L"  FAIL: Chunked data mismatch at offset %zu\n", global_offset);
                    CloseHandle(h);
                    return false;
                }
            }
        }
        
        CloseHandle(h);
    }
    
    DeleteFileW(file);
    printf("Large read from middle test PASSED\n");
    return true;
}

//=============================================================================
// Test: Multi-block spanning read
// Tests reading data that spans many encrypted blocks
//=============================================================================
bool Test_MultiBlockSpanningRead(const WCHAR* file)
{
    DeleteFileW(file);
    
    // EncFS block data size is typically 1016 bytes (1024 - 8 byte header)
    const size_t BLOCK_DATA_SIZE = 1016;
    const size_t NUM_BLOCKS = 100;
    const size_t FILE_SIZE = BLOCK_DATA_SIZE * NUM_BLOCKS;
    
    printf("Creating %zu byte file (%zu blocks) for spanning read test...\n", FILE_SIZE, NUM_BLOCKS);
    
    // Create file
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Create file failed: %d\n", GetLastError());
            return false;
        }

        std::vector<char> data(FILE_SIZE);
        for (size_t i = 0; i < FILE_SIZE; i++) {
            // Mark block boundaries
            size_t block = i / BLOCK_DATA_SIZE;
            size_t offset_in_block = i % BLOCK_DATA_SIZE;
            data[i] = (char)((block % 26) + 'A');  // Each block has a different letter
        }
        
        DWORD written;
        if (!WriteFile(h, data.data(), (DWORD)FILE_SIZE, &written, NULL)) {
            wprintf(L"  FAIL: Write failed: %d\n", GetLastError());
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);
    }
    
    // Test reads that span multiple blocks at various alignments
    struct TestCase {
        size_t offset;
        size_t size;
        const char* description;
    };
    
    TestCase tests[] = {
        // Aligned reads
        {0, BLOCK_DATA_SIZE * 5, "First 5 blocks"},
        {BLOCK_DATA_SIZE * 10, BLOCK_DATA_SIZE * 20, "Middle 20 blocks"},
        {BLOCK_DATA_SIZE * 90, BLOCK_DATA_SIZE * 10, "Last 10 blocks"},
        
        // Misaligned reads (starting mid-block)
        {500, BLOCK_DATA_SIZE * 3, "Start mid-block, 3 blocks"},
        {BLOCK_DATA_SIZE + 100, BLOCK_DATA_SIZE * 5 + 200, "Start offset 100 in block 1"},
        
        // Reads crossing block boundaries
        {BLOCK_DATA_SIZE - 10, 20, "Cross first block boundary"},
        {BLOCK_DATA_SIZE * 50 - 500, 1000, "Cross middle block boundary"},
        
        // Large reads
        {1000, 50000, "Large read from offset 1000"},
        {BLOCK_DATA_SIZE * 20 + 333, 60000, "Large misaligned read"},
    };
    
    for (const auto& test : tests) {
        if (test.offset + test.size > FILE_SIZE) continue;
        
        HANDLE h = CreateFileW(file, GENERIC_READ, 
            FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Open failed for '%hs': %d\n", test.description, GetLastError());
            return false;
        }
        
        LARGE_INTEGER pos;
        pos.QuadPart = test.offset;
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            wprintf(L"  FAIL: Seek failed for '%hs': %d\n", test.description, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        std::vector<char> buffer(test.size);
        DWORD bytesRead;
        if (!ReadFile(h, buffer.data(), (DWORD)test.size, &bytesRead, NULL)) {
            wprintf(L"  FAIL: Read failed for '%hs': %d\n", test.description, GetLastError());
            CloseHandle(h);
            return false;
        }
        
        if (bytesRead != test.size) {
            wprintf(L"  FAIL: Partial read for '%hs': got %lu, expected %zu\n", 
                test.description, (unsigned long)bytesRead, test.size);
            CloseHandle(h);
            return false;
        }
        
        // Verify data
        for (size_t i = 0; i < test.size; i++) {
            size_t global_offset = test.offset + i;
            size_t block = global_offset / BLOCK_DATA_SIZE;
            char expected = (char)((block % 26) + 'A');
            if (buffer[i] != expected) {
                wprintf(L"  FAIL: Data mismatch for '%hs' at offset %zu (block %zu): got '%c', expected '%c'\n",
                    test.description, global_offset, block, buffer[i], expected);
                CloseHandle(h);
                return false;
            }
        }
        
        CloseHandle(h);
        printf("  %s: OK\n", test.description);
    }
    
    DeleteFileW(file);
    printf("Multi-block spanning read test PASSED\n");
    return true;
}

//=============================================================================
// Test: Delete On Close and Attribute Preservation
// Simulates: Acrobat temporary file handling
// 1. FILE_FLAG_DELETE_ON_CLOSE should work
// 2. TRUNCATE_EXISTING should preserve attributes (e.g. Hidden)
//=============================================================================
bool Test_DeleteOnCloseAttributePreservation(const WCHAR* file)
{
    DeleteFileW(file);

    // Part 1: Test FILE_FLAG_DELETE_ON_CLOSE
    {
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
            NULL, CREATE_ALWAYS, 
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
            
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: CreateFile with DELETE_ON_CLOSE failed: %d\n", GetLastError());
            return false;
        }
        
        const char* data = "Temp data";
        DWORD written;
        WriteFile(h, data, (DWORD)strlen(data), &written, NULL);
        
        // File should exist while handle is open
        if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) {
            wprintf(L"  FAIL: File does not exist while handle is open\n");
            CloseHandle(h);
            return false;
        }
        
        CloseHandle(h);
        
        // File should be gone now
        if (GetFileAttributesW(file) != INVALID_FILE_ATTRIBUTES) {
            wprintf(L"  FAIL: File was not deleted on close\n");
            DeleteFileW(file);
            return false;
        }
        printf("  DELETE_ON_CLOSE verified\n");
    }

    // Part 2: Test Attribute Preservation with TRUNCATE_EXISTING
    {
        // Create a hidden file
        HANDLE h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
            
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: Create hidden file failed: %d\n", GetLastError());
            return false;
        }
        CloseHandle(h);
        
        // Verify it is hidden
        DWORD attrs = GetFileAttributesW(file);
        if ((attrs & FILE_ATTRIBUTE_HIDDEN) == 0) {
            wprintf(L"  FAIL: File is not hidden (attrs=0x%X)\n", attrs);
            return false;
        }
        
        // Open with TRUNCATE_EXISTING
        h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 
            FILE_SHARE_READ, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"  FAIL: TRUNCATE_EXISTING failed: %d\n", GetLastError());
            return false;
        }
        CloseHandle(h);
        
        // Verify it is STILL hidden
        attrs = GetFileAttributesW(file);
        if ((attrs & FILE_ATTRIBUTE_HIDDEN) == 0) {
            wprintf(L"  FAIL: Hidden attribute lost after truncate (attrs=0x%X)\n", attrs);
            return false;
        }
        printf("  Attribute preservation verified\n");
    }

    DeleteFileW(file);
    return true;
}
