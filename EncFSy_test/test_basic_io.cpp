// test_basic_io.cpp - Basic I/O tests
#include "test_common.h"

//=============================================================================
// Test: Basic directory operations (create, rename, remove)
//=============================================================================
bool Test_DirectoryOps(const WCHAR* rootDir)
{
    WCHAR subDir[260];
    wsprintfW(subDir, L"%s\\DirTest", rootDir);
    if (!CreateDirectoryW(subDir, NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) { PrintLastError("CreateDirectoryW"); return false; }
    }

    WCHAR renamed[260];
    wsprintfW(renamed, L"%s\\DirRenamed", rootDir);
    if (!MoveFileExW(subDir, renamed, MOVEFILE_COPY_ALLOWED)) { PrintLastError("MoveFileExW"); return false; }

    if (!RemoveDirectoryW(renamed)) { PrintLastError("RemoveDirectoryW"); return false; }
    return true;
}

//=============================================================================
// Test: Create file and print its final normalized path
//=============================================================================
bool Test_CreateAndPrintPath(const WCHAR* file)
{
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = PrintFinalPath(h);
    CloseHandle(h);
    return ok;
}

//=============================================================================
// Test: Case-insensitive open
//=============================================================================
bool Test_CaseInsensitiveOpen(const WCHAR* fileLowerCase, const WCHAR* fileCaseVariant)
{
    // First, ensure the directory structure exists and create the test file
    // Extract parent directory from fileLowerCase
    std::wstring path(fileLowerCase);
    size_t lastSep = path.find_last_of(L'\\');
    if (lastSep != std::wstring::npos && lastSep > 0) {
        std::wstring parentDir = path.substr(0, lastSep);
        // Create parent directories recursively
        size_t pos = 0;
        while ((pos = parentDir.find(L'\\', pos + 1)) != std::wstring::npos) {
            std::wstring dir = parentDir.substr(0, pos);
            CreateDirectoryW(dir.c_str(), NULL);
        }
        CreateDirectoryW(parentDir.c_str(), NULL);
    }

    // Create the test file if it doesn't exist
    if (!PathExists(fileLowerCase)) {
        HANDLE h = OpenTestFile(fileLowerCase, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("Failed to create test file '%ls'\n", fileLowerCase);
            return false;
        }
        const char testData[] = "case-insensitive-test";
        DWORD written;
        WriteFile(h, testData, sizeof(testData) - 1, &written, NULL);
        CloseHandle(h);
        printf("Created test file: '%ls'\n", fileLowerCase);
    }

    // Now try to open with case variant
    HANDLE h = OpenTestFile(fileCaseVariant, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to open with case variant '%ls'\n", fileCaseVariant);
        return false;
    }
    bool ok = PrintFinalPath(h);
    CloseHandle(h);
    return ok;
}

//=============================================================================
// Test: Basic buffered IO with seek/read/write
//=============================================================================
bool Test_BufferedIO(const WCHAR* file)
{
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER pos; pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }

    const char writeData[] = "ABCDEFG";
    const DWORD size = static_cast<DWORD>(sizeof(writeData) - 1);
    std::vector<char> buff(size, 0);
    DWORD ioLen = 0;

    if (!ReadFile(h, buff.data(), size, &ioLen, NULL)) { PrintLastError("ReadFile"); CloseHandle(h); return false; }
    printf("ReadLen at 0: %lu\n", static_cast<unsigned long>(ioLen));

    pos.QuadPart = 100;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    ioLen = 0;
    if (!ReadFile(h, buff.data(), size, &ioLen, NULL)) { PrintLastError("ReadFile"); CloseHandle(h); return false; }
    printf("ReadLen at 100: %lu\n", static_cast<unsigned long>(ioLen));

    if (!WriteFile(h, writeData, size, &ioLen, NULL)) { PrintLastError("WriteFile"); CloseHandle(h); return false; }

    pos.QuadPart = 100;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    buff.assign(size, 0);
    ioLen = 0;
    if (!ReadFile(h, buff.data(), size, &ioLen, NULL)) { PrintLastError("ReadFile"); CloseHandle(h); return false; }
    printf("Data at 100: %lu bytes, '%.*s'\n", static_cast<unsigned long>(ioLen), static_cast<int>(ioLen), buff.data());

    CloseHandle(h);
    return true;
}

//=============================================================================
// Test: FILE_APPEND_DATA behavior
//=============================================================================
bool Test_AppendWrite(const WCHAR* file)
{
    HANDLE h = OpenTestFile(file, FILE_APPEND_DATA, FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER pos; pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }

    const char writeData[] = "ABCDEFG";
    const DWORD size = static_cast<DWORD>(sizeof(writeData) - 1);
    DWORD ioLen = 0;
    if (!WriteFile(h, writeData, size, &ioLen, NULL)) { PrintLastError("WriteFile"); CloseHandle(h); return false; }

    CloseHandle(h);
    return true;
}

//=============================================================================
// Test: No-buffering I/O (sector-aligned)
//=============================================================================
bool Test_NoBufferingIO(const WCHAR* file, const WCHAR* drive)
{
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD bytesPerSector = 0;
    if (!GetDiskFreeSpaceW(drive, NULL, &bytesPerSector, NULL, NULL)) { PrintLastError("GetDiskFreeSpaceW"); CloseHandle(h); return false; }
    printf("Sector size: %lu\n", static_cast<unsigned long>(bytesPerSector));

    char* buff = static_cast<char*>(VirtualAlloc(NULL, bytesPerSector, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!buff) { printf("VirtualAlloc ERROR\n"); CloseHandle(h); return false; }
    memset(buff, 'A', bytesPerSector);

    DWORD ioLen = 0;
    if (!WriteFile(h, buff, bytesPerSector, &ioLen, NULL)) { PrintLastError("WriteFile"); VirtualFree(buff, 0, MEM_RELEASE); CloseHandle(h); return false; }

    LARGE_INTEGER pos; pos.QuadPart = 0;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); VirtualFree(buff, 0, MEM_RELEASE); CloseHandle(h); return false; }

    memset(buff, 0, bytesPerSector);
    if (!ReadFile(h, buff, bytesPerSector, &ioLen, NULL)) { PrintLastError("ReadFile"); VirtualFree(buff, 0, MEM_RELEASE); CloseHandle(h); return false; }
    printf("Read %lu bytes, first 8 bytes: '%.*s'\n", static_cast<unsigned long>(ioLen), 8, buff);

    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); VirtualFree(buff, 0, MEM_RELEASE); CloseHandle(h); return false; }
    ioLen = 0;
    if (!ReadFile(h, buff, bytesPerSector - 1, &ioLen, NULL)) {
        PrintLastError("ReadFile (expected failure with unaligned size)");
    }

    VirtualFree(buff, 0, MEM_RELEASE);
    CloseHandle(h);
    return true;
}

//=============================================================================
// Test: Alternate Data Streams
//=============================================================================
bool Test_AlternateDataStreams(const WCHAR* baseFile)
{
    HANDLE base = OpenTestFile(baseFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (base == INVALID_HANDLE_VALUE) return false;
    CloseHandle(base);

    WCHAR streamPath[260];
    wsprintfW(streamPath, L"%s:metadata", baseFile);
    HANDLE h = OpenTestFile(streamPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;
    const char payload[] = "stream-data";
    DWORD ioLen = 0;
    if (!WriteFile(h, payload, static_cast<DWORD>(sizeof(payload) - 1), &ioLen, NULL)) { PrintLastError("WriteFile"); CloseHandle(h); return false; }

    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    char buf[32] = {0};
    ioLen = 0;
    if (!ReadFile(h, buf, sizeof(buf), &ioLen, NULL)) { PrintLastError("ReadFile"); CloseHandle(h); return false; }
    printf("ADS read: %lu bytes, '%.*s'\n", static_cast<unsigned long>(ioLen), static_cast<int>(ioLen), buf);
    CloseHandle(h);
    return true;
}

//=============================================================================
// Test: File attributes and times
//=============================================================================
bool Test_FileAttributesAndTimes(const WCHAR* file)
{
    // First, ensure any existing file is removed
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE);
    if (h == INVALID_HANDLE_VALUE) return false;

    FILETIME c, a, w;
    GetSystemTimeAsFileTime(&c);
    a = c; w = c;
    if (!SetFileTime(h, &c, &a, &w)) { PrintLastError("SetFileTime"); CloseHandle(h); return false; }

    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(h, &info)) { PrintLastError("GetFileInformationByHandle"); CloseHandle(h); return false; }
    printf("Attributes: 0x%08lx\n", static_cast<unsigned long>(info.dwFileAttributes));

    CloseHandle(h);
    
    // Reset attributes to NORMAL so subsequent tests can access the file
    SetFileAttributesW(file, FILE_ATTRIBUTE_NORMAL);
    
    // Delete the test file to avoid interference with subsequent tests
    DeleteFileW(file);
    
    return true;
}

//=============================================================================
// Test: Sharing modes and locks
//=============================================================================
bool Test_SharingAndLocks(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h1 = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h1 == INVALID_HANDLE_VALUE) return false;

    // Try opening with read-only access, sharing both read and write
    HANDLE h2 = OpenTestFile(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    bool secondOpenSucceeded = (h2 != INVALID_HANDLE_VALUE);
    printf("Second handle open (read-only, shared): %s\n", secondOpenSucceeded ? "success" : "failed");

    OVERLAPPED ov = {};
    if (!LockFileEx(h1, LOCKFILE_EXCLUSIVE_LOCK, 0, 16, 0, &ov)) { 
        PrintLastError("LockFileEx"); 
        CloseHandle(h1); 
        if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2); 
        return false; 
    }
    printf("Exclusive lock acquired on first 16 bytes\n");

    if (!UnlockFileEx(h1, 0, 16, 0, &ov)) { PrintLastError("UnlockFileEx"); }
    printf("Lock released\n");

    if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
    CloseHandle(h1);
    
    // Clean up
    DeleteFileW(file);
    
    return true;
}

//=============================================================================
// Test: Sparse file behavior
//=============================================================================
bool Test_SparseFile(const WCHAR* file)
{
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD tmp = 0;
    if (!DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &tmp, NULL)) { 
        DWORD error = GetLastError();
        printf("FSCTL_SET_SPARSE not supported (error %lu) - expected on virtual file systems\n", 
               static_cast<unsigned long>(error));
    }

    LARGE_INTEGER pos; pos.QuadPart = 1LL << 20;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    if (!SetEndOfFile(h)) { PrintLastError("SetEndOfFile"); CloseHandle(h); return false; }

    CloseHandle(h);
    
    // Clean up
    DeleteFileW(file);
    
    return true;
}

//=============================================================================
// Test: Block boundary writes/reads
//=============================================================================
bool Test_BlockBoundaryIO(const WCHAR* file, DWORD blockSize)
{
    // Ensure clean state
    DeleteFileW(file);
    
    HANDLE h = OpenTestFile(file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::vector<char> pre(blockSize - 4, 'P');
    DWORD ioLen = 0; LARGE_INTEGER pos; pos.QuadPart = 0;
    if (!WriteFile(h, pre.data(), static_cast<DWORD>(pre.size()), &ioLen, NULL)) { PrintLastError("WriteFile"); CloseHandle(h); return false; }

    const char cross[] = "XXXXXXXX";
    if (!WriteFile(h, cross, static_cast<DWORD>(sizeof(cross) - 1), &ioLen, NULL)) { PrintLastError("WriteFile"); CloseHandle(h); return false; }

    pos.QuadPart = blockSize - 8;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    char buf[32] = {0};
    if (!ReadFile(h, buf, sizeof(buf), &ioLen, NULL)) { PrintLastError("ReadFile"); CloseHandle(h); return false; }
    printf("Boundary read (%lld): %lu bytes, '%.*s'\n", pos.QuadPart, static_cast<unsigned long>(ioLen), static_cast<int>(ioLen), buf);

    pos.QuadPart = (static_cast<LONGLONG>(blockSize) * 2) - 1;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    const char cross2[] = "YZ";
    if (!WriteFile(h, cross2, static_cast<DWORD>(sizeof(cross2) - 1), &ioLen, NULL)) { PrintLastError("WriteFile"); CloseHandle(h); return false; }

    pos.QuadPart = (static_cast<LONGLONG>(blockSize) * 2) - 8;
    if (!SetFilePointerEx(h, pos, NULL, FILE_BEGIN)) { PrintLastError("SetFilePointerEx"); CloseHandle(h); return false; }
    memset(buf, 0, sizeof(buf));
    if (!ReadFile(h, buf, sizeof(buf), &ioLen, NULL)) { PrintLastError("ReadFile"); CloseHandle(h); return false; }
    printf("Boundary read2 (%lld): %lu bytes, '%.*s'\n", pos.QuadPart, static_cast<unsigned long>(ioLen), static_cast<int>(ioLen), buf);

    CloseHandle(h);
    
    // Clean up
    DeleteFileW(file);
    
    return true;
}
