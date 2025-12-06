// test_file_size.cpp - File size manipulation tests
#include "test_common.h"

//=============================================================================
// Test: Expand and shrink file size with writes
//=============================================================================
bool Test_ExpandShrink(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER pos;

    pos.QuadPart = 3686LL;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }

    pos.QuadPart = 5529LL;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }

    pos.QuadPart = 3686LL;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    {
        const DWORD size = 4;
        char data[4];
        memset(data, 'A', size);
        DWORD ioLen = 0;
        if (!WriteFile(h, data, size, &ioLen, NULL)) { PrintLastError("WriteFile"); CloseHandle(h); return false; }
    }

    pos.QuadPart = 5529LL;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }

    pos.QuadPart = 4505LL;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }

    pos.QuadPart = 3686LL;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    {
        const DWORD size = 108;
        std::vector<char> data(size, 'A');
        DWORD ioLen = 0;
        if (!WriteFile(h, data.data(), size, &ioLen, NULL)) { PrintLastError("WriteFile"); CloseHandle(h); return false; }
    }

    CloseHandle(h);
    DeleteFileW(file);
    return true;
}

//=============================================================================
// Test: SetEndOfFile followed by write (cache invalidation test)
//=============================================================================
bool Test_SetEndOfFileThenWrite(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing SetEndOfFile followed by write (cache invalidation test)\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    const char initialData[] = "INITIAL_DATA_1234567890ABCDEFGHIJ";
    const size_t initialDataLen = sizeof(initialData) - 1;  // 33 bytes
    DWORD ioLen = 0;
    if (!WriteFile(h, initialData, static_cast<DWORD>(initialDataLen), &ioLen, NULL)) {
        PrintLastError("WriteFile initial");
        CloseHandle(h);
        return false;
    }
    printf("Wrote initial %lu bytes\n", static_cast<unsigned long>(ioLen));

    LARGE_INTEGER pos;
    pos.QuadPart = 100;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile expand"); CloseHandle(h); return false; }
    printf("Expanded file to 100 bytes\n");

    pos.QuadPart = 50;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    const char middleData[] = "MIDDLE";
    if (!WriteFile(h, middleData, static_cast<DWORD>(sizeof(middleData) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile middle");
        CloseHandle(h);
        return false;
    }
    printf("Wrote '%s' at offset 50\n", middleData);

    // Shrink to 40 bytes - this should preserve first 33 bytes of initial data
    // and 7 bytes of zeros (from expansion), truncating everything else
    pos.QuadPart = 40;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile shrink"); CloseHandle(h); return false; }
    printf("Shrunk file to 40 bytes\n");

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    char readBuf[64] = {0};
    if (!ReadFile(h, readBuf, 40, &ioLen, NULL)) { PrintLastError("ReadFile verify"); CloseHandle(h); return false; }
    
    printf("Read %lu bytes: '%.*s'\n", static_cast<unsigned long>(ioLen), static_cast<int>(ioLen), readBuf);
    
    // Verify: first 33 bytes should be initial data, bytes 33-39 should be zeros
    bool dataOk = true;
    
    // Check initial data (0-32)
    if (memcmp(readBuf, initialData, initialDataLen) != 0) {
        printf("ERROR: Initial data corrupted after SetEndOfFile operations!\n");
        printf("Expected: '%s'\n", initialData);
        printf("Got: '%.*s'\n", static_cast<int>(initialDataLen), readBuf);
        dataOk = false;
    }
    
    // Check that bytes 33-39 are zeros (expanded region)
    if (dataOk) {
        for (size_t i = initialDataLen; i < 40; i++) {
            if (readBuf[i] != 0) {
                printf("ERROR: Byte at offset %zu should be zero (expanded region), got 0x%02x\n", 
                       i, static_cast<unsigned char>(readBuf[i]));
                dataOk = false;
                break;
            }
        }
    }
    
    if (dataOk) {
        printf("Data integrity verified OK\n");
    }

    CloseHandle(h);
    DeleteFileW(file);
    return dataOk;
}

//=============================================================================
// Test: Partial block write after truncation (boundary block handling)
//=============================================================================
bool Test_TruncateThenPartialWrite(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing truncate then partial write (boundary block test)\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::vector<char> initialData(2000);
    for (size_t i = 0; i < initialData.size(); i++) {
        initialData[i] = 'A' + static_cast<char>(i % 26);
    }
    DWORD ioLen = 0;
    if (!WriteFile(h, initialData.data(), static_cast<DWORD>(initialData.size()), &ioLen, NULL)) {
        PrintLastError("WriteFile initial");
        CloseHandle(h);
        return false;
    }
    printf("Wrote %lu bytes of pattern data\n", static_cast<unsigned long>(ioLen));

    LARGE_INTEGER pos;
    pos.QuadPart = 1500;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }
    printf("Truncated to 1500 bytes\n");

    pos.QuadPart = 1400;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    std::vector<char> newData(100, 'X');
    if (!WriteFile(h, newData.data(), static_cast<DWORD>(newData.size()), &ioLen, NULL)) {
        PrintLastError("WriteFile partial");
        CloseHandle(h);
        return false;
    }
    printf("Wrote 100 'X' bytes at offset 1400\n");

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    std::vector<char> verifyBuf(1500);
    if (!ReadFile(h, verifyBuf.data(), static_cast<DWORD>(verifyBuf.size()), &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(h);
        return false;
    }
    printf("Read back %lu bytes\n", static_cast<unsigned long>(ioLen));

    bool ok = true;
    for (size_t i = 0; i < 1400; i++) {
        char expected = 'A' + static_cast<char>(i % 26);
        if (verifyBuf[i] != expected) {
            printf("ERROR: Data corrupted at offset %zu: expected '%c', got '%c'\n", i, expected, verifyBuf[i]);
            ok = false;
            break;
        }
    }
    for (size_t i = 1400; i < 1500; i++) {
        if (verifyBuf[i] != 'X') {
            printf("ERROR: Written data corrupted at offset %zu: expected 'X', got '%c'\n", i, verifyBuf[i]);
            ok = false;
            break;
        }
    }

    if (ok) printf("Data integrity verified OK\n");

    CloseHandle(h);
    DeleteFileW(file);
    return ok;
}

//=============================================================================
// Test: Expand file then write beyond original size
//=============================================================================
bool Test_ExpandThenWriteBeyond(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing expand then write beyond original size\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    const char initialData[] = "SMALL";
    DWORD ioLen = 0;
    if (!WriteFile(h, initialData, static_cast<DWORD>(sizeof(initialData) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile initial");
        CloseHandle(h);
        return false;
    }
    printf("Wrote %lu bytes initially\n", static_cast<unsigned long>(ioLen));

    LARGE_INTEGER pos;
    pos.QuadPart = 10000;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }
    printf("Expanded to 10000 bytes\n");

    pos.QuadPart = 5000;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    const char farData[] = "FAR_DATA_BEYOND_ORIGINAL";
    if (!WriteFile(h, farData, static_cast<DWORD>(sizeof(farData) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile far");
        CloseHandle(h);
        return false;
    }
    printf("Wrote '%s' at offset 5000\n", farData);

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    char startBuf[16] = {0};
    if (!ReadFile(h, startBuf, static_cast<DWORD>(sizeof(initialData) - 1), &ioLen, NULL)) {
        PrintLastError("ReadFile start");
        CloseHandle(h);
        return false;
    }
    
    bool startOk = (memcmp(startBuf, initialData, sizeof(initialData) - 1) == 0);
    if (!startOk) {
        printf("ERROR: Initial data corrupted! Expected '%s', got '%.*s'\n", 
               initialData, static_cast<int>(sizeof(initialData) - 1), startBuf);
    }

    pos.QuadPart = 5000;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    char farBuf[32] = {0};
    if (!ReadFile(h, farBuf, static_cast<DWORD>(sizeof(farData) - 1), &ioLen, NULL)) {
        PrintLastError("ReadFile far");
        CloseHandle(h);
        return false;
    }
    
    bool farOk = (memcmp(farBuf, farData, sizeof(farData) - 1) == 0);
    if (!farOk) {
        printf("ERROR: Far data corrupted! Expected '%s', got '%.*s'\n", 
               farData, static_cast<int>(sizeof(farData) - 1), farBuf);
    }

    CloseHandle(h);
    
    if (startOk && farOk) printf("Data integrity verified OK\n");
    
    return startOk && farOk;
}

//=============================================================================
// Test: Multiple consecutive SetEndOfFile operations
//=============================================================================
bool Test_MultipleSetEndOfFile(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing multiple consecutive SetEndOfFile operations\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::vector<char> pattern(3000);
    for (size_t i = 0; i < pattern.size(); i++) {
        pattern[i] = 'A' + static_cast<char>(i % 26);
    }
    DWORD ioLen = 0;
    if (!WriteFile(h, pattern.data(), static_cast<DWORD>(pattern.size()), &ioLen, NULL)) {
        PrintLastError("WriteFile pattern");
        CloseHandle(h);
        return false;
    }
    printf("Wrote %lu bytes of pattern\n", static_cast<unsigned long>(ioLen));

    LARGE_INTEGER pos;
    
    // Track the minimum size we truncate to - data beyond this point is lost
    // Sequence: 5000, 2500, 4000, 1500, 3500, 2000
    // After truncating to 1500, data from 1500 onwards is permanently lost
    // Even if we expand again, that region will be zeros
    const LONGLONG sizes[] = { 5000, 2500, 4000, 1500, 3500, 2000 };
    LONGLONG minSize = 3000;  // Initial written size
    
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        pos.QuadPart = sizes[i];
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
        if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }
        printf("Set file size to %lld\n", sizes[i]);
        
        // Track minimum size (truncation point)
        if (sizes[i] < minSize) {
            minSize = sizes[i];
        }
    }
    
    printf("Minimum truncation point was %lld bytes\n", minSize);

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    
    std::vector<char> verifyBuf(2000);
    if (!ReadFile(h, verifyBuf.data(), static_cast<DWORD>(verifyBuf.size()), &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(h);
        return false;
    }
    printf("Read back %lu bytes\n", static_cast<unsigned long>(ioLen));

    bool ok = true;
    
    // Data up to minSize should be preserved (original pattern)
    for (size_t i = 0; i < static_cast<size_t>(minSize) && i < 2000; i++) {
        char expected = 'A' + static_cast<char>(i % 26);
        if (verifyBuf[i] != expected) {
            printf("ERROR: Data corrupted at offset %zu (within preserved region): expected '%c', got '%c' (0x%02x)\n", 
                   i, expected, verifyBuf[i], static_cast<unsigned char>(verifyBuf[i]));
            ok = false;
            break;
        }
    }
    
    // Data from minSize to 2000 should be zeros (expanded region after truncation)
    if (ok) {
        for (size_t i = static_cast<size_t>(minSize); i < 2000; i++) {
            if (verifyBuf[i] != 0) {
                printf("ERROR: Expanded region at offset %zu should be zero, got '%c' (0x%02x)\n", 
                       i, verifyBuf[i], static_cast<unsigned char>(verifyBuf[i]));
                ok = false;
                break;
            }
        }
    }

    if (ok) printf("Data integrity after multiple SetEndOfFile verified OK\n");

    CloseHandle(h);
    DeleteFileW(file);
    return ok;
}

//=============================================================================
// Test: Truncate at exact block boundaries
//=============================================================================
bool Test_TruncateAtBlockBoundary(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing truncate at exact block boundaries\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    const size_t dataBlockSize = 1016;
    std::vector<char> pattern(dataBlockSize * 4);
    for (size_t i = 0; i < pattern.size(); i++) {
        pattern[i] = 'A' + static_cast<char>(i % 26);
    }
    DWORD ioLen = 0;
    if (!WriteFile(h, pattern.data(), static_cast<DWORD>(pattern.size()), &ioLen, NULL)) {
        PrintLastError("WriteFile pattern");
        CloseHandle(h);
        return false;
    }
    printf("Wrote %lu bytes (4 blocks of %zu)\n", static_cast<unsigned long>(ioLen), dataBlockSize);

    LARGE_INTEGER pos;
    const size_t boundaries[] = { dataBlockSize * 3, dataBlockSize * 2, dataBlockSize * 1 };
    for (size_t i = 0; i < sizeof(boundaries)/sizeof(boundaries[0]); i++) {
        pos.QuadPart = static_cast<LONGLONG>(boundaries[i]);
        if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
        if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }
        printf("Truncated to %zu bytes (block boundary)\n", boundaries[i]);
    }

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    
    std::vector<char> verifyBuf(dataBlockSize);
    if (!ReadFile(h, verifyBuf.data(), static_cast<DWORD>(verifyBuf.size()), &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(h);
        return false;
    }
    printf("Read back %lu bytes\n", static_cast<unsigned long>(ioLen));

    bool ok = true;
    for (size_t i = 0; i < dataBlockSize; i++) {
        char expected = 'A' + static_cast<char>(i % 26);
        if (verifyBuf[i] != expected) {
            printf("ERROR: Data corrupted at offset %zu: expected '%c', got '%c'\n", i, expected, verifyBuf[i]);
            ok = false;
            break;
        }
    }

    if (ok) printf("Block boundary truncation verified OK\n");

    CloseHandle(h);
    DeleteFileW(file);
    return ok;
}

//=============================================================================
// Test: Truncate to zero then rewrite
//=============================================================================
bool Test_TruncateToZeroThenRewrite(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing truncate to zero then rewrite\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    const char initialData[] = "INITIAL_CONTENT_BEFORE_TRUNCATE";
    DWORD ioLen = 0;
    if (!WriteFile(h, initialData, static_cast<DWORD>(sizeof(initialData) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile initial");
        CloseHandle(h);
        return false;
    }
    printf("Wrote initial %lu bytes\n", static_cast<unsigned long>(ioLen));

    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile to zero"); CloseHandle(h); return false; }
    printf("Truncated to 0 bytes\n");

    const char newData[] = "NEW_CONTENT_AFTER_TRUNCATE";
    if (!WriteFile(h, newData, static_cast<DWORD>(sizeof(newData) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile new");
        CloseHandle(h);
        return false;
    }
    printf("Wrote new %lu bytes\n", static_cast<unsigned long>(ioLen));

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    
    char verifyBuf[64] = {0};
    if (!ReadFile(h, verifyBuf, static_cast<DWORD>(sizeof(newData) - 1), &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(h);
        return false;
    }
    printf("Read back %lu bytes: '%.*s'\n", static_cast<unsigned long>(ioLen), 
           static_cast<int>(sizeof(newData) - 1), verifyBuf);

    bool ok = (memcmp(verifyBuf, newData, sizeof(newData) - 1) == 0);
    if (!ok) {
        printf("ERROR: Data mismatch after truncate-to-zero and rewrite!\n");
        printf("Expected: '%s'\n", newData);
        printf("Got: '%.*s'\n", static_cast<int>(sizeof(newData) - 1), verifyBuf);
    } else {
        printf("Truncate to zero then rewrite verified OK\n");
    }

    CloseHandle(h);
    DeleteFileW(file);
    return ok;
}

//=============================================================================
// Test: Write immediately after SetEndOfFile
//=============================================================================
bool Test_WriteImmediatelyAfterSetEndOfFile(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing write immediately after SetEndOfFile\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    const char part1[] = "PART1_";
    DWORD ioLen = 0;
    if (!WriteFile(h, part1, static_cast<DWORD>(sizeof(part1) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile part1");
        CloseHandle(h);
        return false;
    }
    printf("Wrote part1: %lu bytes\n", static_cast<unsigned long>(ioLen));

    LARGE_INTEGER pos;
    pos.QuadPart = 100;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }
    printf("Expanded to 100 bytes, file pointer at 100\n");

    const char part2[] = "PART2_AT_100";
    if (!WriteFile(h, part2, static_cast<DWORD>(sizeof(part2) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile part2");
        CloseHandle(h);
        return false;
    }
    printf("Wrote part2 at position 100: %lu bytes\n", static_cast<unsigned long>(ioLen));

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    
    char buf1[16] = {0};
    if (!ReadFile(h, buf1, static_cast<DWORD>(sizeof(part1) - 1), &ioLen, NULL)) {
        PrintLastError("ReadFile part1");
        CloseHandle(h);
        return false;
    }
    bool part1Ok = (memcmp(buf1, part1, sizeof(part1) - 1) == 0);
    printf("Part1 at 0: '%.*s' - %s\n", static_cast<int>(sizeof(part1) - 1), buf1, part1Ok ? "OK" : "ERROR");

    pos.QuadPart = 100;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    
    char buf2[32] = {0};
    if (!ReadFile(h, buf2, static_cast<DWORD>(sizeof(part2) - 1), &ioLen, NULL)) {
        PrintLastError("ReadFile part2");
        CloseHandle(h);
        return false;
    }
    bool part2Ok = (memcmp(buf2, part2, sizeof(part2) - 1) == 0);
    printf("Part2 at 100: '%.*s' - %s\n", static_cast<int>(sizeof(part2) - 1), buf2, part2Ok ? "OK" : "ERROR");

    CloseHandle(h);
    DeleteFileW(file);
    return part1Ok && part2Ok;
}

//=============================================================================
// Test: SetEndOfFile(0) + SetAllocationSize(0) + Write
// This pattern was observed in VS Project loading debug log
//=============================================================================
bool Test_TruncateZeroAllocWrite(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing SetEndOfFile(0) + SetAllocationSize(0) + Write pattern\n");
    printf("(This pattern is used by Visual Studio when updating project files)\n");
    
    // First, create a file with some content
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    const char initialData[] = "INITIAL_CONTENT_TO_BE_REPLACED_COMPLETELY";
    DWORD ioLen = 0;
    if (!WriteFile(h, initialData, static_cast<DWORD>(sizeof(initialData) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile initial");
        CloseHandle(h);
        return false;
    }
    printf("Wrote initial %lu bytes\n", static_cast<unsigned long>(ioLen));
    CloseHandle(h);
    
    // Reopen with OPEN_ALWAYS behavior
    h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                     FILE_SHARE_READ | FILE_SHARE_WRITE,  // Allow sharing
                     OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    // SetEndOfFile to 0
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx");
        CloseHandle(h);
        return false;
    }
    if (!SetEndOfFile(h)) {
        PrintLastError("SetEndOfFile(0)");
        CloseHandle(h);
        return false;
    }
    printf("SetEndOfFile(0) succeeded\n");
    
    // SetAllocationSize to 0 using FILE_ALLOCATION_INFO
    FILE_ALLOCATION_INFO allocInfo;
    allocInfo.AllocationSize.QuadPart = 0;
    if (!SetFileInformationByHandle(h, FileAllocationInfo, &allocInfo, sizeof(allocInfo))) {
        // This might fail on some filesystems, which is OK
        printf("SetAllocationSize(0) returned error %lu (may be expected)\n", GetLastError());
    } else {
        printf("SetAllocationSize(0) succeeded\n");
    }
    
    // Now write new content
    const char newData[] = "{\"key\": \"value\", \"array\": [1, 2, 3, 4, 5], \"nested\": {\"a\": 1, \"b\": 2}}";
    const DWORD newDataLen = static_cast<DWORD>(sizeof(newData) - 1);
    
    if (!WriteFile(h, newData, newDataLen, &ioLen, NULL)) {
        DWORD err = GetLastError();
        printf("WriteFile failed with error %lu\n", err);
        if (err == 1392) {
            printf("ERROR 1392 (ERROR_FILE_CORRUPT) - This is the bug we're testing for!\n");
        }
        CloseHandle(h);
        return false;
    }
    printf("Wrote %lu bytes after truncate-to-zero\n", static_cast<unsigned long>(ioLen));
    
    // Verify the data
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx verify");
        CloseHandle(h);
        return false;
    }
    
    std::vector<char> verifyBuf(newDataLen + 1, 0);
    if (!ReadFile(h, verifyBuf.data(), newDataLen, &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(h);
        return false;
    }
    
    bool dataOk = (ioLen == newDataLen && memcmp(verifyBuf.data(), newData, newDataLen) == 0);
    if (!dataOk) {
        printf("ERROR: Data verification failed!\n");
        printf("Expected %lu bytes: '%s'\n", static_cast<unsigned long>(newDataLen), newData);
        printf("Got %lu bytes: '%.*s'\n", static_cast<unsigned long>(ioLen), 
               static_cast<int>(ioLen), verifyBuf.data());
    } else {
        printf("Data verification OK\n");
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    return dataOk;
}

//=============================================================================
// Test: Large write after truncate to zero
// Tests the case where we truncate a file and then write a large amount of data
//=============================================================================
bool Test_LargeWriteAfterTruncateZero(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing large write after truncate to zero\n");
    
    // Create file with initial content
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    // Write 10KB of initial data
    std::vector<char> initialData(10 * 1024);
    for (size_t i = 0; i < initialData.size(); i++) {
        initialData[i] = 'A' + static_cast<char>(i % 26);
    }
    DWORD ioLen = 0;
    if (!WriteFile(h, initialData.data(), static_cast<DWORD>(initialData.size()), &ioLen, NULL)) {
        PrintLastError("WriteFile initial");
        CloseHandle(h);
        return false;
    }
    printf("Wrote initial %lu bytes\n", static_cast<unsigned long>(ioLen));
    
    // Truncate to zero
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx");
        CloseHandle(h);
        return false;
    }
    if (!SetEndOfFile(h)) {
        PrintLastError("SetEndOfFile(0)");
        CloseHandle(h);
        return false;
    }
    printf("Truncated to 0 bytes\n");
    
    // Write 15KB of new data (larger than original)
    std::vector<char> newData(15 * 1024);
    for (size_t i = 0; i < newData.size(); i++) {
        newData[i] = '0' + static_cast<char>(i % 10);
    }
    
    if (!WriteFile(h, newData.data(), static_cast<DWORD>(newData.size()), &ioLen, NULL)) {
        DWORD err = GetLastError();
        printf("WriteFile failed with error %lu\n", err);
        CloseHandle(h);
        return false;
    }
    printf("Wrote new %lu bytes after truncate\n", static_cast<unsigned long>(ioLen));
    
    // Verify file size
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(h, &fileSize)) {
        PrintLastError("GetFileSizeEx");
        CloseHandle(h);
        return false;
    }
    printf("File size: %lld bytes\n", fileSize.QuadPart);
    
    if (fileSize.QuadPart != static_cast<LONGLONG>(newData.size())) {
        printf("ERROR: File size mismatch! Expected %zu, got %lld\n", 
               newData.size(), fileSize.QuadPart);
        CloseHandle(h);
        return false;
    }
    
    // Verify data content
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx verify");
        CloseHandle(h);
        return false;
    }
    
    std::vector<char> verifyBuf(newData.size());
    if (!ReadFile(h, verifyBuf.data(), static_cast<DWORD>(verifyBuf.size()), &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(h);
        return false;
    }
    
    bool dataOk = (ioLen == static_cast<DWORD>(newData.size()) && 
                   memcmp(verifyBuf.data(), newData.data(), newData.size()) == 0);
    if (!dataOk) {
        printf("ERROR: Data verification failed!\n");
        // Find first mismatch
        for (size_t i = 0; i < newData.size() && i < ioLen; i++) {
            if (verifyBuf[i] != newData[i]) {
                printf("First mismatch at offset %zu: expected 0x%02x, got 0x%02x\n",
                       i, static_cast<unsigned char>(newData[i]), 
                       static_cast<unsigned char>(verifyBuf[i]));
                break;
            }
        }
    } else {
        printf("Data verification OK\n");
    }
    
    CloseHandle(h);
    DeleteFileW(file);
    return dataOk;
}

//=============================================================================
// Test: Rapid open-close-reopen cycle
// Tests that file state is correctly persisted between handle close/reopen
//=============================================================================
bool Test_RapidOpenCloseReopen(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing rapid open-close-reopen cycle\n");
    
    const int iterations = 10;
    bool allOk = true;
    
    for (int iter = 0; iter < iterations && allOk; iter++) {
        // Create/truncate and write
        HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Iteration %d: Failed to create file\n", iter);
            return false;
        }
        
        char data[64];
        snprintf(data, sizeof(data), "ITERATION_%d_DATA_%08X", iter, iter * 12345);
        DWORD dataLen = static_cast<DWORD>(strlen(data));
        DWORD ioLen = 0;
        
        if (!WriteFile(h, data, dataLen, &ioLen, NULL)) {
            PrintLastError("WriteFile");
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);
        
        // Immediately reopen and verify
        h = OpenTestFile(file, GENERIC_READ, 
                         FILE_SHARE_READ | FILE_SHARE_WRITE, 
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Iteration %d: Failed to reopen file\n", iter);
            return false;
        }
        
        char verifyBuf[64] = {0};
        if (!ReadFile(h, verifyBuf, dataLen, &ioLen, NULL)) {
            PrintLastError("ReadFile");
            CloseHandle(h);
            return false;
        }
        
        if (ioLen != dataLen || memcmp(verifyBuf, data, dataLen) != 0) {
            printf("Iteration %d: Data mismatch!\n", iter);
            printf("  Expected: '%s'\n", data);
            printf("  Got: '%.*s'\n", static_cast<int>(ioLen), verifyBuf);
            allOk = false;
        }
        
        CloseHandle(h);
    }
    
    if (allOk) {
        printf("All %d iterations passed\n", iterations);
    }
    
    DeleteFileW(file);
    return allOk;
}

//=============================================================================
// Test: Multiple concurrent file handles
// Tests that multiple handles to the same file work correctly
//=============================================================================
bool Test_MultipleConcurrentHandles(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing multiple concurrent file handles\n");
    
    // Create file
    HANDLE h1 = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                             FILE_SHARE_READ | FILE_SHARE_WRITE, 
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h1 == INVALID_HANDLE_VALUE) return false;
    
    // Write initial data through h1
    const char data1[] = "WRITTEN_BY_HANDLE_1";
    DWORD ioLen = 0;
    if (!WriteFile(h1, data1, static_cast<DWORD>(sizeof(data1) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile h1");
        CloseHandle(h1);
        return false;
    }
    printf("Handle 1 wrote: '%s'\n", data1);
    
    // Open second handle
    HANDLE h2 = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, 
                             FILE_SHARE_READ | FILE_SHARE_WRITE, 
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h2 == INVALID_HANDLE_VALUE) {
        CloseHandle(h1);
        return false;
    }
    
    // Read through h2
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h2, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx h2");
        CloseHandle(h1);
        CloseHandle(h2);
        return false;
    }
    
    char readBuf[64] = {0};
    if (!ReadFile(h2, readBuf, static_cast<DWORD>(sizeof(data1) - 1), &ioLen, NULL)) {
        PrintLastError("ReadFile h2");
        CloseHandle(h1);
        CloseHandle(h2);
        return false;
    }
    printf("Handle 2 read: '%.*s'\n", static_cast<int>(ioLen), readBuf);
    
    bool read1Ok = (memcmp(readBuf, data1, sizeof(data1) - 1) == 0);
    if (!read1Ok) {
        printf("ERROR: Handle 2 read incorrect data!\n");
    }
    
    // Write more data through h2
    const char data2[] = "_AND_HANDLE_2";
    if (!WriteFile(h2, data2, static_cast<DWORD>(sizeof(data2) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile h2");
        CloseHandle(h1);
        CloseHandle(h2);
        return false;
    }
    printf("Handle 2 appended: '%s'\n", data2);
    
    // Read entire file through h1
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h1, pos, NULL, FILE_BEGIN)) {
        PrintLastError("SetFilePointerEx h1");
        CloseHandle(h1);
        CloseHandle(h2);
        return false;
    }
    
    memset(readBuf, 0, sizeof(readBuf));
    const DWORD expectedLen = static_cast<DWORD>(sizeof(data1) - 1 + sizeof(data2) - 1);
    if (!ReadFile(h1, readBuf, expectedLen, &ioLen, NULL)) {
        PrintLastError("ReadFile h1");
        CloseHandle(h1);
        CloseHandle(h2);
        return false;
    }
    printf("Handle 1 read entire file: '%.*s'\n", static_cast<int>(ioLen), readBuf);
    
    // Verify combined content
    char expected[64];
    snprintf(expected, sizeof(expected), "%s%s", data1, data2);
    bool combinedOk = (ioLen == expectedLen && memcmp(readBuf, expected, expectedLen) == 0);
    if (!combinedOk) {
        printf("ERROR: Combined data mismatch!\n");
        printf("  Expected: '%s'\n", expected);
        printf("  Got: '%.*s'\n", static_cast<int>(ioLen), readBuf);
    }
    
    CloseHandle(h1);
    CloseHandle(h2);
    
    if (read1Ok && combinedOk) {
        printf("Multiple concurrent handles test OK\n");
    }
    
    DeleteFileW(file);
    return read1Ok && combinedOk;
}
