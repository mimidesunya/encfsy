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
#include <codecvt>

using namespace std;

void ShowUsage() {
	// clang-format off
	fprintf(stderr, "encfs.exe [options] rootdir mountPoint \n"
		"  rootdir (ex. c:\\test)\t\t\t Directory source to EncFS.\n"
		"  mountPoint (ex. m)\t\t\t Mount point. Can be M:\\ (drive letter) or empty NTFS folder C:\\mount\\dokan .\n\n"
		"Options:\n"
		"  -u mountPoint \t\t\t Unmount.\n"
		"  -l \t\t\t\t\t List mount pounts.\n"
		"  -v \t\t\t\t\t Enable debug output to an attached debugger.\n"
		"  -i Timeout (Milliseconds ex. 30000)\t Timeout until a running operation is aborted and the device is unmounted. Default to 30000.\n"
		"  -t ThreadCount (ex. 5)\t\t Number of threads to be used internally by Dokan library.\n\t\t\t\t\t More threads will handle more event at the same time. Default to 5.\n"
		"  --dokan-network UNC (ex. \\host\\myfs)\t UNC name used for network volume.\n"
		"  --dokan-removable \t\t\t Show device as removable media.\n"
		"  --dokan-write-protect \t\t Read only filesystem.\n"
		"  --dokan-mount-manager \t\t Register device to Windows mount manager.\n\t\t\t\t\t This enables advanced Windows features like recycle bin and more...\n"
		"  --dokan-current-session \t\t Device only visible for current user session.\n"
		"  --dokan-filelock-user-mode \t\t Enable Lockfile/Unlockfile operations. Otherwise Dokan will take care of it.\n"
		"  --public \t\t\t\t Impersonate Caller User when getting the handle in CreateFile for operations.\n\t\t\t\t\t This option requires administrator right to work properly.\n"
		"  --allocation-unit-size Bytes (ex. 512) Allocation Unit Size of the volume. This will behave on the disk file size.\n"
		"  --sector-size Bytes (ex. 512)\t\t Sector Size of the volume. This will behave on the disk file size.\n\n"
		"Examples:\n"
		"\tencfs.exe C:\\Users M:\t\t\t\t\t # EncFS C:\\Users as RootDirectory into a drive of letter M:\\.\n"
		"\tencfs.exe C:\\Users C:\\mount\\dokan \t\t\t # EncFS C:\\Users as RootDirectory into NTFS folder C:\\mount\\dokan.\n"
		"\tencfs.exe C:\\Users M: --dokan-network \\myfs\\myfs1 \t # EncFS C:\\Users as RootDirectory into a network drive M:\\. with UNC \\\\myfs\\myfs1\n\n"
		"Unmount the drive with CTRL + C in the console or alternatively via \"encfs.exe -u MountPoint\".\n");
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

	while (a < size - 1)
	{
		if (!ReadConsole(hIn, &ch, 1, &dwRead, NULL)) {
			ch = getc(stdin);
		}
		if (ch == RETURN || ch == '\n') {
			break;
		}
		else if (ch == BACKSPACE)
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

	bool unmount = false, list = false;
	EncFSOptions efo;
	ZeroMemory(&efo, sizeof(EncFSOptions));
	efo.Timeout = 30000;
	efo.ThreadCount = 5;

	WCHAR ConfigFile[DOKAN_MAX_PATH];

	for (command = 1; command < argc; command++) {
		if (argv[command][0] == L'-') {
			// options
			switch (towlower(argv[command][1])) {
			case L'u':
				command++;
				wcscpy_s(efo.MountPoint, sizeof(efo.MountPoint) / sizeof(WCHAR), argv[command]);
				unmount = true;
				break;
			case L'l':
				list = true;
				break;
			case L'v':
				efo.g_DebugMode = TRUE;
				break;
			case L's':
				efo.g_UseStdErr = TRUE;
				break;
			case L'i':
				command++;
				efo.Timeout = (ULONG)_wtol(argv[command]);
				break;
			case L't':
				command++;
				efo.ThreadCount = (USHORT)_wtoi(argv[command]);
				break;
			case L'-':
				if (wcscmp(argv[command], L"--dokan-network")) {
					command++;
					wcscpy_s(efo.UNCName, sizeof(efo.UNCName) / sizeof(WCHAR), argv[command]);
					efo.DokanOptions |= DOKAN_OPTION_NETWORK;
				}
				else if (wcscmp(argv[command], L"--dokan-removable")) {
					efo.DokanOptions |= DOKAN_OPTION_REMOVABLE;
				}
				else if (wcscmp(argv[command], L"--dokan-write-protect")) {
					efo.DokanOptions |= DOKAN_OPTION_WRITE_PROTECT;
				}
				else if (wcscmp(argv[command], L"--dokan-mount-manager")) {
					efo.DokanOptions |= DOKAN_OPTION_MOUNT_MANAGER;
				}
				else if (wcscmp(argv[command], L"--dokan-current-session")) {
					efo.DokanOptions |= DOKAN_OPTION_CURRENT_SESSION;
				}
				else if (wcscmp(argv[command], L"--dokan-filelock-user-mode")) {
					efo.DokanOptions |= DOKAN_OPTION_FILELOCK_USER_MODE;
				}
				else if (wcscmp(argv[command], L"--reverse")) {
					efo.Reverse = TRUE;
				}
				else if (wcscmp(argv[command], L"--public")) {
					efo.g_ImpersonateCallerUser = TRUE;
				}
				else if (wcscmp(argv[command], L"--allocation-unit-size")) {
					command++;
					efo.AllocationUnitSize = (ULONG)_wtol(argv[command]);
				}
				else if (wcscmp(argv[command], L"--sector-size")) {
					command++;
					efo.SectorSize = (ULONG)_wtol(argv[command]);
				}
				break;
			default:
				fwprintf(stderr, L"unknown command: %s\n", argv[command]);
				return EXIT_FAILURE;
			}
		}
		else {
			// path
			if (efo.RootDirectory[0] == L'\0') {
				wcscpy_s(efo.RootDirectory, sizeof(efo.RootDirectory) / sizeof(WCHAR), argv[command]);
			}
			else {
				wcscpy_s(efo.MountPoint, sizeof(efo.MountPoint) / sizeof(WCHAR), argv[command]);
			}
		}
	}

	if (list) {
		// List drives.
		ULONG nbRead = 0;
		DOKAN_CONTROL dokanControl[DOKAN_MAX_INSTANCES];
		if (!DokanGetMountPointList(dokanControl, DOKAN_MAX_INSTANCES, FALSE,
			&nbRead)) {
			return -1;
		}

		wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
		for (int i = 0; i < nbRead; ++i) {
			string cMountPoint = strConv.to_bytes(wstring(dokanControl[i].MountPoint));
			printf("%s\n", cMountPoint.c_str());
		}
	}
	else if (unmount) {
		// Unmount drive.
		if (argc < 3) {
			ShowUsage();
			return EXIT_FAILURE;
		}
		return DokanRemoveMountPoint(efo.MountPoint);
	}
	else {
		// Mount drive.
		if (argc < 3) {
			ShowUsage();
			return EXIT_FAILURE;
		}
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
