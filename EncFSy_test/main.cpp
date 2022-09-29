// EncFSy_test.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <windows.h>
#include <stdio.h>
#include <memory.h>
#include <string.h>

int main()
{
    //const WCHAR* drive = L"G:\\";
    //const WCHAR* file = L"G:\\Dokan\\TEST_FILE.txt";
    const WCHAR* drive = L"O:\\";
    const WCHAR* file = L"O:\\TEST_FILE.txt";

    // file information
    {
        DWORD dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
        DWORD dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        DWORD dwCreationDisposition = CREATE_ALWAYS;
        DWORD dwFlagsAndAttribute = FILE_ATTRIBUTE_NORMAL;
        HANDLE h = CreateFileW(file, dwDesiredAccess, dwShareMode, NULL,
            dwCreationDisposition, dwFlagsAndAttribute, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD lastError = GetLastError();
            printf("CreateFileW ERROR: %d\n", lastError);
            return -1;
        }
        TCHAR name[255];
        if (!GetFinalPathNameByHandle(h, name, 255, FILE_NAME_NORMALIZED)) {
            DWORD lastError = GetLastError();
            printf("GetFinalPathNameByHandle ERROR: %d\n", lastError);
            return -1;
        }
        wprintf(L"%s\n", name);
        CloseHandle(h);
    }

    // buffer mode
    {
        DWORD dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
        DWORD dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        DWORD dwCreationDisposition = CREATE_ALWAYS;
        DWORD dwFlagsAndAttribute = FILE_ATTRIBUTE_NORMAL;
        HANDLE h = CreateFileW(file, dwDesiredAccess, dwShareMode, NULL,
            dwCreationDisposition, dwFlagsAndAttribute, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD lastError = GetLastError();
            printf("CreateFileW ERROR: %d\n", lastError);
            return -1;
        }
        LARGE_INTEGER distanceToMove;
        const char* writeData = "ABCDEFG";
        const DWORD size = sizeof(writeData);
        char buff[size];
        DWORD readLen;

        distanceToMove.QuadPart = 0;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }

        if (!ReadFile(h, buff, size, &readLen, NULL)) {
            DWORD lastError = GetLastError();
            printf("ReadFile ERROR: %d\n", lastError);
            return -1;
        }
        printf("ReadLen: %d\n", readLen);

        distanceToMove.QuadPart = 100;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }

        if (!ReadFile(h, buff, size, &readLen, NULL)) {
            DWORD lastError = GetLastError();
            printf("ReadFile ERROR: %d\n", lastError);
            return -1;
        }
        printf("ReadLen: %d\n", readLen);

        if (!WriteFile(h, writeData, size, &readLen, NULL)) {
            DWORD lastError = GetLastError();
            printf("WriteFile ERROR: %d\n", lastError);
            return -1;
        }

        distanceToMove.QuadPart = 0;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }

        if (!ReadFile(h, buff, size, &readLen, NULL)) {
            DWORD lastError = GetLastError();
            printf("ReadFile ERROR: %d\n", lastError);
            return -1;
        }
        buff[size - 1] = 0;
        printf("data: %d, %s\n", readLen, buff);

        CloseHandle(h);
    }

    // file append
    {
        DWORD dwDesiredAccess = FILE_APPEND_DATA;
        DWORD dwShareMode = FILE_SHARE_WRITE;
        DWORD dwCreationDisposition = CREATE_ALWAYS;
        DWORD dwFlagsAndAttribute = FILE_ATTRIBUTE_NORMAL;
        HANDLE h = CreateFileW(file, dwDesiredAccess, dwShareMode, NULL,
            dwCreationDisposition, dwFlagsAndAttribute, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD lastError = GetLastError();
            printf("CreateFileW ERROR: %d\n", lastError);
            return -1;
        }

        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = 0;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }

        const char* writeData = "ABCDEFG";
        const DWORD size = sizeof(writeData);
        DWORD readLen;
        if (!WriteFile(h, writeData, size, &readLen, NULL)) {
            DWORD lastError = GetLastError();
            printf("WriteFile ERROR: %d\n", lastError);
            return -1;
        }

        CloseHandle(h);
    }

    // no buffering mode
    {
        DWORD dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
        DWORD dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        DWORD dwCreationDisposition = CREATE_ALWAYS;
        DWORD dwFlagsAndAttribute = FILE_FLAG_NO_BUFFERING;
        HANDLE h = CreateFileW(file, dwDesiredAccess, dwShareMode, NULL,
            dwCreationDisposition, dwFlagsAndAttribute, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD lastError = GetLastError();
            printf("CreateFileW ERROR: %d\n", lastError);
            return -1;
        }

        DWORD SecBytes;
        if (!GetDiskFreeSpaceW(drive, NULL, &SecBytes, NULL, NULL)) {
            DWORD lastError = GetLastError();
            printf("GetDiskFreeSpaceW ERROR: %d\n", lastError);
            return -1;
        }
        printf("secter size: %d\n", SecBytes);

        char* buff = (char*)malloc(SecBytes);
        memset(buff, 'A', SecBytes);
        DWORD readLen;
        if (!WriteFile(h, buff, SecBytes, &readLen, NULL)) {
            DWORD lastError = GetLastError();
            printf("WriteFile ERROR: %d\n", lastError);
            return -1;
        }

        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = 0;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }

        if (!ReadFile(h, buff, SecBytes, &readLen, NULL)) {
            DWORD lastError = GetLastError();
            printf("ReadFile ERROR: %d\n", lastError);
            return -1;
        }
        buff[SecBytes - 1] = 0;
        printf("data: %d, %s\n", readLen, buff);

        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }

        if (!ReadFile(h, buff, SecBytes - 1, &readLen, NULL)) {
            DWORD lastError = GetLastError();
            printf("ReadFile error (normal): %d\n", lastError);
        }
        free(buff);

        CloseHandle(h);
    }

    // expand shrink
    {
        DWORD dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
        DWORD dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        DWORD dwCreationDisposition = CREATE_ALWAYS;
        DWORD dwFlagsAndAttribute = FILE_ATTRIBUTE_NORMAL;
        HANDLE h = CreateFileW(file, dwDesiredAccess, dwShareMode, NULL,
            dwCreationDisposition, dwFlagsAndAttribute, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD lastError = GetLastError();
            printf("CreateFileW ERROR: %d\n", lastError);
            return -1;
        }

        LARGE_INTEGER distanceToMove;

        // expand
        distanceToMove.QuadPart = 3686L;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }
        if (!SetEndOfFile(h)) {
            DWORD lastError = GetLastError();
            printf("SetEndOfFile ERROR: %d\n", lastError);
            return -1;
        }

        // expand
        distanceToMove.QuadPart = 5529L;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }
        if (!SetEndOfFile(h)) {
            DWORD lastError = GetLastError();
            printf("SetEndOfFile ERROR: %d\n", lastError);
            return -1;
        }

        // write
        distanceToMove.QuadPart = 3686L;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }
        {
            DWORD size = 4;
            char* buff = (char*)malloc(size);
            memset(buff, 'A', size);
            DWORD readLen;
            if (!WriteFile(h, buff, size, &readLen, NULL)) {
                DWORD lastError = GetLastError();
                printf("WriteFile ERROR: %d\n", lastError);
                return -1;
            }
            free(buff);
        }

        // same size
        distanceToMove.QuadPart = 5529L;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }
        if (!SetEndOfFile(h)) {
            DWORD lastError = GetLastError();
            printf("SetEndOfFile ERROR: %d\n", lastError);
            return -1;
        }

        // shrink
        distanceToMove.QuadPart = 4505L;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }
        if (!SetEndOfFile(h)) {
            DWORD lastError = GetLastError();
            printf("SetEndOfFile ERROR: %d\n", lastError);
            return -1;
        }

        // write
        distanceToMove.QuadPart = 3686L;
        if (!SetFilePointerEx(h, distanceToMove, NULL, FILE_BEGIN)) {
            DWORD lastError = GetLastError();
            printf("SetFilePointerEx ERROR: %d\n", lastError);
            return -1;
        }
        {
            DWORD size = 108;
            char* buff = (char*)malloc(size);
            memset(buff, 'A', size);
            DWORD readLen;
            if (!WriteFile(h, buff, size, &readLen, NULL)) {
                DWORD lastError = GetLastError();
                printf("WriteFile ERROR: %d\n", lastError);
                return -1;
            }
            free(buff);
        }


        CloseHandle(h);
    }
}
