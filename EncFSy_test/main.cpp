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
    const WCHAR* drive = L"M:\\";
    const WCHAR* file = L"M:\\TEST_FILE.txt";

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

        const char* writeData = "ABCDEFG";
        const DWORD size = sizeof(writeData);
        DWORD readLen;
        if (!WriteFile(h, writeData, size, &readLen, NULL)) {
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

        char buff[size];
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
            printf("ReadFile error: %d\n", lastError);
        }

        CloseHandle(h);
    }
}
