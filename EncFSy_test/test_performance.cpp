// test_performance.cpp - Performance tests for large file operations
// These tests measure throughput and identify potential performance bottlenecks

#include "test_common.h"
#include <chrono>
#include <vector>
#include <numeric>
#include <atomic>
#include <thread>
#include <algorithm>

using namespace std::chrono;

//=============================================================================
// Helper: Format bytes per second
//=============================================================================
static void FormatThroughput(double bytesPerSecond, char* buffer, size_t bufSize) {
    if (bytesPerSecond >= 1024.0 * 1024.0 * 1024.0) {
        snprintf(buffer, bufSize, "%.2f GB/s", bytesPerSecond / (1024.0 * 1024.0 * 1024.0));
    } else if (bytesPerSecond >= 1024.0 * 1024.0) {
        snprintf(buffer, bufSize, "%.2f MB/s", bytesPerSecond / (1024.0 * 1024.0));
    } else if (bytesPerSecond >= 1024.0) {
        snprintf(buffer, bufSize, "%.2f KB/s", bytesPerSecond / 1024.0);
    } else {
        snprintf(buffer, bufSize, "%.2f B/s", bytesPerSecond);
    }
}

//=============================================================================
// Test: Sequential write performance
// Measures throughput for sequential writes of various sizes
//=============================================================================
bool Test_SequentialWritePerformance(const WCHAR* file)
{
    printf("Testing sequential write performance...\n");
    
    DeleteFileW(file);
    
    // Test different write sizes
    const size_t testSizes[] = { 
        1 * 1024,           // 1KB
        4 * 1024,           // 4KB
        64 * 1024,          // 64KB
        1 * 1024 * 1024,    // 1MB
        4 * 1024 * 1024,    // 4MB
        16 * 1024 * 1024    // 16MB
    };
    
    const size_t totalWriteSize = 32 * 1024 * 1024;  // 32MB total per test
    
    for (size_t writeSize : testSizes) {
        DeleteFileW(file);
        
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to create file for %zu byte writes\n", writeSize);
            continue;
        }
        
        // Prepare write buffer
        std::vector<char> writeBuffer;
        try {
            writeBuffer.resize(writeSize);
            for (size_t i = 0; i < writeSize; i++) {
                writeBuffer[i] = static_cast<char>((i * 17 + 31) & 0xFF);
            }
        } catch (const std::bad_alloc&) {
            printf("  Skipping %zu byte writes (out of memory)\n", writeSize);
            CloseHandle(h);
            continue;
        }
        
        size_t iterations = totalWriteSize / writeSize;
        size_t totalWritten = 0;
        
        auto start = high_resolution_clock::now();
        
        for (size_t i = 0; i < iterations; i++) {
            DWORD written = 0;
            if (!WriteFile(h, writeBuffer.data(), static_cast<DWORD>(writeSize), &written, NULL)) {
                printf("  Write failed at iteration %zu\n", i);
                CloseHandle(h);
                DeleteFileW(file);
                return false;
            }
            totalWritten += written;
        }
        
        // Flush to ensure all data is written
        FlushFileBuffers(h);
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        double seconds = duration.count() / 1000000.0;
        double throughput = totalWritten / seconds;
        
        char throughputStr[32];
        FormatThroughput(throughput, throughputStr, sizeof(throughputStr));
        
        printf("  Write size %6zu bytes: %s (%.3f sec for %zu bytes)\n",
               writeSize, throughputStr, seconds, totalWritten);
        
        CloseHandle(h);
    }
    
    DeleteFileW(file);
    printf("Sequential write performance test completed\n");
    return true;
}

//=============================================================================
// Test: Sequential read performance
// Measures throughput for sequential reads of various sizes
//=============================================================================
bool Test_SequentialReadPerformance(const WCHAR* file)
{
    printf("Testing sequential read performance...\n");
    
    DeleteFileW(file);
    
    const size_t fileSize = 32 * 1024 * 1024;  // 32MB test file
    
    // First, create the test file
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to create test file\n");
            return false;
        }
        
        std::vector<char> writeBuffer(1024 * 1024);  // 1MB chunks
        for (size_t i = 0; i < writeBuffer.size(); i++) {
            writeBuffer[i] = static_cast<char>((i * 17 + 31) & 0xFF);
        }
        
        size_t written = 0;
        while (written < fileSize) {
            DWORD w = 0;
            size_t remaining = fileSize - written;
            DWORD toWrite = static_cast<DWORD>(remaining < writeBuffer.size() ? remaining : writeBuffer.size());
            if (!WriteFile(h, writeBuffer.data(), toWrite, &w, NULL)) {
                printf("  Failed to write test file\n");
                CloseHandle(h);
                DeleteFileW(file);
                return false;
            }
            written += w;
        }
        
        CloseHandle(h);
        printf("  Created %zu byte test file\n", fileSize);
    }
    
    // Test different read sizes
    const size_t readSizes[] = { 
        1 * 1024,           // 1KB
        4 * 1024,           // 4KB
        64 * 1024,          // 64KB
        1 * 1024 * 1024,    // 1MB
        4 * 1024 * 1024,    // 4MB
        16 * 1024 * 1024    // 16MB
    };
    
    for (size_t readSize : readSizes) {
        HANDLE h = OpenTestFile(file, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to open file for %zu byte reads\n", readSize);
            continue;
        }
        
        std::vector<char> readBuffer;
        try {
            readBuffer.resize(readSize);
        } catch (const std::bad_alloc&) {
            printf("  Skipping %zu byte reads (out of memory)\n", readSize);
            CloseHandle(h);
            continue;
        }
        
        size_t totalRead = 0;
        
        auto start = high_resolution_clock::now();
        
        while (totalRead < fileSize) {
            DWORD bytesRead = 0;
            size_t remaining = fileSize - totalRead;
            DWORD toRead = static_cast<DWORD>(remaining < readSize ? remaining : readSize);
            if (!ReadFile(h, readBuffer.data(), toRead, &bytesRead, NULL)) {
                printf("  Read failed\n");
                CloseHandle(h);
                DeleteFileW(file);
                return false;
            }
            if (bytesRead == 0) break;
            totalRead += bytesRead;
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        double seconds = duration.count() / 1000000.0;
        double throughput = totalRead / seconds;
        
        char throughputStr[32];
        FormatThroughput(throughput, throughputStr, sizeof(throughputStr));
        
        printf("  Read size %6zu bytes: %s (%.3f sec for %zu bytes)\n",
               readSize, throughputStr, seconds, totalRead);
        
        CloseHandle(h);
    }
    
    DeleteFileW(file);
    printf("Sequential read performance test completed\n");
    return true;
}

//=============================================================================
// Test: Random access read performance
// Measures throughput for random offset reads
//=============================================================================
bool Test_RandomReadPerformance(const WCHAR* file)
{
    printf("Testing random access read performance...\n");
    
    DeleteFileW(file);
    
    const size_t fileSize = 16 * 1024 * 1024;  // 16MB test file
    const size_t readSize = 4096;               // 4KB reads
    const int numReads = 1000;                  // 1000 random reads
    
    // Create test file
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to create test file\n");
            return false;
        }
        
        // Use SetEndOfFile to quickly create a large sparse file
        LARGE_INTEGER pos;
        pos.QuadPart = fileSize;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        
        // Write some data at various positions
        std::vector<char> data(readSize, 'X');
        for (size_t offset = 0; offset < fileSize; offset += fileSize / 16) {
            pos.QuadPart = offset;
            SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
            DWORD written;
            WriteFile(h, data.data(), static_cast<DWORD>(readSize), &written, NULL);
        }
        
        CloseHandle(h);
        printf("  Created %zu byte test file\n", fileSize);
    }
    
    // Generate random offsets
    std::vector<size_t> offsets(numReads);
    srand(12345);  // Fixed seed for reproducibility
    for (int i = 0; i < numReads; i++) {
        offsets[i] = (rand() % (fileSize / readSize)) * readSize;
    }
    
    // Perform random reads
    HANDLE h = OpenTestFile(file, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  Failed to open file\n");
        DeleteFileW(file);
        return false;
    }
    
    std::vector<char> readBuffer(readSize);
    size_t totalRead = 0;
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < numReads; i++) {
        LARGE_INTEGER pos;
        pos.QuadPart = offsets[i];
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
            printf("  Seek failed at iteration %d\n", i);
            CloseHandle(h);
            DeleteFileW(file);
            return false;
        }
        
        DWORD bytesRead = 0;
        if (!ReadFile(h, readBuffer.data(), static_cast<DWORD>(readSize), &bytesRead, NULL)) {
            printf("  Read failed at iteration %d\n", i);
            CloseHandle(h);
            DeleteFileW(file);
            return false;
        }
        totalRead += bytesRead;
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    
    double seconds = duration.count() / 1000000.0;
    double throughput = totalRead / seconds;
    double iops = numReads / seconds;
    
    char throughputStr[32];
    FormatThroughput(throughput, throughputStr, sizeof(throughputStr));
    
    printf("  Random reads: %s, %.0f IOPS (%.3f sec for %d reads)\n",
           throughputStr, iops, seconds, numReads);
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("Random read performance test completed\n");
    return true;
}

//=============================================================================
// Test: Large single read performance
// Tests how performance degrades with very large single read requests
//=============================================================================
bool Test_LargeSingleReadPerformance(const WCHAR* file)
{
    printf("Testing large single read performance...\n");
    
    DeleteFileW(file);
    
    const size_t maxTestSize = 64 * 1024 * 1024;  // 64MB max
    
    // Create test file
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to create test file\n");
            return false;
        }
        
        std::vector<char> writeBuffer(1024 * 1024);  // 1MB chunks
        for (size_t i = 0; i < writeBuffer.size(); i++) {
            writeBuffer[i] = static_cast<char>((i * 17 + 31) & 0xFF);
        }
        
        size_t written = 0;
        while (written < maxTestSize) {
            DWORD w = 0;
            if (!WriteFile(h, writeBuffer.data(), static_cast<DWORD>(writeBuffer.size()), &w, NULL)) {
                printf("  Failed to write test file\n");
                CloseHandle(h);
                DeleteFileW(file);
                return false;
            }
            written += w;
        }
        
        CloseHandle(h);
        printf("  Created %zu byte test file\n", maxTestSize);
    }
    
    // Test progressively larger single reads
    const size_t testSizes[] = {
        1 * 1024 * 1024,    // 1MB
        4 * 1024 * 1024,    // 4MB
        8 * 1024 * 1024,    // 8MB
        16 * 1024 * 1024,   // 16MB
        32 * 1024 * 1024,   // 32MB
        64 * 1024 * 1024    // 64MB
    };
    
    for (size_t readSize : testSizes) {
        HANDLE h = OpenTestFile(file, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to open file\n");
            continue;
        }
        
        std::vector<char> readBuffer;
        try {
            readBuffer.resize(readSize);
        } catch (const std::bad_alloc&) {
            printf("  Skipping %zu byte read (out of memory)\n", readSize);
            CloseHandle(h);
            continue;
        }
        
        auto start = high_resolution_clock::now();
        
        DWORD bytesRead = 0;
        BOOL success = ReadFile(h, readBuffer.data(), static_cast<DWORD>(readSize), &bytesRead, NULL);
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        if (!success) {
            DWORD err = GetLastError();
            printf("  %6zu MB read: FAILED (error %lu)\n", readSize / (1024 * 1024), err);
        } else {
            double seconds = duration.count() / 1000000.0;
            double throughput = bytesRead / seconds;
            
            char throughputStr[32];
            FormatThroughput(throughput, throughputStr, sizeof(throughputStr));
            
            printf("  %6zu MB read: %s (%.3f sec)\n",
                   readSize / (1024 * 1024), throughputStr, seconds);
        }
        
        CloseHandle(h);
    }
    
    DeleteFileW(file);
    printf("Large single read performance test completed\n");
    return true;
}

//=============================================================================
// Test: File resize performance
// Measures time to expand and shrink files
//=============================================================================
bool Test_FileResizePerformance(const WCHAR* file)
{
    printf("Testing file resize performance...\n");
    
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  Failed to create file\n");
        return false;
    }
    
    // Test expansion
    const size_t expansionSizes[] = {
        1 * 1024 * 1024,    // 1MB
        10 * 1024 * 1024,   // 10MB
        50 * 1024 * 1024,   // 50MB
        100 * 1024 * 1024   // 100MB
    };
    
    printf("  Expansion tests:\n");
    for (size_t targetSize : expansionSizes) {
        // Reset to zero
        LARGE_INTEGER pos;
        pos.QuadPart = 0;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        
        auto start = high_resolution_clock::now();
        
        pos.QuadPart = targetSize;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        BOOL success = SetEndOfFile(h);
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        if (!success) {
            DWORD err = GetLastError();
            if (err == ERROR_DISK_FULL) {
                printf("    %6zu MB: SKIPPED (disk full)\n", targetSize / (1024 * 1024));
                break;
            }
            printf("    %6zu MB: FAILED (error %lu)\n", targetSize / (1024 * 1024), err);
        } else {
            printf("    %6zu MB: %.3f ms\n", targetSize / (1024 * 1024), duration.count() / 1000.0);
        }
    }
    
    // Test shrinking
    printf("  Shrink tests (from 100MB):\n");
    
    // First expand to 100MB
    LARGE_INTEGER pos;
    pos.QuadPart = 100 * 1024 * 1024;
    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
    if (!SetEndOfFile(h)) {
        printf("    Could not create 100MB file for shrink test\n");
        CloseHandle(h);
        DeleteFileW(file);
        return true;  // Not a failure, just skip
    }
    
    const size_t shrinkSizes[] = {
        50 * 1024 * 1024,   // 50MB
        10 * 1024 * 1024,   // 10MB
        1 * 1024 * 1024,    // 1MB
        0                   // 0 bytes
    };
    
    for (size_t targetSize : shrinkSizes) {
        auto start = high_resolution_clock::now();
        
        pos.QuadPart = targetSize;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        SetEndOfFile(h);
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        printf("    to %6zu MB: %.3f ms\n", targetSize / (1024 * 1024), duration.count() / 1000.0);
        
        // Restore to 100MB for next test
        if (targetSize != 0) {
            pos.QuadPart = 100 * 1024 * 1024;
            SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
            SetEndOfFile(h);
        }
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("File resize performance test completed\n");
    return true;
}

//=============================================================================
// Test: Memory allocation impact
// Tests if large reads cause excessive memory allocation
//=============================================================================
bool Test_MemoryAllocationImpact(const WCHAR* file)
{
    printf("Testing memory allocation impact...\n");
    
    DeleteFileW(file);
    
    const size_t fileSize = 8 * 1024 * 1024;  // 8MB file
    const size_t readSize = 4096;              // 4KB reads
    const int iterations = 5000;               // 5000 iterations
    
    // Create test file
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to create test file\n");
            return false;
        }
        
        std::vector<char> data(fileSize, 'X');
        DWORD written;
        WriteFile(h, data.data(), static_cast<DWORD>(fileSize), &written, NULL);
        CloseHandle(h);
    }
    
    // Open file and perform many small reads
    HANDLE h = OpenTestFile(file, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  Failed to open file\n");
        DeleteFileW(file);
        return false;
    }
    
    std::vector<char> readBuffer(readSize);
    
    // Warmup
    for (int i = 0; i < 100; i++) {
        LARGE_INTEGER pos;
        pos.QuadPart = (i * readSize) % fileSize;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        DWORD bytesRead;
        ReadFile(h, readBuffer.data(), static_cast<DWORD>(readSize), &bytesRead, NULL);
    }
    
    // Measure
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        LARGE_INTEGER pos;
        pos.QuadPart = (i * readSize) % fileSize;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        DWORD bytesRead;
        if (!ReadFile(h, readBuffer.data(), static_cast<DWORD>(readSize), &bytesRead, NULL)) {
            printf("  Read failed at iteration %d\n", i);
            CloseHandle(h);
            DeleteFileW(file);
            return false;
        }
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    
    double totalRead = static_cast<double>(iterations) * readSize;
    double seconds = duration.count() / 1000000.0;
    double throughput = totalRead / seconds;
    double avgLatency = static_cast<double>(duration.count()) / iterations;
    
    char throughputStr[32];
    FormatThroughput(throughput, throughputStr, sizeof(throughputStr));
    
    printf("  %d x %zu byte reads: %s, avg latency %.1f us\n",
           iterations, readSize, throughputStr, avgLatency);
    
    CloseHandle(h);
    DeleteFileW(file);
    printf("Memory allocation impact test completed\n");
    return true;
}

//=============================================================================
// Test: Concurrent read/write performance
// Tests performance impact of concurrent operations
//=============================================================================
bool Test_ConcurrentIOPerformance(const WCHAR* file)
{
    printf("Testing concurrent I/O performance...\n");
    
    DeleteFileW(file);
    
    const size_t fileSize = 16 * 1024 * 1024;  // 16MB
    const size_t chunkSize = 64 * 1024;         // 64KB chunks
    const int numThreads = 4;
    
    // Create test file
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to create test file\n");
            return false;
        }
        
        std::vector<char> data(fileSize, 'X');
        DWORD written;
        WriteFile(h, data.data(), static_cast<DWORD>(fileSize), &written, NULL);
        CloseHandle(h);
    }
    
    // Single-threaded baseline
    {
        HANDLE h = OpenTestFile(file, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  Failed to open file\n");
            DeleteFileW(file);
            return false;
        }
        
        std::vector<char> buffer(chunkSize);
        size_t totalRead = 0;
        
        auto start = high_resolution_clock::now();
        
        while (totalRead < fileSize) {
            DWORD bytesRead;
            if (!ReadFile(h, buffer.data(), static_cast<DWORD>(chunkSize), &bytesRead, NULL)) {
                break;
            }
            if (bytesRead == 0) break;
            totalRead += bytesRead;
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        double seconds = duration.count() / 1000000.0;
        double throughput = totalRead / seconds;
        
        char throughputStr[32];
        FormatThroughput(throughput, throughputStr, sizeof(throughputStr));
        
        printf("  Single-threaded read: %s\n", throughputStr);
        
        CloseHandle(h);
    }
    
    // Multi-threaded (multiple handles)
    {
        std::vector<HANDLE> handles(numThreads);
        for (int i = 0; i < numThreads; i++) {
            handles[i] = OpenTestFile(file, GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
            if (handles[i] == INVALID_HANDLE_VALUE) {
                printf("  Failed to open handle %d\n", i);
                for (int j = 0; j < i; j++) CloseHandle(handles[j]);
                DeleteFileW(file);
                return false;
            }
        }
        
        std::atomic<size_t> totalRead(0);
        std::atomic<bool> hasError(false);
        
        auto start = high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int t = 0; t < numThreads; t++) {
            threads.emplace_back([&handles, &totalRead, &hasError, t, numThreads, fileSize, chunkSize]() {
                std::vector<char> buffer(chunkSize);
                size_t offset = (t * fileSize) / numThreads;
                size_t endOffset = ((t + 1) * fileSize) / numThreads;
                
                LARGE_INTEGER pos;
                pos.QuadPart = offset;
                SetFilePointerEx(handles[t], pos, NULL, FILE_BEGIN);
                
                while (offset < endOffset && !hasError.load()) {
                    DWORD bytesRead;
                    if (!ReadFile(handles[t], buffer.data(), static_cast<DWORD>(chunkSize), &bytesRead, NULL)) {
                        hasError.store(true);
                        break;
                    }
                    if (bytesRead == 0) break;
                    totalRead.fetch_add(bytesRead);
                    offset += bytesRead;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        double seconds = duration.count() / 1000000.0;
        double throughput = totalRead.load() / seconds;
        
        char throughputStr[32];
        FormatThroughput(throughput, throughputStr, sizeof(throughputStr));
        
        printf("  %d-thread parallel read: %s\n", numThreads, throughputStr);
        
        for (int i = 0; i < numThreads; i++) {
            CloseHandle(handles[i]);
        }
    }
    
    DeleteFileW(file);
    printf("Concurrent I/O performance test completed\n");
    return true;
}
