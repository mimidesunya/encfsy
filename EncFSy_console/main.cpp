/*
Dokan : user - mode file system library for Windows
	Copyright(C) 2015 - 2018 Adrien J. <liryna.stark@gmail.com> and Maxime C. <maxime@islog.com>
	Copyright(C) 2007 - 2011 Hiroki Asakawa <info@dokan-dev.net>
	http ://dokan-dev.github.io
	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files(the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/ 

#include "EncFSy.h"

#include <malloc.h>
#include <iostream>
#include <windows.h>
using namespace std;

void ShowUsage() {
	// clang-format off
	fprintf(stderr, "EncFSy.exe\n"
		"  /r RootDirectory (ex. /r c:\\test)\t\t Directory source to EncFS.\n"
		"  /l MountPoint (ex. /l m)\t\t\t Mount point. Can be M:\\ (drive letter) or empty NTFS folder C:\\mount\\dokan .\n"
		"  /e Unmount.\n"
		"  /d (enable debug output)\t\t\t Enable debug output to an attached debugger.\n"
		"  /s (use stderr for output)\t\t\t Enable debug output to stderr.\n"
		"  /n (use network drive)\t\t\t Show device as network device.\n"
		"  /m (use removable drive)\t\t\t Show device as removable media.\n"
		"  /w (write-protect drive)\t\t\t Read only filesystem.\n"
		"  /o (use mount manager)\t\t\t Register device to Windows mount manager.\n\t\t\t\t\t\t This enables advanced Windows features like recycle bin and more...\n"
		"  /c (mount for current session only)\t\t Device only visible for current user session.\n"
		"  /u (UNC provider name ex. \\localhost\\myfs)\t UNC name used for network volume.\n"
		"  /p (Impersonate Caller User)\t\t\t Impersonate Caller User when getting the handle in CreateFile for operations.\n\t\t\t\t\t\t This option requires administrator right to work properly.\n"
		"  /a Allocation unit size (ex. /a 512)\t\t Allocation Unit Size of the volume. This will behave on the disk file size.\n"
		"  /k Sector size (ex. /k 512)\t\t\t Sector Size of the volume. This will behave on the disk file size.\n"
		"  /f User mode Lock\t\t\t\t Enable Lockfile/Unlockfile operations. Otherwise Dokan will take care of it.\n"
		"  /i (Timeout in Milliseconds ex. /i 30000)\t Timeout until a running operation is aborted and the device is unmounted.\n\n"
		"Examples:\n"
		"\tEncFSy.exe /r C:\\Users /l M:\t\t\t# EncFS C:\\Users as RootDirectory into a drive of letter M:\\.\n"
		"\tEncFSy.exe /r C:\\Users /l C:\\mount\\dokan\t# EncFS C:\\Users as RootDirectory into NTFS folder C:\\mount\\dokan.\n"
		"\tEncFSy.exe /r C:\\Users /l M: /n /u \\myfs\\myfs1\t# EncFS C:\\Users as RootDirectory into a network drive M:\\. with UNC \\\\myfs\\myfs1\n\n"
		"Unmount the drive with CTRL + C in the console or alternatively via \"EncFSy.exe /v MountPoint\".\n");
	// clang-format on
}

void getpass(const char *prompt, char* password, int size)
{
	const char BACKSPACE = 8;
	const char RETURN = 13;

	unsigned char ch = 0;
	int a = 0;

	cout << prompt;

	DWORD con_mode;
	DWORD dwRead;

	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

	GetConsoleMode(hIn, &con_mode);
	SetConsoleMode(hIn, con_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

	while (a < size - 1 && ReadConsoleA(hIn, &ch, 1, &dwRead, NULL) && ch != RETURN)
	{
		if (ch == BACKSPACE)
		{
			if (a > 0)
			{
				--a;
			}
		}
		else
		{
			password[a++] = ch;
		}
	}
	cout << endl;
	password[a++] = '\0';
}


// .\WinEncFS.exe /r G:\EncFSTest /l i /t 5
int __cdecl wmain(ULONG argc, PWCHAR argv[]) {
	int status;
	ULONG command;

	if (argc < 3) {
		ShowUsage();
		return EXIT_FAILURE;
	}

	bool unmount = false;
	EncFSOptions efo;
	ZeroMemory(&efo, sizeof(EncFSOptions));
	efo.Timeout = 30000;

	//WCHAR RootDirectory[DOKAN_MAX_PATH];
	//WCHAR MountPoint[DOKAN_MAX_PATH];
	WCHAR ConfigFile[DOKAN_MAX_PATH];

	for (command = 1; command < argc; command++) {
		switch (towlower(argv[command][1])) {
		case L'r':
			command++;
			wcscpy_s(efo.RootDirectory, sizeof(efo.RootDirectory) / sizeof(WCHAR), argv[command]);
			//efo.RootDirectory = RootDirectory;
			break;
		case L'l':
			command++;
			wcscpy_s(efo.MountPoint, sizeof(efo.MountPoint) / sizeof(WCHAR), argv[command]);
			//efo.MountPoint = MountPoint;
			break;
		case L'g':
			command++;
			wcscpy_s(ConfigFile, sizeof(ConfigFile) / sizeof(WCHAR), argv[command]);
			efo.ConfigFile = ConfigFile;
			break;
		case L'e':
			unmount = true;
			break;
		case L't':
			command++;
			efo.ThreadCount = (USHORT)_wtoi(argv[command]);
			break;
		case L'd':
			efo.g_DebugMode = TRUE;
			break;
		case L's':
			efo.g_UseStdErr = TRUE;
			break;
		case L'n':
			efo.DokanOptions |= DOKAN_OPTION_NETWORK;
			break;
		case L'm':
			efo.DokanOptions |= DOKAN_OPTION_REMOVABLE;
			break;
		case L'w':
			efo.DokanOptions |= DOKAN_OPTION_WRITE_PROTECT;
			break;
		case L'o':
			efo.DokanOptions |= DOKAN_OPTION_MOUNT_MANAGER;
			break;
		case L'c':
			efo.DokanOptions |= DOKAN_OPTION_CURRENT_SESSION;
			break;
		case L'f':
			efo.DokanOptions |= DOKAN_OPTION_FILELOCK_USER_MODE;
			break;
		case L'v':
			efo.Reverse = TRUE;
			break;
		case L'u':
			command++;
			wcscpy_s(efo.UNCName, sizeof(efo.UNCName) / sizeof(WCHAR), argv[command]);
			//efo.UNCName = UNCName;
			break;
		case L'p':
			efo.g_ImpersonateCallerUser = TRUE;
			break;
		case L'i':
			command++;
			efo.Timeout = (ULONG)_wtol(argv[command]);
			break;
		case L'a':
			command++;
			efo.AllocationUnitSize = (ULONG)_wtol(argv[command]);
			break;
		case L'k':
			command++;
			efo.SectorSize = (ULONG)_wtol(argv[command]);
			break;
		default:
			fwprintf(stderr, L"unknown command: %s\n", argv[command]);
			return EXIT_FAILURE;
		}
	}

	if (unmount) {
		return DokanRemoveMountPoint(efo.MountPoint);
	}
	else {
		char password[100];
		if (!IsEncFSExists(efo.RootDirectory)) {
			printf("EncFS configuration file doesn't exist.\n");
			getpass("Enter new password: ", password, sizeof password);
			CreateEncFS(efo.RootDirectory, password, false);

		}
		getpass("Enter password: ", password, sizeof password);

		return StartEncFS(efo, password);
	}
}
