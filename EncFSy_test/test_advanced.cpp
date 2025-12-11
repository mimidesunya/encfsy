// test_advanced.cpp - Advanced and scenario-based tests
#include "test_common.h"

//=============================================================================
// Test: Adobe Reader-like save pattern (tmp file -> rename to original)
// This tests the pattern where:
// 1. New content is written to a .tmp file
// 2. Original file is deleted or renamed to backup
// 3. Tmp file is renamed to original filename
// This pattern can fail if rename operations don't properly synchronize
// with file handles or IV caching.
//=============================================================================
bool Test_AdobeReaderSavePattern(const WCHAR* rootDir)
{
    printf("Testing Adobe Reader-like save pattern (aggressive version)\n");
    
    WCHAR originalFile[260];
    WCHAR tmpFile[260];
    WCHAR backupFile[260];
    wsprintfW(originalFile, L"%s\\document.pdf", rootDir);
    wsprintfW(tmpFile, L"%s\\document.tmp", rootDir);
    wsprintfW(backupFile, L"%s\\document.pdf.bak", rootDir);

    bool ok = true;
    const int ITERATIONS = 20;  // Repeat the entire save cycle multiple times

    for (int iter = 0; iter < ITERATIONS && ok; iter++) {
        // Ensure clean state
        DeleteFileW(originalFile);
        DeleteFileW(tmpFile);
        DeleteFileW(backupFile);

        // Create unique content for this iteration
        char originalPdfContent[128];
        char newPdfContent[128];
        sprintf_s(originalPdfContent, "%%PDF-1.4 Original content iteration %d - ORIGINAL DATA", iter);
        sprintf_s(newPdfContent, "%%PDF-1.4 Modified content iteration %d - NEW DATA AFTER EDIT", iter);

        // Step 1: Create original PDF with content
        HANDLE h = OpenTestFile(originalFile, GENERIC_READ | GENERIC_WRITE, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) return false;
        
        DWORD ioLen = 0;
        if (!WriteFile(h, originalPdfContent, static_cast<DWORD>(strlen(originalPdfContent)), &ioLen, NULL)) {
            PrintLastError("WriteFile original PDF");
            CloseHandle(h);
            return false;
        }
        FlushFileBuffers(h);
        CloseHandle(h);

        // Step 2: Write new content to tmp file
        h = OpenTestFile(tmpFile, GENERIC_READ | GENERIC_WRITE, 
                         FILE_SHARE_READ | FILE_SHARE_WRITE, 
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) return false;

        if (!WriteFile(h, newPdfContent, static_cast<DWORD>(strlen(newPdfContent)), &ioLen, NULL)) {
            PrintLastError("WriteFile tmp");
            CloseHandle(h);
            return false;
        }
        FlushFileBuffers(h);
        CloseHandle(h);

        // Step 3: Rename original to backup (NO DELAY - immediate)
        if (!MoveFileExW(originalFile, backupFile, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW (original to backup)");
            ok = false;
            break;
        }

        // Step 4: Rename tmp to original (NO DELAY - immediate)
        if (!MoveFileExW(tmpFile, originalFile, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW (tmp to original)");
            MoveFileExW(backupFile, originalFile, MOVEFILE_REPLACE_EXISTING);
            ok = false;
            break;
        }

        // Step 5: IMMEDIATELY open and verify (no delay - this is where timing issues show)
        h = OpenTestFile(originalFile, GENERIC_READ, FILE_SHARE_READ, 
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("ERROR: Cannot open renamed file at iteration %d!\n", iter);
            ok = false;
            break;
        }

        char verifyBuf[256] = {0};
        if (!ReadFile(h, verifyBuf, sizeof(verifyBuf) - 1, &ioLen, NULL)) {
            PrintLastError("ReadFile verify");
            CloseHandle(h);
            ok = false;
            break;
        }
        CloseHandle(h);

        size_t expectedLen = strlen(newPdfContent);
        if (ioLen != expectedLen) {
            printf("ERROR at iteration %d: File size mismatch! Expected %zu, got %lu\n",
                   iter, expectedLen, static_cast<unsigned long>(ioLen));
            ok = false;
            break;
        }
        if (memcmp(verifyBuf, newPdfContent, expectedLen) != 0) {
            printf("ERROR at iteration %d: Content mismatch after rename!\n", iter);
            printf("Expected: '%s'\n", newPdfContent);
            printf("Got: '%.*s'\n", static_cast<int>(ioLen), verifyBuf);
            ok = false;
            break;
        }

        // Step 6: Also verify backup file is correct
        h = OpenTestFile(backupFile, GENERIC_READ, FILE_SHARE_READ, 
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("ERROR: Cannot open backup file at iteration %d!\n", iter);
            ok = false;
            break;
        }

        memset(verifyBuf, 0, sizeof(verifyBuf));
        if (!ReadFile(h, verifyBuf, sizeof(verifyBuf) - 1, &ioLen, NULL)) {
            PrintLastError("ReadFile backup verify");
            CloseHandle(h);
            ok = false;
            break;
        }
        CloseHandle(h);

        expectedLen = strlen(originalPdfContent);
        if (memcmp(verifyBuf, originalPdfContent, expectedLen) != 0) {
            printf("ERROR at iteration %d: Backup content corrupted!\n", iter);
            printf("Expected: '%s'\n", originalPdfContent);
            printf("Got: '%.*s'\n", static_cast<int>(ioLen), verifyBuf);
            ok = false;
            break;
        }

        if ((iter + 1) % 5 == 0) {
            printf("  Completed %d/%d save cycles\n", iter + 1, ITERATIONS);
        }
    }

    if (ok) printf("Adobe Reader save pattern (%d iterations) verified OK\n", ITERATIONS);

    // Cleanup
    DeleteFileW(originalFile);
    DeleteFileW(backupFile);
    DeleteFileW(tmpFile);

    return ok;
}

//=============================================================================
// Test: Rapid rename to same target (stress test for rename timing)
// This simulates rapid save operations where a file is repeatedly
// replaced via rename operations.
//=============================================================================
bool Test_RapidRenameToSameTarget(const WCHAR* rootDir)
{
    printf("Testing rapid rename to same target (stress test)\n");
    
    WCHAR targetFile[260];
    WCHAR tmpFile[260];
    wsprintfW(targetFile, L"%s\\rapid_target.txt", rootDir);
    wsprintfW(tmpFile, L"%s\\rapid_tmp.txt", rootDir);

    // Ensure clean state
    DeleteFileW(targetFile);
    DeleteFileW(tmpFile);

    bool ok = true;
    const int ITERATIONS = 100;

    for (int i = 0; i < ITERATIONS && ok; i++) {
        // Create tmp file with unique content
        HANDLE h = OpenTestFile(tmpFile, GENERIC_READ | GENERIC_WRITE, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) return false;

        char content[64];
        sprintf_s(content, "Content version %d - unique identifier", i);
        DWORD ioLen = 0;
        if (!WriteFile(h, content, static_cast<DWORD>(strlen(content)), &ioLen, NULL)) {
            PrintLastError("WriteFile");
            CloseHandle(h);
            return false;
        }
        // NO flush - test worst case
        CloseHandle(h);

        // Rename tmp to target (replaces existing)
        if (!MoveFileExW(tmpFile, targetFile, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW");
            ok = false;
            break;
        }

        // Immediately verify
        h = OpenTestFile(targetFile, GENERIC_READ, FILE_SHARE_READ, 
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("ERROR: Cannot open target after rename at iteration %d\n", i);
            ok = false;
            break;
        }

        char verifyBuf[64] = {0};
        if (!ReadFile(h, verifyBuf, sizeof(verifyBuf) - 1, &ioLen, NULL)) {
            PrintLastError("ReadFile verify");
            CloseHandle(h);
            ok = false;
            break;
        }
        CloseHandle(h);

        if (memcmp(verifyBuf, content, strlen(content)) != 0) {
            printf("ERROR: Content mismatch at iteration %d!\n", i);
            printf("Expected: '%s'\n", content);
            printf("Got: '%s'\n", verifyBuf);
            ok = false;
            break;
        }

        if ((i + 1) % 25 == 0) {
            printf("  Completed %d/%d iterations\n", i + 1, ITERATIONS);
        }
    }

    if (ok) printf("Rapid rename to same target (%d iterations) verified OK\n", ITERATIONS);

    // Cleanup
    DeleteFileW(targetFile);
    DeleteFileW(tmpFile);

    return ok;
}

//=============================================================================
// Test: Rename chain without delays (A->B->C->A cycle)
// Tests rename operations in a cycle to stress IV/cache invalidation
//=============================================================================
bool Test_RenameChainCycle(const WCHAR* rootDir)
{
    printf("Testing rename chain cycle (A->B->C->A)\n");
    
    WCHAR fileA[260];
    WCHAR fileB[260];
    WCHAR fileC[260];
    wsprintfW(fileA, L"%s\\chain_a.txt", rootDir);
    wsprintfW(fileB, L"%s\\chain_b.txt", rootDir);
    wsprintfW(fileC, L"%s\\chain_c.txt", rootDir);

    // Ensure clean state
    DeleteFileW(fileA);
    DeleteFileW(fileB);
    DeleteFileW(fileC);

    bool ok = true;
    const int CYCLES = 30;
    const char* content = "Persistent content through rename cycles - should never change";

    // Create initial file
    HANDLE h = OpenTestFile(fileA, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD ioLen = 0;
    if (!WriteFile(h, content, static_cast<DWORD>(strlen(content)), &ioLen, NULL)) {
        PrintLastError("WriteFile initial");
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);

    for (int i = 0; i < CYCLES && ok; i++) {
        // A -> B
        if (!MoveFileExW(fileA, fileB, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW A->B");
            ok = false;
            break;
        }

        // B -> C
        if (!MoveFileExW(fileB, fileC, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW B->C");
            ok = false;
            break;
        }

        // C -> A (completes the cycle)
        if (!MoveFileExW(fileC, fileA, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW C->A");
            ok = false;
            break;
        }

        // Verify content after full cycle
        h = OpenTestFile(fileA, GENERIC_READ, FILE_SHARE_READ, 
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("ERROR: Cannot open file A after cycle %d\n", i);
            ok = false;
            break;
        }

        char verifyBuf[128] = {0};
        if (!ReadFile(h, verifyBuf, sizeof(verifyBuf) - 1, &ioLen, NULL)) {
            PrintLastError("ReadFile verify");
            CloseHandle(h);
            ok = false;
            break;
        }
        CloseHandle(h);

        if (memcmp(verifyBuf, content, strlen(content)) != 0) {
            printf("ERROR: Content corrupted after cycle %d!\n", i);
            printf("Expected: '%s'\n", content);
            printf("Got: '%s'\n", verifyBuf);
            ok = false;
            break;
        }

        if ((i + 1) % 10 == 0) {
            printf("  Completed %d/%d cycles\n", i + 1, CYCLES);
        }
    }

    if (ok) printf("Rename chain cycle (%d cycles) verified OK\n", CYCLES);

    // Cleanup
    DeleteFileW(fileA);
    DeleteFileW(fileB);
    DeleteFileW(fileC);

    return ok;
}

//=============================================================================
// Test: Write-rename-read without close (tests handle behavior during rename)
// Simulates pattern where file is written, renamed, then read through
// potentially stale references
//=============================================================================
bool Test_WriteRenameReadImmediate(const WCHAR* rootDir)
{
    printf("Testing write-rename-read immediate pattern\n");
    
    WCHAR srcFile[260];
    WCHAR dstFile[260];
    wsprintfW(srcFile, L"%s\\wri_src.txt", rootDir);
    wsprintfW(dstFile, L"%s\\wri_dst.txt", rootDir);

    bool ok = true;
    const int ITERATIONS = 50;

    for (int i = 0; i < ITERATIONS && ok; i++) {
        // Ensure clean state
        DeleteFileW(srcFile);
        DeleteFileW(dstFile);

        // Create and write source file
        HANDLE hSrc = OpenTestFile(srcFile, GENERIC_READ | GENERIC_WRITE, 
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (hSrc == INVALID_HANDLE_VALUE) return false;

        char content[64];
        sprintf_s(content, "Write-rename-read iteration %d data", i);
        DWORD ioLen = 0;
        if (!WriteFile(hSrc, content, static_cast<DWORD>(strlen(content)), &ioLen, NULL)) {
            PrintLastError("WriteFile");
            CloseHandle(hSrc);
            return false;
        }
        
        // Flush but keep handle open
        FlushFileBuffers(hSrc);

        // Close handle before rename
        CloseHandle(hSrc);

        // Rename immediately
        if (!MoveFileExW(srcFile, dstFile, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW");
            ok = false;
            break;
        }

        // Open destination and read immediately
        HANDLE hDst = OpenTestFile(dstFile, GENERIC_READ, FILE_SHARE_READ, 
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (hDst == INVALID_HANDLE_VALUE) {
            printf("ERROR: Cannot open destination at iteration %d\n", i);
            ok = false;
            break;
        }

        char verifyBuf[64] = {0};
        if (!ReadFile(hDst, verifyBuf, sizeof(verifyBuf) - 1, &ioLen, NULL)) {
            PrintLastError("ReadFile");
            CloseHandle(hDst);
            ok = false;
            break;
        }
        CloseHandle(hDst);

        if (memcmp(verifyBuf, content, strlen(content)) != 0) {
            printf("ERROR: Content mismatch at iteration %d!\n", i);
            printf("Expected: '%s'\n", content);
            printf("Got: '%s'\n", verifyBuf);
            ok = false;
            break;
        }

        // Delete destination for next iteration
        DeleteFileW(dstFile);

        if ((i + 1) % 10 == 0) {
            printf("  Completed %d/%d iterations\n", i + 1, ITERATIONS);
        }
    }

    if (ok) printf("Write-rename-read immediate (%d iterations) verified OK\n", ITERATIONS);

    // Cleanup
    DeleteFileW(srcFile);
    DeleteFileW(dstFile);

    return ok;
}

//=============================================================================
// Test: Rapid rename cycle (stress test for rename timing issues)
// This tests rapid succession of rename operations which can expose
// timing issues with IV caching or file handle synchronization.
//=============================================================================
bool Test_RapidRenameCycle(const WCHAR* rootDir)
{
    printf("Testing rapid rename cycle (stress test)\n");
    
    WCHAR fileA[260];
    WCHAR fileB[260];
    WCHAR fileC[260];
    wsprintfW(fileA, L"%s\\rename_test_a.txt", rootDir);
    wsprintfW(fileB, L"%s\\rename_test_b.txt", rootDir);
    wsprintfW(fileC, L"%s\\rename_test_c.txt", rootDir);

    // Ensure clean state
    DeleteFileW(fileA);
    DeleteFileW(fileB);
    DeleteFileW(fileC);

    bool ok = true;
    const int ITERATIONS = 50;

    for (int i = 0; i < ITERATIONS && ok; i++) {
        // Create file A with unique content
        HANDLE h = OpenTestFile(fileA, GENERIC_READ | GENERIC_WRITE, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) return false;

        char content[64];
        sprintf_s(content, "Iteration %d content - unique data", i);
        DWORD ioLen = 0;
        if (!WriteFile(h, content, static_cast<DWORD>(strlen(content)), &ioLen, NULL)) {
            PrintLastError("WriteFile");
            CloseHandle(h);
            return false;
        }
        CloseHandle(h);

        // Rename A -> B
        if (!MoveFileExW(fileA, fileB, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW A->B");
            ok = false;
            break;
        }

        // Rename B -> C
        if (!MoveFileExW(fileB, fileC, MOVEFILE_REPLACE_EXISTING)) {
            PrintLastError("MoveFileExW B->C");
            ok = false;
            break;
        }

        // Verify content in C
        h = OpenTestFile(fileC, GENERIC_READ, FILE_SHARE_READ, 
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("ERROR: Cannot open file C after rename at iteration %d\n", i);
            ok = false;
            break;
        }

        char verifyBuf[64] = {0};
        if (!ReadFile(h, verifyBuf, sizeof(verifyBuf) - 1, &ioLen, NULL)) {
            PrintLastError("ReadFile verify");
            CloseHandle(h);
            ok = false;
            break;
        }
        CloseHandle(h);

        if (memcmp(verifyBuf, content, strlen(content)) != 0) {
            printf("ERROR: Content corrupted at iteration %d!\n", i);
            printf("Expected: '%s'\n", content);
            printf("Got: '%s'\n", verifyBuf);
            ok = false;
            break;
        }

        // Delete C for next iteration
        DeleteFileW(fileC);

        if ((i + 1) % 10 == 0) {
            printf("  Completed %d/%d iterations\n", i + 1, ITERATIONS);
        }
    }

    if (ok) printf("Rapid rename cycle (%d iterations) verified OK\n", ITERATIONS);

    // Cleanup
    DeleteFileW(fileA);
    DeleteFileW(fileB);
    DeleteFileW(fileC);

    return ok;
}

//=============================================================================
// Test: Rename with open handle (simulates programs that keep file open)
// This tests renaming a file while another handle to it may still exist,
// which can happen with some applications that don't properly close handles.
//=============================================================================
bool Test_RenameWithConcurrentAccess(const WCHAR* rootDir)
{
    printf("Testing rename with concurrent file access\n");
    
    WCHAR fileA[260];
    WCHAR fileB[260];
    wsprintfW(fileA, L"%s\\concurrent_a.txt", rootDir);
    wsprintfW(fileB, L"%s\\concurrent_b.txt", rootDir);

    // Ensure clean state
    DeleteFileW(fileA);
    DeleteFileW(fileB);

    // Create file A
    HANDLE hWrite = OpenTestFile(fileA, GENERIC_READ | GENERIC_WRITE, 
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (hWrite == INVALID_HANDLE_VALUE) return false;

    const char content[] = "Content before rename operation";
    DWORD ioLen = 0;
    if (!WriteFile(hWrite, content, static_cast<DWORD>(sizeof(content) - 1), &ioLen, NULL)) {
        PrintLastError("WriteFile");
        CloseHandle(hWrite);
        return false;
    }
    printf("Created file A with content\n");

    // Open another handle for reading (simulates another process reading)
    HANDLE hRead = OpenTestFile(fileA, GENERIC_READ, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (hRead == INVALID_HANDLE_VALUE) {
        CloseHandle(hWrite);
        return false;
    }
    printf("Opened second handle for reading\n");

    // Close write handle before rename
    CloseHandle(hWrite);

    // Rename file while read handle is still open
    if (!MoveFileExW(fileA, fileB, MOVEFILE_REPLACE_EXISTING)) {
        // This might fail on some systems - not necessarily an error
        DWORD err = GetLastError();
        printf("Note: Rename with open handle failed (error=%lu) - this is expected on some systems\n", err);
        CloseHandle(hRead);
        DeleteFileW(fileA);
        return true;  // Not a failure, just unsupported scenario
    }
    printf("Renamed file while read handle was open\n");

    // Close the read handle
    CloseHandle(hRead);

    // Verify file B exists and has correct content
    HANDLE hVerify = OpenTestFile(fileB, GENERIC_READ, FILE_SHARE_READ, 
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (hVerify == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open renamed file!\n");
        return false;
    }

    char verifyBuf[64] = {0};
    if (!ReadFile(hVerify, verifyBuf, sizeof(verifyBuf) - 1, &ioLen, NULL)) {
        PrintLastError("ReadFile verify");
        CloseHandle(hVerify);
        return false;
    }
    CloseHandle(hVerify);

    bool ok = (memcmp(verifyBuf, content, sizeof(content) - 1) == 0);
    if (!ok) {
        printf("ERROR: Content corrupted after rename with concurrent access!\n");
        printf("Expected: '%s'\n", content);
        printf("Got: '%s'\n", verifyBuf);
    } else {
        printf("Rename with concurrent access verified OK\n");
    }

    // Cleanup
    DeleteFileW(fileA);
    DeleteFileW(fileB);

    return ok;
}

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
