// test_advanced.cpp - Advanced and scenario-based tests
#include "test_common.h"

//=============================================================================
// Test: Word-like save pattern (rename + write + rename)
//=============================================================================
bool Test_WordLikeSavePattern(const WCHAR* rootDir)
{
    printf("Testing Word-like save pattern (rename + write + rename)\n");
    
    WCHAR originalFile[260];
    WCHAR tempFile1[260];
    WCHAR tempFile2[260];
    wsprintfW(originalFile, L"%s\\document.doc", rootDir);
    wsprintfW(tempFile1, L"%s\\~WRL0001.tmp", rootDir);
    wsprintfW(tempFile2, L"%s\\~WRD0000.tmp", rootDir);

    // Ensure clean state
    DeleteFileW(originalFile);
    DeleteFileW(tempFile1);
    DeleteFileW(tempFile2);

    HANDLE h = OpenTestFile(originalFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const char originalContent[] = "Original document content - this should be preserved in backup";
    DWORD ioLen = 0;
    if (!WriteFile(h, originalContent, static_cast<DWORD>(sizeof(originalContent) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile original");
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    printf("Created original document: %lu bytes\n", static_cast<unsigned long>(sizeof(originalContent) - 1));

    // Step 1: Rename original to backup
    if (!MoveFileExW(originalFile, tempFile1, MOVEFILE_REPLACE_EXISTING)) {
        PrintLastError("MoveFileExW (backup)");
        return false;
    }
    printf("Renamed original to backup\n");

    // Step 2: Create new temp file and write
    h = OpenTestFile(tempFile2, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER pos;
    pos.QuadPart = 5000;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile expand"); CloseHandle(h); return false; }
    printf("Expanded temp file to 5000 bytes\n");

    const char newContent[] = "New document content after edit - completely different data";
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!WriteFile(h, newContent, static_cast<DWORD>(sizeof(newContent) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile new content");
        CloseHandle(h);
        return false;
    }
    printf("Wrote new content at start\n");

    pos.QuadPart = 2000;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    const char middleContent[] = "Middle section data";
    if (!WriteFile(h, middleContent, static_cast<DWORD>(sizeof(middleContent) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile middle");
        CloseHandle(h);
        return false;
    }
    printf("Wrote middle content at 2000\n");

    pos.QuadPart = 3000;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile shrink"); CloseHandle(h); return false; }
    printf("Shrunk to final size 3000 bytes\n");

    CloseHandle(h);

    // Step 3: Rename temp to original
    if (!MoveFileExW(tempFile2, originalFile, MOVEFILE_REPLACE_EXISTING)) {
        PrintLastError("MoveFileExW (save)");
        return false;
    }
    printf("Renamed temp to original\n");

    // Verify
    h = OpenTestFile(originalFile, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::vector<char> verifyBuf(3000);
    if (!ReadFile(h, verifyBuf.data(), static_cast<DWORD>(verifyBuf.size()), &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    printf("Read back %lu bytes from saved file\n", static_cast<unsigned long>(ioLen));

    bool ok = true;
    if (memcmp(verifyBuf.data(), newContent, sizeof(newContent) - 1) != 0) {
        printf("ERROR: Start content corrupted!\n");
        printf("Expected: '%s'\n", newContent);
        printf("Got: '%.*s'\n", static_cast<int>(sizeof(newContent) - 1), verifyBuf.data());
        ok = false;
    }
    if (memcmp(verifyBuf.data() + 2000, middleContent, sizeof(middleContent) - 1) != 0) {
        printf("ERROR: Middle content corrupted!\n");
        printf("Expected: '%s'\n", middleContent);
        printf("Got: '%.*s'\n", static_cast<int>(sizeof(middleContent) - 1), verifyBuf.data() + 2000);
        ok = false;
    }

    if (ok) printf("Word-like save pattern verified OK\n");

    // Cleanup
    DeleteFileW(originalFile);
    DeleteFileW(tempFile1);
    DeleteFileW(tempFile2);

    return ok;
}

//=============================================================================
// Test: Interleaved read-write with SetEndOfFile
//=============================================================================
bool Test_InterleavedReadWriteWithResize(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing interleaved read-write with SetEndOfFile\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER pos;
    DWORD ioLen = 0;
    bool ok = true;

    const char blockA[] = "AAAAAAAAAA";
    if (!WriteFile(h, blockA, static_cast<DWORD>(sizeof(blockA) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile A");
        CloseHandle(h);
        return false;
    }
    printf("Step 1: Wrote block A (10 bytes) at 0\n");

    pos.QuadPart = 1000;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }
    printf("Step 2: Expanded to 1000 bytes\n");

    pos.QuadPart = 500;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    const char blockB[] = "BBBBBBBBBB";
    if (!WriteFile(h, blockB, static_cast<DWORD>(sizeof(blockB) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile B");
        CloseHandle(h);
        return false;
    }
    printf("Step 3: Wrote block B (10 bytes) at 500\n");

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    char readA[16] = {0};
    if (!ReadFile(h, readA, static_cast<DWORD>(sizeof(blockA) - 1), &ioLen, NULL)) {
        PrintLastError("ReadFile A");
        CloseHandle(h);
        return false;
    }
    if (memcmp(readA, blockA, sizeof(blockA) - 1) != 0) {
        printf("Step 4 ERROR: Block A corrupted! Expected '%s', got '%.*s'\n", 
               blockA, static_cast<int>(sizeof(blockA) - 1), readA);
        ok = false;
    } else {
        printf("Step 4: Block A verified OK\n");
    }

    pos.QuadPart = 600;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }
    printf("Step 5: Shrunk to 600 bytes\n");

    pos.QuadPart = 500;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    char readB[16] = {0};
    if (!ReadFile(h, readB, static_cast<DWORD>(sizeof(blockB) - 1), &ioLen, NULL)) {
        PrintLastError("ReadFile B");
        CloseHandle(h);
        return false;
    }
    if (memcmp(readB, blockB, sizeof(blockB) - 1) != 0) {
        printf("Step 6 ERROR: Block B corrupted! Expected '%s', got '%.*s'\n", 
               blockB, static_cast<int>(sizeof(blockB) - 1), readB);
        ok = false;
    } else {
        printf("Step 6: Block B verified OK\n");
    }

    pos.QuadPart = 100;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    const char blockC[] = "CCCCCCCCCC";
    if (!WriteFile(h, blockC, static_cast<DWORD>(sizeof(blockC) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile C");
        CloseHandle(h);
        return false;
    }
    printf("Step 7: Wrote block C (10 bytes) at 100\n");

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    std::vector<char> fullRead(600);
    if (!ReadFile(h, fullRead.data(), static_cast<DWORD>(fullRead.size()), &ioLen, NULL)) {
        PrintLastError("ReadFile full");
        CloseHandle(h);
        return false;
    }
    printf("Step 8: Read full file (%lu bytes)\n", static_cast<unsigned long>(ioLen));

    if (memcmp(fullRead.data(), blockA, sizeof(blockA) - 1) != 0) {
        printf("Final check ERROR: Block A at 0 corrupted\n");
        ok = false;
    }
    if (memcmp(fullRead.data() + 100, blockC, sizeof(blockC) - 1) != 0) {
        printf("Final check ERROR: Block C at 100 corrupted\n");
        ok = false;
    }
    if (memcmp(fullRead.data() + 500, blockB, sizeof(blockB) - 1) != 0) {
        printf("Final check ERROR: Block B at 500 corrupted\n");
        ok = false;
    }

    if (ok) printf("Interleaved read-write with resize verified OK\n");

    CloseHandle(h);
    DeleteFileW(file);
    return ok;
}

//=============================================================================
// Test: Overwrite existing data then truncate
//=============================================================================
bool Test_OverwriteThenTruncate(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    printf("Testing overwrite then truncate (data leak check)\n");
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::vector<char> sensitiveData(2000, 'S');
    DWORD ioLen = 0;
    if (!WriteFile(h, sensitiveData.data(), static_cast<DWORD>(sensitiveData.size()), &ioLen, NULL)) {
        PrintLastError("WriteFile sensitive");
        CloseHandle(h);
        return false;
    }
    printf("Wrote 2000 bytes of 'S' (sensitive data)\n");

    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    
    std::vector<char> newData(500, 'N');
    if (!WriteFile(h, newData.data(), static_cast<DWORD>(newData.size()), &ioLen, NULL)) {
        PrintLastError("WriteFile new");
        CloseHandle(h);
        return false;
    }
    printf("Overwrote first 500 bytes with 'N'\n");

    pos.QuadPart = 1000;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }
    printf("Truncated to 1000 bytes\n");

    pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    
    std::vector<char> verifyBuf(1000);
    if (!ReadFile(h, verifyBuf.data(), static_cast<DWORD>(verifyBuf.size()), &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(h);
        return false;
    }
    printf("Read back %lu bytes\n", static_cast<unsigned long>(ioLen));

    bool ok = true;
    for (size_t i = 0; i < 500; i++) {
        if (verifyBuf[i] != 'N') {
            printf("ERROR at %zu: expected 'N', got '%c'\n", i, verifyBuf[i]);
            ok = false;
            break;
        }
    }
    for (size_t i = 500; i < 1000; i++) {
        if (verifyBuf[i] != 'S') {
            printf("ERROR at %zu: expected 'S', got '%c'\n", i, verifyBuf[i]);
            ok = false;
            break;
        }
    }

    if (ok) printf("Overwrite then truncate verified OK\n");

    CloseHandle(h);
    DeleteFileW(file);
    return ok;
}
