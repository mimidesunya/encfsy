// test_multithread.cpp - Multi-threaded and multi-handle tests
// These tests verify:
// 1. Multi-handle read/write synchronization
// 2. Thread safety and race conditions
// 3. Concurrent file access patterns

#include "test_common.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

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
