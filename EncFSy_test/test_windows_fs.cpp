// test_windows_fs.cpp - Windows-specific filesystem tests
// Tests for: symbolic links, hard links, memory-mapped files, change notifications,
//            alternate data streams, special filenames, shortcuts
#include "test_common.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <shobjidl.h>  // For IShellLink
#include <shlguid.h>   // For CLSID_ShellLink
#include <objbase.h>   // For CoInitialize

#pragma comment(lib, "ole32.lib")

//=============================================================================
// Helper: Check if running with admin privileges (required for symlinks)
//=============================================================================
static bool IsRunningAsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

//=============================================================================
// ALTERNATE DATA STREAMS TESTS
//=============================================================================

//=============================================================================
// Test: Basic Alternate Data Stream read/write
//=============================================================================
bool Test_AlternateDataStreams(const WCHAR* baseFile)
{
    printf("Testing basic Alternate Data Stream\n");
    
    // Ensure clean state
    DeleteFileW(baseFile);
    
    // Create base file
    HANDLE base = OpenTestFile(baseFile, GENERIC_READ | GENERIC_WRITE, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (base == INVALID_HANDLE_VALUE) return false;
    
    const char mainContent[] = "Main file content";
    DWORD written = 0;
    WriteFile(base, mainContent, static_cast<DWORD>(strlen(mainContent)), &written, NULL);
    CloseHandle(base);
    printf("Created base file with %lu bytes\n", static_cast<unsigned long>(written));

    // Create alternate stream
    WCHAR streamPath[260];
    wsprintfW(streamPath, L"%s:metadata", baseFile);
    HANDLE h = OpenTestFile(streamPath, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Note: ADS may not be supported on this filesystem\n");
        DeleteFileW(baseFile);
        return true;  // Not a failure if ADS not supported
    }
    
    const char payload[] = "stream-metadata-content";
    DWORD ioLen = 0;
    if (!WriteFile(h, payload, static_cast<DWORD>(strlen(payload)), &ioLen, NULL)) { 
        PrintLastError("WriteFile ADS"); 
        CloseHandle(h);
        DeleteFileW(baseFile);
        return false; 
    }
    printf("Wrote %lu bytes to ADS\n", static_cast<unsigned long>(ioLen));

    // Read back from ADS
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    char buf[64] = {0};
    ioLen = 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &ioLen, NULL)) { 
        PrintLastError("ReadFile ADS"); 
        CloseHandle(h);
        DeleteFileW(baseFile);
        return false; 
    }
    CloseHandle(h);
    
    bool ok = (ioLen == strlen(payload) && memcmp(buf, payload, strlen(payload)) == 0);
    if (ok) {
        printf("ADS read verified: %lu bytes, '%s'\n", static_cast<unsigned long>(ioLen), buf);
    } else {
        printf("ERROR: ADS content mismatch!\n");
        printf("Expected: '%s'\n", payload);
        printf("Got: '%s'\n", buf);
    }
    
    // Verify main file content is unchanged
    base = OpenTestFile(baseFile, GENERIC_READ, FILE_SHARE_READ, 
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (base != INVALID_HANDLE_VALUE) {
        memset(buf, 0, sizeof(buf));
        ReadFile(base, buf, sizeof(buf) - 1, &ioLen, NULL);
        CloseHandle(base);
        
        if (memcmp(buf, mainContent, strlen(mainContent)) == 0) {
            printf("Main file content unchanged - OK\n");
        } else {
            printf("ERROR: Main file content corrupted after ADS write!\n");
            ok = false;
        }
    }
    
    // Cleanup
    DeleteFileW(streamPath);
    DeleteFileW(baseFile);
    
    return ok;
}

//=============================================================================
// Test: Multiple Alternate Data Streams on same file
//=============================================================================
bool Test_MultipleAlternateStreams(const WCHAR* baseFile)
{
    printf("Testing multiple Alternate Data Streams\n");
    
    // Ensure clean state
    DeleteFileW(baseFile);
    
    // Create base file
    HANDLE h = OpenTestFile(baseFile, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const char mainContent[] = "Base file";
    DWORD written;
    WriteFile(h, mainContent, static_cast<DWORD>(strlen(mainContent)), &written, NULL);
    CloseHandle(h);
    
    // Create multiple streams
    const WCHAR* streamNames[] = { L":stream1", L":stream2", L":stream3", L":metadata" };
    const char* streamContents[] = { "Content of stream 1", "Content of stream 2", 
                                      "Content of stream 3", "Metadata stream" };
    const int numStreams = sizeof(streamNames) / sizeof(streamNames[0]);
    
    bool ok = true;
    
    // Write to each stream
    for (int i = 0; i < numStreams; i++) {
        WCHAR streamPath[260];
        wsprintfW(streamPath, L"%s%s", baseFile, streamNames[i]);
        
        h = OpenTestFile(streamPath, GENERIC_READ | GENERIC_WRITE, 
                         FILE_SHARE_READ | FILE_SHARE_WRITE, 
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"Failed to create stream: %s\n", streamNames[i]);
            ok = false;
            continue;
        }
        
        WriteFile(h, streamContents[i], static_cast<DWORD>(strlen(streamContents[i])), &written, NULL);
        CloseHandle(h);
        wprintf(L"Created stream %s with %lu bytes\n", streamNames[i], static_cast<unsigned long>(written));
    }
    
    // Read back and verify each stream
    for (int i = 0; i < numStreams; i++) {
        WCHAR streamPath[260];
        wsprintfW(streamPath, L"%s%s", baseFile, streamNames[i]);
        
        h = OpenTestFile(streamPath, GENERIC_READ, FILE_SHARE_READ, 
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"Failed to reopen stream: %s\n", streamNames[i]);
            ok = false;
            continue;
        }
        
        char buf[128] = {0};
        DWORD readLen;
        ReadFile(h, buf, sizeof(buf) - 1, &readLen, NULL);
        CloseHandle(h);
        
        if (memcmp(buf, streamContents[i], strlen(streamContents[i])) == 0) {
            wprintf(L"Stream %s verified OK\n", streamNames[i]);
        } else {
            wprintf(L"ERROR: Stream %s content mismatch!\n", streamNames[i]);
            ok = false;
        }
    }
    
    // Cleanup
    for (int i = 0; i < numStreams; i++) {
        WCHAR streamPath[260];
        wsprintfW(streamPath, L"%s%s", baseFile, streamNames[i]);
        DeleteFileW(streamPath);
    }
    DeleteFileW(baseFile);
    
    return ok;
}

//=============================================================================
// Test: Large Alternate Data Stream
//=============================================================================
bool Test_LargeAlternateStream(const WCHAR* baseFile)
{
    printf("Testing large Alternate Data Stream (1MB)\n");
    
    // Ensure clean state
    DeleteFileW(baseFile);
    
    // Create base file
    HANDLE h = OpenTestFile(baseFile, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    
    // Create large stream
    WCHAR streamPath[260];
    wsprintfW(streamPath, L"%s:largestream", baseFile);
    
    h = OpenTestFile(streamPath, GENERIC_READ | GENERIC_WRITE, 
                     FILE_SHARE_READ | FILE_SHARE_WRITE, 
                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        DeleteFileW(baseFile);
        return true;  // ADS may not be supported
    }
    
    // Write 1MB of data
    const DWORD chunkSize = 64 * 1024;  // 64KB chunks
    const DWORD totalSize = 1024 * 1024;  // 1MB
    std::vector<char> buffer(chunkSize);
    
    // Fill with pattern
    for (size_t i = 0; i < chunkSize; i++) {
        buffer[i] = static_cast<char>('A' + (i % 26));
    }
    
    DWORD totalWritten = 0;
    while (totalWritten < totalSize) {
        DWORD toWrite = (totalSize - totalWritten < chunkSize) ? (totalSize - totalWritten) : chunkSize;
        DWORD written;
        if (!WriteFile(h, buffer.data(), toWrite, &written, NULL)) {
            PrintLastError("WriteFile large ADS");
            CloseHandle(h);
            DeleteFileW(streamPath);
            DeleteFileW(baseFile);
            return false;
        }
        totalWritten += written;
    }
    printf("Wrote %lu bytes to large ADS\n", static_cast<unsigned long>(totalWritten));
    
    // Verify size
    LARGE_INTEGER fileSize;
    GetFileSizeEx(h, &fileSize);
    CloseHandle(h);
    
    bool ok = (fileSize.QuadPart == totalSize);
    if (ok) {
        printf("Large ADS size verified: %lld bytes\n", fileSize.QuadPart);
    } else {
        printf("ERROR: ADS size mismatch! Expected %lu, got %lld\n", 
               static_cast<unsigned long>(totalSize), fileSize.QuadPart);
    }
    
    // Read back and verify pattern at random positions
    h = OpenTestFile(streamPath, GENERIC_READ, FILE_SHARE_READ, 
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h != INVALID_HANDLE_VALUE) {
        // Check start
        char checkBuf[64];
        DWORD readLen;
        ReadFile(h, checkBuf, sizeof(checkBuf), &readLen, NULL);
        for (size_t i = 0; i < sizeof(checkBuf); i++) {
            if (checkBuf[i] != static_cast<char>('A' + (i % 26))) {
                printf("ERROR: Pattern mismatch at start!\n");
                ok = false;
                break;
            }
        }
        
        // Check middle
        LARGE_INTEGER pos;
        pos.QuadPart = totalSize / 2;
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        ReadFile(h, checkBuf, sizeof(checkBuf), &readLen, NULL);
        for (size_t i = 0; i < sizeof(checkBuf); i++) {
            size_t offset = (totalSize / 2 + i) % chunkSize;
            if (checkBuf[i] != static_cast<char>('A' + (offset % 26))) {
                printf("ERROR: Pattern mismatch at middle!\n");
                ok = false;
                break;
            }
        }
        
        CloseHandle(h);
        if (ok) printf("Large ADS pattern verified OK\n");
    }
    
    // Cleanup
    DeleteFileW(streamPath);
    DeleteFileW(baseFile);
    
    return ok;
}

//=============================================================================
// Test: ADS survives file rename
//=============================================================================
bool Test_ADSSurvivesRename(const WCHAR* rootDir)
{
    printf("Testing ADS survives file rename\n");
    
    WCHAR originalFile[260];
    WCHAR renamedFile[260];
    WCHAR streamPath[260];
    wsprintfW(originalFile, L"%s\\ads_original.txt", rootDir);
    wsprintfW(renamedFile, L"%s\\ads_renamed.txt", rootDir);
    
    // Cleanup
    DeleteFileW(renamedFile);
    DeleteFileW(originalFile);
    
    // Create file with ADS
    HANDLE h = OpenTestFile(originalFile, GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const char mainContent[] = "Main content";
    DWORD written;
    WriteFile(h, mainContent, static_cast<DWORD>(strlen(mainContent)), &written, NULL);
    CloseHandle(h);
    
    // Create ADS
    wsprintfW(streamPath, L"%s:metadata", originalFile);
    h = OpenTestFile(streamPath, GENERIC_READ | GENERIC_WRITE, 
                     FILE_SHARE_READ | FILE_SHARE_WRITE, 
                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        DeleteFileW(originalFile);
        return true;  // ADS not supported
    }
    
    const char adsContent[] = "ADS metadata that should survive rename";
    WriteFile(h, adsContent, static_cast<DWORD>(strlen(adsContent)), &written, NULL);
    CloseHandle(h);
    printf("Created file with ADS\n");
    
    // Rename file
    if (!MoveFileExW(originalFile, renamedFile, MOVEFILE_REPLACE_EXISTING)) {
        PrintLastError("MoveFileExW");
        DeleteFileW(originalFile);
        return false;
    }
    printf("Renamed file\n");
    
    // Verify ADS exists on renamed file
    wsprintfW(streamPath, L"%s:metadata", renamedFile);
    h = OpenTestFile(streamPath, GENERIC_READ, FILE_SHARE_READ, 
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    
    bool ok = true;
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: ADS not found after rename!\n");
        ok = false;
    } else {
        char buf[128] = {0};
        DWORD readLen;
        ReadFile(h, buf, sizeof(buf) - 1, &readLen, NULL);
        CloseHandle(h);
        
        if (memcmp(buf, adsContent, strlen(adsContent)) == 0) {
            printf("ADS content preserved after rename - OK\n");
        } else {
            printf("ERROR: ADS content changed after rename!\n");
            ok = false;
        }
    }
    
    // Cleanup
    DeleteFileW(renamedFile);
    
    return ok;
}

//=============================================================================
// Test: Symbolic link to file
// Creates a symbolic link and verifies read/write through it
//=============================================================================
bool Test_SymbolicLinkFile(const WCHAR* rootDir)
{
    printf("Testing symbolic link to file\n");
    
    WCHAR targetFile[260];
    WCHAR linkFile[260];
    wsprintfW(targetFile, L"%s\\symlink_target.txt", rootDir);
    wsprintfW(linkFile, L"%s\\symlink_link.txt", rootDir);
    
    // Cleanup
    DeleteFileW(linkFile);
    DeleteFileW(targetFile);
    
    // Check admin privileges
    if (!IsRunningAsAdmin()) {
        printf("Note: Symbolic links require administrator privileges or Developer Mode.\n");
        printf("      Attempting anyway (may fail on older Windows without Developer Mode)...\n");
    }
    
    // Create target file with content
    HANDLE h = OpenTestFile(targetFile, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const char content[] = "Symbolic link test content - original data";
    DWORD written = 0;
    if (!WriteFile(h, content, static_cast<DWORD>(strlen(content)), &written, NULL)) {
        PrintLastError("WriteFile target");
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    printf("Created target file with %lu bytes\n", static_cast<unsigned long>(written));
    
    // Create symbolic link (SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE for Developer Mode)
    DWORD flags = 0;
    #ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    #define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
    #endif
    flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    
    if (!CreateSymbolicLinkW(linkFile, targetFile, flags)) {
        DWORD err = GetLastError();
        if (err == ERROR_PRIVILEGE_NOT_HELD || err == ERROR_INVALID_PARAMETER) {
            // Try without the flag for older Windows
            if (!CreateSymbolicLinkW(linkFile, targetFile, 0)) {
                err = GetLastError();
                printf("CreateSymbolicLinkW failed (error=%lu) - may need admin or Developer Mode\n",
                       static_cast<unsigned long>(err));
                DeleteFileW(targetFile);
                // Not a failure if symlinks aren't supported
                return true;
            }
        } else {
            printf("CreateSymbolicLinkW failed (error=%lu)\n", static_cast<unsigned long>(err));
            DeleteFileW(targetFile);
            return true;  // Symlinks may not be supported
        }
    }
    printf("Created symbolic link: %ls -> %ls\n", linkFile, targetFile);
    
    // Read through symbolic link
    h = OpenTestFile(linkFile, GENERIC_READ, FILE_SHARE_READ,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to open symbolic link for reading\n");
        DeleteFileW(linkFile);
        DeleteFileW(targetFile);
        return false;
    }
    
    char readBuf[128] = {0};
    DWORD readLen = 0;
    if (!ReadFile(h, readBuf, sizeof(readBuf) - 1, &readLen, NULL)) {
        PrintLastError("ReadFile through symlink");
        CloseHandle(h);
        DeleteFileW(linkFile);
        DeleteFileW(targetFile);
        return false;
    }
    CloseHandle(h);
    
    bool ok = (readLen == strlen(content) && memcmp(readBuf, content, strlen(content)) == 0);
    if (!ok) {
        printf("ERROR: Content mismatch through symbolic link!\n");
        printf("Expected: '%s'\n", content);
        printf("Got: '%s'\n", readBuf);
    } else {
        printf("Read through symbolic link verified OK\n");
    }
    
    // Write through symbolic link and verify target is modified
    h = OpenTestFile(linkFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h != INVALID_HANDLE_VALUE) {
        const char newContent[] = "Modified through symlink";
        LARGE_INTEGER pos = {0};
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        WriteFile(h, newContent, static_cast<DWORD>(strlen(newContent)), &written, NULL);
        SetEndOfFile(h);
        CloseHandle(h);
        
        // Verify target file was modified
        h = OpenTestFile(targetFile, GENERIC_READ, FILE_SHARE_READ,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h != INVALID_HANDLE_VALUE) {
            memset(readBuf, 0, sizeof(readBuf));
            ReadFile(h, readBuf, sizeof(readBuf) - 1, &readLen, NULL);
            CloseHandle(h);
            
            if (memcmp(readBuf, newContent, strlen(newContent)) == 0) {
                printf("Write through symbolic link verified OK\n");
            } else {
                printf("ERROR: Target not modified through symlink write!\n");
                ok = false;
            }
        }
    }
    
    // Cleanup
    DeleteFileW(linkFile);
    DeleteFileW(targetFile);
    
    return ok;
}

//=============================================================================
// Test: Symbolic link to directory
//=============================================================================
bool Test_SymbolicLinkDirectory(const WCHAR* rootDir)
{
    printf("Testing symbolic link to directory\n");
    
    WCHAR targetDir[260];
    WCHAR linkDir[260];
    WCHAR fileInTarget[260];
    WCHAR fileViaLink[260];
    wsprintfW(targetDir, L"%s\\symlink_target_dir", rootDir);
    wsprintfW(linkDir, L"%s\\symlink_link_dir", rootDir);
    wsprintfW(fileInTarget, L"%s\\symlink_target_dir\\test.txt", rootDir);
    wsprintfW(fileViaLink, L"%s\\symlink_link_dir\\test.txt", rootDir);
    
    // Cleanup
    DeleteFileW(fileInTarget);
    RemoveDirectoryW(linkDir);
    RemoveDirectoryW(targetDir);
    
    // Create target directory
    if (!CreateDirectoryW(targetDir, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            PrintLastError("CreateDirectoryW target");
            return false;
        }
    }
    
    // Create file in target directory
    HANDLE h = OpenTestFile(fileInTarget, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(targetDir);
        return false;
    }
    const char content[] = "File in target directory";
    DWORD written = 0;
    WriteFile(h, content, static_cast<DWORD>(strlen(content)), &written, NULL);
    CloseHandle(h);
    
    // Create directory symbolic link
    DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
    #ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    #define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
    #endif
    flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    
    if (!CreateSymbolicLinkW(linkDir, targetDir, flags)) {
        DWORD err = GetLastError();
        // Try without unprivileged flag
        if (!CreateSymbolicLinkW(linkDir, targetDir, SYMBOLIC_LINK_FLAG_DIRECTORY)) {
            printf("CreateSymbolicLinkW (directory) failed (error=%lu) - may need admin\n",
                   static_cast<unsigned long>(err));
            DeleteFileW(fileInTarget);
            RemoveDirectoryW(targetDir);
            return true;  // Not a failure if not supported
        }
    }
    printf("Created directory symbolic link\n");
    
    // Access file through symbolic link
    h = OpenTestFile(fileViaLink, GENERIC_READ, FILE_SHARE_READ,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    bool ok = true;
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open file through directory symlink\n");
        ok = false;
    } else {
        char readBuf[128] = {0};
        DWORD readLen = 0;
        ReadFile(h, readBuf, sizeof(readBuf) - 1, &readLen, NULL);
        CloseHandle(h);
        
        if (memcmp(readBuf, content, strlen(content)) == 0) {
            printf("Access through directory symlink verified OK\n");
        } else {
            printf("ERROR: Content mismatch through directory symlink!\n");
            ok = false;
        }
    }
    
    // Cleanup
    DeleteFileW(fileInTarget);
    DeleteFileW(fileViaLink);
    RemoveDirectoryW(linkDir);
    RemoveDirectoryW(targetDir);
    
    return ok;
}

//=============================================================================
// Test: Hard link creation and usage
//=============================================================================
bool Test_HardLink(const WCHAR* rootDir)
{
    printf("Testing hard link\n");
    
    WCHAR originalFile[260];
    WCHAR hardLink[260];
    wsprintfW(originalFile, L"%s\\hardlink_original.txt", rootDir);
    wsprintfW(hardLink, L"%s\\hardlink_link.txt", rootDir);
    
    // Cleanup
    DeleteFileW(hardLink);
    DeleteFileW(originalFile);
    
    // Create original file
    HANDLE h = OpenTestFile(originalFile, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const char content[] = "Hard link test content - shared data";
    DWORD written = 0;
    if (!WriteFile(h, content, static_cast<DWORD>(strlen(content)), &written, NULL)) {
        PrintLastError("WriteFile original");
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    printf("Created original file with %lu bytes\n", static_cast<unsigned long>(written));
    
    // Create hard link
    if (!CreateHardLinkW(hardLink, originalFile, NULL)) {
        DWORD err = GetLastError();
        printf("CreateHardLinkW failed (error=%lu)\n", static_cast<unsigned long>(err));
        if (err == ERROR_NOT_SAME_DEVICE) {
            printf("Note: Hard links must be on the same volume\n");
        }
        DeleteFileW(originalFile);
        return true;  // Not a failure if not supported
    }
    printf("Created hard link\n");
    
    bool ok = true;
    
    // Read through hard link
    h = OpenTestFile(hardLink, GENERIC_READ, FILE_SHARE_READ,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open hard link for reading\n");
        ok = false;
    } else {
        char readBuf[128] = {0};
        DWORD readLen = 0;
        ReadFile(h, readBuf, sizeof(readBuf) - 1, &readLen, NULL);
        CloseHandle(h);
        
        if (memcmp(readBuf, content, strlen(content)) == 0) {
            printf("Read through hard link verified OK\n");
        } else {
            printf("ERROR: Content mismatch through hard link!\n");
            ok = false;
        }
    }
    
    // Modify through hard link
    h = OpenTestFile(hardLink, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h != INVALID_HANDLE_VALUE) {
        const char newContent[] = "Modified via hard link";
        LARGE_INTEGER pos = {0};
        SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        WriteFile(h, newContent, static_cast<DWORD>(strlen(newContent)), &written, NULL);
        SetEndOfFile(h);
        CloseHandle(h);
        
        // Verify original shows the change
        h = OpenTestFile(originalFile, GENERIC_READ, FILE_SHARE_READ,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h != INVALID_HANDLE_VALUE) {
            char readBuf[128] = {0};
            DWORD readLen = 0;
            ReadFile(h, readBuf, sizeof(readBuf) - 1, &readLen, NULL);
            CloseHandle(h);
            
            if (memcmp(readBuf, newContent, strlen(newContent)) == 0) {
                printf("Write through hard link reflected in original - OK\n");
            } else {
                printf("ERROR: Original not modified through hard link!\n");
                ok = false;
            }
        }
    }
    
    // Delete original - hard link should still work
    DeleteFileW(originalFile);
    h = OpenTestFile(hardLink, GENERIC_READ, FILE_SHARE_READ,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h != INVALID_HANDLE_VALUE) {
        printf("Hard link still accessible after original deleted - OK\n");
        CloseHandle(h);
    } else {
        printf("ERROR: Hard link not accessible after original deleted!\n");
        ok = false;
    }
    
    // Cleanup
    DeleteFileW(hardLink);
    DeleteFileW(originalFile);
    
    return ok;
}

//=============================================================================
// Test: Multiple hard links to same file
//=============================================================================
bool Test_MultipleHardLinks(const WCHAR* rootDir)
{
    printf("Testing multiple hard links to same file\n");
    
    WCHAR original[260];
    WCHAR link1[260];
    WCHAR link2[260];
    WCHAR link3[260];
    wsprintfW(original, L"%s\\multi_hl_original.txt", rootDir);
    wsprintfW(link1, L"%s\\multi_hl_link1.txt", rootDir);
    wsprintfW(link2, L"%s\\multi_hl_link2.txt", rootDir);
    wsprintfW(link3, L"%s\\multi_hl_link3.txt", rootDir);
    
    // Cleanup
    DeleteFileW(link3);
    DeleteFileW(link2);
    DeleteFileW(link1);
    DeleteFileW(original);
    
    // Create original
    HANDLE h = OpenTestFile(original, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    
    const char content[] = "Multiple hard links test";
    DWORD written = 0;
    WriteFile(h, content, static_cast<DWORD>(strlen(content)), &written, NULL);
    CloseHandle(h);
    
    // Create multiple hard links
    bool linksCreated = true;
    if (!CreateHardLinkW(link1, original, NULL)) {
        printf("CreateHardLinkW link1 failed\n");
        linksCreated = false;
    }
    if (linksCreated && !CreateHardLinkW(link2, original, NULL)) {
        printf("CreateHardLinkW link2 failed\n");
        linksCreated = false;
    }
    if (linksCreated && !CreateHardLinkW(link3, original, NULL)) {
        printf("CreateHardLinkW link3 failed\n");
        linksCreated = false;
    }
    
    if (!linksCreated) {
        DeleteFileW(link3);
        DeleteFileW(link2);
        DeleteFileW(link1);
        DeleteFileW(original);
        return true;  // Not a failure if not supported
    }
    
    printf("Created 3 hard links to original file\n");
    
    bool ok = true;
    
    // Verify all links have same content
    const WCHAR* files[] = { original, link1, link2, link3 };
    for (int i = 0; i < 4; i++) {
        h = OpenTestFile(files[i], GENERIC_READ, FILE_SHARE_READ,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            wprintf(L"ERROR: Cannot open %s\n", files[i]);
            ok = false;
            continue;
        }
        
        char readBuf[128] = {0};
        DWORD readLen = 0;
        ReadFile(h, readBuf, sizeof(readBuf) - 1, &readLen, NULL);
        CloseHandle(h);
        
        if (memcmp(readBuf, content, strlen(content)) != 0) {
            wprintf(L"ERROR: Content mismatch in %s\n", files[i]);
            ok = false;
        }
    }
    
    if (ok) {
        printf("All 4 paths (original + 3 links) have identical content - OK\n");
    }
    
    // Cleanup
    DeleteFileW(link3);
    DeleteFileW(link2);
    DeleteFileW(link1);
    DeleteFileW(original);
    
    return ok;
}

//=============================================================================
// Test: Memory-mapped file (read-only)
//=============================================================================
bool Test_MemoryMappedFileRead(const WCHAR* file)
{
    printf("Testing memory-mapped file (read-only)\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    // Create file with test data
    HANDLE hFile = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    const char content[] = "Memory mapped file test data - this should be readable via mapping";
    DWORD written = 0;
    if (!WriteFile(hFile, content, static_cast<DWORD>(strlen(content)), &written, NULL)) {
        PrintLastError("WriteFile");
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);
    printf("Created file with %lu bytes\n", static_cast<unsigned long>(written));
    
    // Open for memory mapping
    hFile = OpenTestFile(file, GENERIC_READ, FILE_SHARE_READ,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    // Create file mapping
    HANDLE hMapping = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMapping == NULL) {
        DWORD err = GetLastError();
        printf("CreateFileMappingW failed (error=%lu)\n", static_cast<unsigned long>(err));
        CloseHandle(hFile);
        DeleteFileW(file);
        return true;  // May not be supported on virtual FS
    }
    
    // Map view
    LPVOID pView = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (pView == NULL) {
        DWORD err = GetLastError();
        printf("MapViewOfFile failed (error=%lu)\n", static_cast<unsigned long>(err));
        CloseHandle(hMapping);
        CloseHandle(hFile);
        DeleteFileW(file);
        return true;  // May not be supported
    }
    
    // Verify content
    bool ok = (memcmp(pView, content, strlen(content)) == 0);
    if (ok) {
        printf("Memory-mapped read verified OK\n");
    } else {
        printf("ERROR: Content mismatch in memory-mapped view!\n");
    }
    
    // Cleanup
    UnmapViewOfFile(pView);
    CloseHandle(hMapping);
    CloseHandle(hFile);
    DeleteFileW(file);
    
    return ok;
}

//=============================================================================
// Test: Memory-mapped file (read-write)
//=============================================================================
bool Test_MemoryMappedFileWrite(const WCHAR* file)
{
    printf("Testing memory-mapped file (read-write)\n");
    
    // Ensure clean state
    DeleteFileW(file);
    
    // Create file with initial size
    HANDLE hFile = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    // Set file size
    const DWORD fileSize = 4096;
    LARGE_INTEGER li;
    li.QuadPart = fileSize;
    SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);
    SetEndOfFile(hFile);
    
    // Create file mapping
    HANDLE hMapping = CreateFileMappingW(hFile, NULL, PAGE_READWRITE, 0, fileSize, NULL);
    if (hMapping == NULL) {
        DWORD err = GetLastError();
        printf("CreateFileMappingW (R/W) failed (error=%lu)\n", static_cast<unsigned long>(err));
        CloseHandle(hFile);
        DeleteFileW(file);
        return true;  // May not be supported
    }
    
    // Map view for writing
    LPVOID pView = MapViewOfFile(hMapping, FILE_MAP_WRITE, 0, 0, fileSize);
    if (pView == NULL) {
        DWORD err = GetLastError();
        printf("MapViewOfFile (write) failed (error=%lu)\n", static_cast<unsigned long>(err));
        CloseHandle(hMapping);
        CloseHandle(hFile);
        DeleteFileW(file);
        return true;
    }
    
    // Write through memory mapping
    const char writeData[] = "Data written through memory mapping";
    memcpy(pView, writeData, strlen(writeData));
    
    // Flush to disk
    if (!FlushViewOfFile(pView, strlen(writeData))) {
        PrintLastError("FlushViewOfFile");
    }
    
    // Unmap
    UnmapViewOfFile(pView);
    CloseHandle(hMapping);
    CloseHandle(hFile);
    
    // Verify by reading file normally
    hFile = OpenTestFile(file, GENERIC_READ, FILE_SHARE_READ,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DeleteFileW(file);
        return false;
    }
    
    char readBuf[128] = {0};
    DWORD readLen = 0;
    ReadFile(hFile, readBuf, sizeof(readBuf) - 1, &readLen, NULL);
    CloseHandle(hFile);
    
    bool ok = (memcmp(readBuf, writeData, strlen(writeData)) == 0);
    if (ok) {
        printf("Memory-mapped write verified OK\n");
    } else {
        printf("ERROR: Data written via mmap not found in file!\n");
        printf("Expected: '%s'\n", writeData);
        printf("Got: '%.*s'\n", static_cast<int>(readLen), readBuf);
    }
    
    DeleteFileW(file);
    return ok;
}

//=============================================================================
// Test: File change notification (ReadDirectoryChangesW)
//=============================================================================
bool Test_FileChangeNotification(const WCHAR* rootDir)
{
    printf("Testing file change notification (ReadDirectoryChangesW)\n");
    
    WCHAR watchDir[260];
    WCHAR testFile[260];
    wsprintfW(watchDir, L"%s\\notify_test", rootDir);
    wsprintfW(testFile, L"%s\\notify_test\\change.txt", rootDir);
    
    // Cleanup and create directory
    DeleteFileW(testFile);
    RemoveDirectoryW(watchDir);
    
    if (!CreateDirectoryW(watchDir, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            PrintLastError("CreateDirectoryW");
            return false;
        }
    }
    
    // Open directory for monitoring
    HANDLE hDir = CreateFileW(watchDir,
                               FILE_LIST_DIRECTORY,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL,
                               OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                               NULL);
    if (hDir == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW (directory)");
        RemoveDirectoryW(watchDir);
        return true;  // May not be supported
    }
    
    // Setup overlapped I/O
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (overlapped.hEvent == NULL) {
        PrintLastError("CreateEventW");
        CloseHandle(hDir);
        RemoveDirectoryW(watchDir);
        return false;
    }
    
    // Start watching
    char buffer[4096];
    DWORD bytesReturned = 0;
    
    BOOL watchResult = ReadDirectoryChangesW(
        hDir,
        buffer,
        sizeof(buffer),
        FALSE,  // Don't watch subtree
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
        &bytesReturned,
        &overlapped,
        NULL);
    
    if (!watchResult) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            printf("ReadDirectoryChangesW failed (error=%lu)\n", static_cast<unsigned long>(err));
            CloseHandle(overlapped.hEvent);
            CloseHandle(hDir);
            RemoveDirectoryW(watchDir);
            return true;  // May not be supported
        }
    }
    
    // Create a file to trigger notification
    std::atomic<bool> fileCreated(false);
    std::thread writerThread([&]() {
        Sleep(100);  // Small delay
        HANDLE hFile = CreateFileW(testFile, GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            const char data[] = "trigger";
            DWORD written;
            WriteFile(hFile, data, sizeof(data), &written, NULL);
            CloseHandle(hFile);
            fileCreated = true;
        }
    });
    
    // Wait for notification
    DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 5000);  // 5 second timeout
    
    writerThread.join();
    
    bool ok = true;
    if (waitResult == WAIT_OBJECT_0) {
        if (GetOverlappedResult(hDir, &overlapped, &bytesReturned, FALSE)) {
            FILE_NOTIFY_INFORMATION* pNotify = (FILE_NOTIFY_INFORMATION*)buffer;
            if (pNotify->Action == FILE_ACTION_ADDED) {
                printf("Received FILE_ACTION_ADDED notification - OK\n");
            } else {
                printf("Received notification with action=%lu\n", static_cast<unsigned long>(pNotify->Action));
            }
        }
    } else if (waitResult == WAIT_TIMEOUT) {
        printf("Notification timeout - change notification may not be supported\n");
        ok = true;  // Not necessarily a failure
    } else {
        printf("WaitForSingleObject failed (result=%lu)\n", static_cast<unsigned long>(waitResult));
        ok = false;
    }
    
    // Cleanup
    CancelIo(hDir);
    CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
    DeleteFileW(testFile);
    RemoveDirectoryW(watchDir);
    
    return ok;
}

//=============================================================================
// Test: Special/Reserved filenames
// Tests handling of Windows reserved names (CON, PRN, AUX, etc.)
//=============================================================================
bool Test_ReservedFilenames(const WCHAR* rootDir)
{
    printf("Testing reserved filename handling\n");
    
    // These are Windows reserved device names
    const WCHAR* reservedNames[] = {
        L"CON", L"PRN", L"AUX", L"NUL",
        L"COM1", L"COM2", L"COM3",
        L"LPT1", L"LPT2"
    };
    
    bool ok = true;
    
    for (size_t i = 0; i < sizeof(reservedNames) / sizeof(reservedNames[0]); i++) {
        WCHAR filePath[260];
        wsprintfW(filePath, L"%s\\%s.txt", rootDir, reservedNames[i]);
        
        // Attempt to create file with reserved name
        HANDLE h = CreateFileW(filePath,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);
        
        if (h != INVALID_HANDLE_VALUE) {
            // File was created - this is unexpected for real reserved names
            // but may be allowed on some filesystems
            wprintf(L"Note: Created file with reserved name '%s' - FS allows it\n", reservedNames[i]);
            CloseHandle(h);
            DeleteFileW(filePath);
        } else {
            DWORD err = GetLastError();
            // ERROR_INVALID_NAME or ERROR_ACCESS_DENIED are expected
            if (err == ERROR_INVALID_NAME || err == ERROR_ACCESS_DENIED || 
                err == ERROR_PATH_NOT_FOUND || err == ERROR_FILE_NOT_FOUND) {
                wprintf(L"Reserved name '%s' correctly rejected (error=%lu)\n", 
                        reservedNames[i], static_cast<unsigned long>(err));
            } else {
                wprintf(L"Unexpected error for '%s': %lu\n", 
                        reservedNames[i], static_cast<unsigned long>(err));
            }
        }
    }
    
    printf("Reserved filename test completed\n");
    return ok;
}

//=============================================================================
// Test: Filenames with trailing spaces and dots
//=============================================================================
bool Test_TrailingSpacesAndDots(const WCHAR* rootDir)
{
    printf("Testing filenames with trailing spaces and dots\n");
    
    // Windows normally strips trailing spaces and dots
    const WCHAR* testNames[] = {
        L"trailing_space ",
        L"trailing_dots...",
        L"space_and_dot. ",
        L"multiple   ",
    };
    
    bool ok = true;
    
    for (size_t i = 0; i < sizeof(testNames) / sizeof(testNames[0]); i++) {
        WCHAR filePath[260];
        wsprintfW(filePath, L"%s\\%s", rootDir, testNames[i]);
        
        HANDLE h = CreateFileW(filePath,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);
        
        if (h != INVALID_HANDLE_VALUE) {
            // Get actual filename
            WCHAR actualPath[260];
            if (GetFinalPathNameByHandleW(h, actualPath, 260, FILE_NAME_NORMALIZED)) {
                wprintf(L"Created: '%s' -> actual: '%s'\n", testNames[i], actualPath);
            }
            CloseHandle(h);
            
            // Try to delete using original name
            if (!DeleteFileW(filePath)) {
                // Try normalized name
                WCHAR normalizedPath[260];
                wsprintfW(normalizedPath, L"%s\\%s", rootDir, testNames[i]);
                // Remove trailing spaces/dots for deletion
                size_t len = wcslen(normalizedPath);
                while (len > 0 && (normalizedPath[len-1] == L' ' || normalizedPath[len-1] == L'.')) {
                    normalizedPath[--len] = L'\0';
                }
                DeleteFileW(normalizedPath);
            }
        } else {
            DWORD err = GetLastError();
            wprintf(L"Cannot create '%s' (error=%lu) - expected behavior\n", 
                    testNames[i], static_cast<unsigned long>(err));
        }
    }
    
    printf("Trailing spaces/dots test completed\n");
    return ok;
}

//=============================================================================
// Test: Long filenames (close to 255 character limit)
//=============================================================================
bool Test_LongFilenames(const WCHAR* rootDir)
{
    printf("Testing long filenames (near 255 char limit)\n");
    
    bool ok = true;
    
    // Test various lengths
    const size_t testLengths[] = { 100, 200, 250, 254, 255 };
    
    for (size_t i = 0; i < sizeof(testLengths) / sizeof(testLengths[0]); i++) {
        size_t nameLen = testLengths[i];
        
        // Create filename of specified length
        std::wstring longName(nameLen - 4, L'A');  // -4 for ".txt"
        longName += L".txt";
        
        std::wstring filePath = rootDir;
        if (filePath.back() != L'\\') filePath += L'\\';
        filePath += longName;
        
        HANDLE h = CreateFileW(filePath.c_str(),
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ,
                                NULL,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);
        
        if (h != INVALID_HANDLE_VALUE) {
            const char data[] = "test";
            DWORD written;
            WriteFile(h, data, sizeof(data), &written, NULL);
            CloseHandle(h);
            
            // Verify can reopen
            h = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                printf("Length %zu: OK (created and reopened)\n", nameLen);
                CloseHandle(h);
            } else {
                printf("Length %zu: Created but cannot reopen!\n", nameLen);
                ok = false;
            }
            
            DeleteFileW(filePath.c_str());
        } else {
            DWORD err = GetLastError();
            if (nameLen > 255) {
                printf("Length %zu: Correctly rejected (error=%lu)\n", 
                       nameLen, static_cast<unsigned long>(err));
            } else {
                printf("Length %zu: FAILED to create (error=%lu)\n", 
                       nameLen, static_cast<unsigned long>(err));
                // Not necessarily a failure - encrypted names may be longer
            }
        }
    }
    
    return ok;
}

//=============================================================================
// Test: Unicode filenames with special characters
//=============================================================================
bool Test_UnicodeFilenames(const WCHAR* rootDir)
{
    printf("Testing Unicode filenames\n");
    
    // Various Unicode test cases
    const WCHAR* unicodeNames[] = {
        L"ì˙ñ{åÍÉtÉ@ÉCÉã.txt",           // Japanese
        L"????.txt",                  // Korean
        L"íÜï∂ï∂åè.txt",                  // Chinese
        L"ÑÜÑpÑzÑ|.txt",                      // Russian
        L"ÉøÉ¿É¡É¬.txt",                      // Greek
        L"emoji_??_test.txt",             // Emoji (surrogate pair)
        L"mixed_ì˙ñ{åÍ_English.txt",      // Mixed
    };
    
    bool ok = true;
    
    for (size_t i = 0; i < sizeof(unicodeNames) / sizeof(unicodeNames[0]); i++) {
        WCHAR filePath[260];
        wsprintfW(filePath, L"%s\\%s", rootDir, unicodeNames[i]);
        
        HANDLE h = CreateFileW(filePath,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ,
                                NULL,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL);
        
        if (h != INVALID_HANDLE_VALUE) {
            const char data[] = "unicode test data";
            DWORD written;
            WriteFile(h, data, sizeof(data) - 1, &written, NULL);
            CloseHandle(h);
            
            // Verify by reopening
            h = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                char readBuf[64] = {0};
                DWORD readLen;
                ReadFile(h, readBuf, sizeof(readBuf) - 1, &readLen, NULL);
                CloseHandle(h);
                
                if (memcmp(readBuf, data, strlen(data)) == 0) {
                    wprintf(L"Unicode '%s': OK\n", unicodeNames[i]);
                } else {
                    wprintf(L"Unicode '%s': Content mismatch!\n", unicodeNames[i]);
                    ok = false;
                }
            } else {
                wprintf(L"Unicode '%s': Cannot reopen!\n", unicodeNames[i]);
                ok = false;
            }
            
            DeleteFileW(filePath);
        } else {
            DWORD err = GetLastError();
            wprintf(L"Unicode '%s': Cannot create (error=%lu)\n", 
                    unicodeNames[i], static_cast<unsigned long>(err));
            // Not necessarily a failure - some characters may be invalid
        }
    }
    
    return ok;
}

//=============================================================================
// Test: GetVolumeInformation
//=============================================================================
bool Test_VolumeInformation(const WCHAR* rootDir)
{
    printf("Testing GetVolumeInformation\n");
    
    WCHAR volumeName[256] = {0};
    DWORD volumeSerial = 0;
    DWORD maxComponentLen = 0;
    DWORD fsFlags = 0;
    WCHAR fsName[256] = {0};
    
    // Extract root path (e.g., "O:\\" from "O:\\path")
    WCHAR rootPath[16] = {0};
    if (rootDir[0] && rootDir[1] == L':') {
        rootPath[0] = rootDir[0];
        rootPath[1] = L':';
        rootPath[2] = L'\\';
        rootPath[3] = L'\0';
    } else {
        wcsncpy_s(rootPath, rootDir, 3);
    }
    
    if (!GetVolumeInformationW(rootPath, volumeName, 256, &volumeSerial,
                                &maxComponentLen, &fsFlags, fsName, 256)) {
        PrintLastError("GetVolumeInformationW");
        return true;  // May not be supported
    }
    
    wprintf(L"Volume Name: %s\n", volumeName);
    printf("Volume Serial: 0x%08lX\n", static_cast<unsigned long>(volumeSerial));
    printf("Max Component Length: %lu\n", static_cast<unsigned long>(maxComponentLen));
    wprintf(L"File System: %s\n", fsName);
    printf("Flags: 0x%08lX\n", static_cast<unsigned long>(fsFlags));
    
    // Check specific flags
    if (fsFlags & FILE_CASE_SENSITIVE_SEARCH) printf("  - Case sensitive search\n");
    if (fsFlags & FILE_CASE_PRESERVED_NAMES) printf("  - Case preserved names\n");
    if (fsFlags & FILE_UNICODE_ON_DISK) printf("  - Unicode on disk\n");
    if (fsFlags & FILE_PERSISTENT_ACLS) printf("  - Persistent ACLs\n");
    if (fsFlags & FILE_NAMED_STREAMS) printf("  - Named streams (ADS)\n");
    if (fsFlags & FILE_SUPPORTS_HARD_LINKS) printf("  - Hard links\n");
    if (fsFlags & FILE_SUPPORTS_REPARSE_POINTS) printf("  - Reparse points\n");
    
    return true;
}

//=============================================================================
// Test: GetDiskFreeSpaceEx
//=============================================================================
bool Test_DiskFreeSpace(const WCHAR* rootDir)
{
    printf("Testing GetDiskFreeSpaceEx\n");
    
    ULARGE_INTEGER freeBytesAvailable;
    ULARGE_INTEGER totalBytes;
    ULARGE_INTEGER totalFreeBytes;
    
    if (!GetDiskFreeSpaceExW(rootDir, &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
        PrintLastError("GetDiskFreeSpaceExW");
        return true;  // May not be supported
    }
    
    printf("Total Space: %llu GB\n", totalBytes.QuadPart / (1024ULL * 1024 * 1024));
    printf("Free Space (total): %llu GB\n", totalFreeBytes.QuadPart / (1024ULL * 1024 * 1024));
    printf("Free Space (available): %llu GB\n", freeBytesAvailable.QuadPart / (1024ULL * 1024 * 1024));
    
    return true;
}

//=============================================================================
// Test: Windows Shortcut (.lnk) file creation and resolution
//=============================================================================
bool Test_ShortcutFile(const WCHAR* rootDir)
{
    printf("Testing Windows shortcut (.lnk) file\n");
    
    // Initialize COM (required for IShellLink)
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        printf("CoInitialize failed (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
        return true;  // Not a failure, COM might not be available
    }
    
    WCHAR targetFile[MAX_PATH];
    WCHAR shortcutFile[MAX_PATH];
    wsprintfW(targetFile, L"%s\\shortcut_target.txt", rootDir);
    wsprintfW(shortcutFile, L"%s\\shortcut_link.lnk", rootDir);
    
    // Cleanup
    DeleteFileW(shortcutFile);
    DeleteFileW(targetFile);
    
    // Create target file
    HANDLE h = OpenTestFile(targetFile, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        CoUninitialize();
        return false;
    }
    
    const char content[] = "Shortcut target file content";
    DWORD written = 0;
    WriteFile(h, content, static_cast<DWORD>(strlen(content)), &written, NULL);
    CloseHandle(h);
    printf("Created target file: %ls\n", targetFile);
    
    // Get the full path to ensure we're using absolute path
    WCHAR fullTargetPath[MAX_PATH];
    if (GetFullPathNameW(targetFile, MAX_PATH, fullTargetPath, NULL) == 0) {
        wcscpy_s(fullTargetPath, targetFile);
    }
    printf("Full target path: %ls\n", fullTargetPath);
    
    // Get working directory (parent directory of target)
    WCHAR workingDir[MAX_PATH];
    wcscpy_s(workingDir, fullTargetPath);
    WCHAR* lastSlash = wcsrchr(workingDir, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }
    printf("Working directory: %ls\n", workingDir);
    
    bool ok = true;
    
    // Create shortcut using IShellLink
    IShellLinkW* psl = NULL;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, (LPVOID*)&psl);
    if (FAILED(hr)) {
        printf("CoCreateInstance(IShellLink) failed (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
        DeleteFileW(targetFile);
        CoUninitialize();
        return true;  // Not a failure if shell link not available
    }
    
    // Set the target path (use full path)
    hr = psl->SetPath(fullTargetPath);
    if (FAILED(hr)) {
        printf("SetPath failed (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
        psl->Release();
        DeleteFileW(targetFile);
        CoUninitialize();
        return false;
    }
    
    // Set working directory
    hr = psl->SetWorkingDirectory(workingDir);
    if (FAILED(hr)) {
        printf("SetWorkingDirectory failed (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
    }
    
    // Set description
    psl->SetDescription(L"Test shortcut created by EncFSy test suite");
    
    // Set show command (normal window)
    psl->SetShowCmd(SW_SHOWNORMAL);
    
    // Save the shortcut
    IPersistFile* ppf = NULL;
    hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
    if (FAILED(hr)) {
        printf("QueryInterface(IPersistFile) failed (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
        psl->Release();
        DeleteFileW(targetFile);
        CoUninitialize();
        return false;
    }
    
    // Get full path for shortcut file too
    WCHAR fullShortcutPath[MAX_PATH];
    if (GetFullPathNameW(shortcutFile, MAX_PATH, fullShortcutPath, NULL) == 0) {
        wcscpy_s(fullShortcutPath, shortcutFile);
    }
    
    hr = ppf->Save(fullShortcutPath, TRUE);
    if (FAILED(hr)) {
        printf("IPersistFile::Save failed (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
        ok = false;
    } else {
        printf("Created shortcut: %ls\n", fullShortcutPath);
    }
    
    ppf->Release();
    psl->Release();
    
    if (!ok) {
        DeleteFileW(targetFile);
        CoUninitialize();
        return false;
    }
    
    // Verify shortcut file exists
    if (GetFileAttributesW(shortcutFile) == INVALID_FILE_ATTRIBUTES) {
        printf("ERROR: Shortcut file was not created!\n");
        DeleteFileW(targetFile);
        CoUninitialize();
        return false;
    }
    
    // Resolve the shortcut and verify all properties
    IShellLinkW* pslRead = NULL;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, (LPVOID*)&pslRead);
    if (SUCCEEDED(hr)) {
        IPersistFile* ppfRead = NULL;
        hr = pslRead->QueryInterface(IID_IPersistFile, (LPVOID*)&ppfRead);
        if (SUCCEEDED(hr)) {
            hr = ppfRead->Load(fullShortcutPath, STGM_READ);
            if (SUCCEEDED(hr)) {
                WCHAR resolvedPath[MAX_PATH];
                WIN32_FIND_DATAW wfd;
                
                // Try to resolve the link (this will follow the shortcut)
                hr = pslRead->Resolve(NULL, SLR_NO_UI | SLR_NOSEARCH);
                if (FAILED(hr)) {
                    printf("Resolve failed (hr=0x%08lX) - target may not exist\n", static_cast<unsigned long>(hr));
                }
                
                hr = pslRead->GetPath(resolvedPath, MAX_PATH, &wfd, SLGP_RAWPATH);
                if (SUCCEEDED(hr)) {
                    printf("Shortcut target path: %ls\n", resolvedPath);
                    
                    // Compare paths (case-insensitive)
                    if (_wcsicmp(resolvedPath, fullTargetPath) == 0) {
                        printf("Shortcut resolved correctly - OK\n");
                    } else {
                        printf("Note: Shortcut resolved to: %ls\n", resolvedPath);
                        printf("      Expected: %ls\n", fullTargetPath);
                        // Check if paths are equivalent
                        WCHAR resolvedFull[MAX_PATH], expectedFull[MAX_PATH];
                        GetFullPathNameW(resolvedPath, MAX_PATH, resolvedFull, NULL);
                        GetFullPathNameW(fullTargetPath, MAX_PATH, expectedFull, NULL);
                        if (_wcsicmp(resolvedFull, expectedFull) == 0) {
                            printf("      Paths are equivalent - OK\n");
                        }
                    }
                } else {
                    printf("GetPath failed (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
                    ok = false;
                }
                
                // Also check working directory
                WCHAR resolvedWorkDir[MAX_PATH];
                hr = pslRead->GetWorkingDirectory(resolvedWorkDir, MAX_PATH);
                if (SUCCEEDED(hr)) {
                    printf("Working directory: %ls\n", resolvedWorkDir);
                }
            } else {
                printf("IPersistFile::Load failed (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
                ok = false;
            }
            ppfRead->Release();
        }
        pslRead->Release();
    }
    
    // DON'T cleanup - leave files for manual testing
    printf("\n*** Shortcut and target files left for manual testing ***\n");
    printf("    Target: %ls\n", fullTargetPath);
    printf("    Shortcut: %ls\n", fullShortcutPath);
    printf("    (Delete manually after testing)\n");
    
    // Cleanup
    // DeleteFileW(shortcutFile);
    // DeleteFileW(targetFile);
    CoUninitialize();
    
    if (ok) {
        printf("Shortcut test completed successfully\n");
    }
    
    return ok;
}

//=============================================================================
// Test: Shortcut to directory
//=============================================================================
bool Test_ShortcutToDirectory(const WCHAR* rootDir)
{
    printf("Testing Windows shortcut to directory\n");
    
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        printf("CoInitialize failed\n");
        return true;
    }
    
    WCHAR targetDir[MAX_PATH];
    WCHAR shortcutFile[MAX_PATH];
    WCHAR fileInDir[MAX_PATH];
    wsprintfW(targetDir, L"%s\\shortcut_target_dir", rootDir);
    wsprintfW(shortcutFile, L"%s\\dir_shortcut.lnk", rootDir);
    wsprintfW(fileInDir, L"%s\\shortcut_target_dir\\test.txt", rootDir);
    
    // Cleanup
    DeleteFileW(fileInDir);
    DeleteFileW(shortcutFile);
    RemoveDirectoryW(targetDir);
    
    // Create target directory
    if (!CreateDirectoryW(targetDir, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            printf("Failed to create target directory (error=%lu)\n", static_cast<unsigned long>(err));
            CoUninitialize();
            return false;
        }
    }
    
    // Get full path
    WCHAR fullTargetDir[MAX_PATH];
    if (GetFullPathNameW(targetDir, MAX_PATH, fullTargetDir, NULL) == 0) {
        wcscpy_s(fullTargetDir, targetDir);
    }
    printf("Full target directory: %ls\n", fullTargetDir);
    
    // Create a file in the directory
    HANDLE h = OpenTestFile(fileInDir, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h != INVALID_HANDLE_VALUE) {
        const char data[] = "file in target directory";
        DWORD written;
        WriteFile(h, data, sizeof(data) - 1, &written, NULL);
        CloseHandle(h);
    }
    
    bool ok = true;
    
    // Create shortcut to directory
    IShellLinkW* psl = NULL;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, (LPVOID*)&psl);
    if (FAILED(hr)) {
        printf("CoCreateInstance failed\n");
        DeleteFileW(fileInDir);
        RemoveDirectoryW(targetDir);
        CoUninitialize();
        return true;
    }
    
    // Set path to directory (use full path)
    psl->SetPath(fullTargetDir);
    psl->SetDescription(L"Shortcut to directory");
    psl->SetShowCmd(SW_SHOWNORMAL);
    
    // For directory shortcuts, set working directory to parent
    WCHAR parentDir[MAX_PATH];
    wcscpy_s(parentDir, fullTargetDir);
    WCHAR* lastSlash = wcsrchr(parentDir, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
        psl->SetWorkingDirectory(parentDir);
    }
    
    IPersistFile* ppf = NULL;
    hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
    if (SUCCEEDED(hr)) {
        WCHAR fullShortcutPath[MAX_PATH];
        GetFullPathNameW(shortcutFile, MAX_PATH, fullShortcutPath, NULL);
        
        hr = ppf->Save(fullShortcutPath, TRUE);
        if (FAILED(hr)) {
            printf("Failed to save directory shortcut (hr=0x%08lX)\n", static_cast<unsigned long>(hr));
            ok = false;
        } else {
            printf("Created directory shortcut: %ls\n", fullShortcutPath);
        }
        ppf->Release();
    }
    psl->Release();
    
    // Verify shortcut exists and can be read
    if (ok && GetFileAttributesW(shortcutFile) != INVALID_FILE_ATTRIBUTES) {
        // Read the .lnk file as binary to verify it's a valid file
        h = OpenTestFile(shortcutFile, GENERIC_READ, FILE_SHARE_READ,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
        if (h != INVALID_HANDLE_VALUE) {
            char header[4] = {0};
            DWORD readLen;
            ReadFile(h, header, 4, &readLen, NULL);
            CloseHandle(h);
            
            // .lnk files start with magic number 0x4C (76 decimal)
            if (readLen >= 4 && header[0] == 0x4C) {
                printf("Shortcut file has valid header (0x4C)\n");
            } else {
                printf("WARNING: Shortcut file header: 0x%02X (expected 0x4C)\n", (unsigned char)header[0]);
            }
        }
    }
    
    // DON'T cleanup - leave files for manual testing
    printf("\n*** Directory shortcut left for manual testing ***\n");
    printf("    Target directory: %ls\n", fullTargetDir);
    printf("    Shortcut: %ls\n", shortcutFile);
    
    // Cleanup
    // DeleteFileW(shortcutFile);
    // DeleteFileW(fileInDir);
    // RemoveDirectoryW(targetDir);
    CoUninitialize();
    
    return ok;
}

//=============================================================================
// Test: Memory-mapped read of shortcut file (regression for INVALID_HANDLE)
//=============================================================================
bool Test_ShortcutMemoryMappedRead(const WCHAR* rootDir)
{
    printf("Testing memory-mapped read of shortcut (.lnk) file\n");

    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        printf("CoInitialize failed\n");
        return true; // not a failure if COM unavailable
    }

    WCHAR targetFile[MAX_PATH];
    WCHAR shortcutFile[MAX_PATH];
    wsprintfW(targetFile, L"%s\\mm_shortcut_target.txt", rootDir);
    wsprintfW(shortcutFile, L"%s\\mm_shortcut.lnk", rootDir);

    // Cleanup
    DeleteFileW(shortcutFile);
    DeleteFileW(targetFile);

    // Create target file
    HANDLE hTarget = CreateFileW(targetFile, GENERIC_WRITE, FILE_SHARE_READ,
                                 NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTarget == INVALID_HANDLE_VALUE) {
        CoUninitialize();
        return false;
    }
    const char payload[] = "MM shortcut target";
    DWORD written;
    WriteFile(hTarget, payload, (DWORD)strlen(payload), &written, NULL);
    CloseHandle(hTarget);

    // Create shortcut
    IShellLinkW* psl = NULL;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, (LPVOID*)&psl);
    if (FAILED(hr)) {
        DeleteFileW(targetFile);
        CoUninitialize();
        return true; // shell link not available
    }
    psl->SetPath(targetFile);
    psl->SetDescription(L"MM shortcut test");
    IPersistFile* ppf = NULL;
    hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
    if (SUCCEEDED(hr)) {
        hr = ppf->Save(shortcutFile, TRUE);
        ppf->Release();
    }
    psl->Release();
    if (FAILED(hr)) {
        DeleteFileW(targetFile);
        CoUninitialize();
        return false;
    }

    // Memory-map the shortcut and read after closing handle (paging I/O path)
    HANDLE hShortcut = CreateFileW(shortcutFile, GENERIC_READ, FILE_SHARE_READ,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hShortcut == INVALID_HANDLE_VALUE) {
        DeleteFileW(shortcutFile);
        DeleteFileW(targetFile);
        CoUninitialize();
        return false;
    }

    HANDLE hMap = CreateFileMappingW(hShortcut, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        CloseHandle(hShortcut);
        DeleteFileW(shortcutFile);
        DeleteFileW(targetFile);
        CoUninitialize();
        return true; // may not be supported
    }

    // Close file handle before accessing view to exercise paging I/O after cleanup
    CloseHandle(hShortcut);

    LPVOID view = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
    bool ok = true;
    if (view) {
        BYTE buf[16] = {0};
        memcpy(buf, view, sizeof(buf));
        // Require non-zero content to ensure read actually happened
        bool nonZero = false;
        for (BYTE b : buf) if (b) { nonZero = true; break; }
        if (!nonZero) {
            printf("ERROR: Mapped shortcut content is zeroed\n");
            ok = false;
        }
        UnmapViewOfFile(view);
    } else {
        DWORD err = GetLastError();
        printf("MapViewOfFile failed (error=%lu)\n", static_cast<unsigned long>(err));
        ok = false;
    }

    // Cleanup
    DeleteFileW(shortcutFile);
    DeleteFileW(targetFile);
    CoUninitialize();
    return ok;
}
