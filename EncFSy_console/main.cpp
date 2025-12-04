#include "EncFSy.h"

#include <malloc.h>
#include <iostream>
#include <windows.h>
#include <codecvt>

using namespace std;

/**
 * @brief Displays command-line usage information
 * 
 * Shows all available options, arguments, and example usage for the EncFSy console application.
 */
void ShowUsage() {
	// clang-format off
	fprintf(stderr,
		"Usage: encfs.exe [options] <rootDir> <mountPoint>\n"
		"\n"
		"Arguments:\n"
		"  rootDir      (e.g., C:\\test)                Directory to be encrypted and mounted.\n"
		"  mountPoint   (e.g., M: or C:\\mount\\dokan)   Mount location — either a drive letter\n"
		"                                               such as M:\\ or an empty NTFS folder.\n"
		"\n"
		"Options:\n"
		"  -u <mountPoint>                              Unmount the specified volume.\n"
		"  -l                                           List currently mounted EncFS volumes.\n"
		"  -v                                           Send debug output to an attached debugger.\n"
		"  -s                                           Send debug output to stderr.\n"
		"  -i <ms>              (default: 120000)       Timeout (in milliseconds) before a running\n"
		"                                               operation is aborted and the volume unmounted.\n"
		"  -t <count>           (default: 5)            Number of worker threads for the Dokan library.\n"
		"  --dokan-debug                                Enable Dokan debug output.\n"
		"  --dokan-network <UNC>                        UNC path for a network volume (e.g., \\\\host\\myfs).\n"
		"  --dokan-removable                            Present the volume as removable media.\n"
		"  --dokan-write-protect                        Mount the filesystem read‑only.\n"
		"  --dokan-mount-manager                        Register the volume with the Windows Mount Manager\n"
		"                                               (enables Recycle Bin support, etc.).\n"
		"  --dokan-current-session                      Make the volume visible only in the current session.\n"
		"  --dokan-filelock-user-mode                   Handle LockFile/UnlockFile in user mode; otherwise\n"
		"                                               Dokan manages them automatically.\n"
		"  --public                                     Impersonate the calling user when opening handles\n"
		"                                               in CreateFile. Requires administrator privileges.\n"
		"  --allocation-unit-size <bytes>               Allocation‑unit size reported by the volume.\n"
		"  --sector-size <bytes>                        Sector size reported by the volume.\n"
		"  --paranoia                                   Enable AES‑256 encryption, renamed IVs, and external\n"
		"                                               IV chaining.\n"
		"  --alt-stream                                 Enable NTFS alternate data streams.\n"
		"  --case-insensitive                           Perform case‑insensitive filename matching.\n"
		"  --reverse                                    Reverse mode: encrypt from <rootDir> to <mountPoint>.\n"
		"\n"
		"Examples:\n"
		"  encfs.exe C:\\Users M:                                    # Mount C:\\Users as drive M:\\\n"
		"  encfs.exe C:\\Users C:\\mount\\dokan                       # Mount C:\\Users at NTFS folder C:\\mount\\dokan\n"
		"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share        # Mount C:\\Users as network drive M:\\ with UNC \\\\myfs\\share\n"
		"\n"
		"To unmount, press Ctrl+C in this console or run:\n"
		"  encfs.exe -u <mountPoint>\n"
	);
	// clang-format on
}

/**
 * @brief Securely reads password from console without echoing characters
 * @param prompt Message to display before password input
 * @param password Output buffer to store the entered password
 * @param size Maximum size of the password buffer
 * 
 * Features:
 * - Disables console echo to hide password input
 * - Supports backspace for corrections
 * - Terminates on Enter/Return key
 * - Ensures null-termination of password string
 */
void getpass(const char *prompt, char* password, int size)
{
	const char BACKSPACE = 8;
	const char RETURN = 13;

	int ch = 0; // Use int to handle multi-byte characters from ReadConsole
	int a = 0;  // Current position in password buffer

	cout << prompt;

	DWORD con_mode;
	DWORD dwRead;

	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

	// Save original console mode and disable echo + line input
	GetConsoleMode(hIn, &con_mode);
	SetConsoleMode(hIn, con_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

	// Read password character by character
	while (a < size - 1)
	{
		// Try ReadConsole first, fallback to getc
		if (!ReadConsole(hIn, &ch, 1, &dwRead, NULL)) {
			ch = getc(stdin);
		}
		
		if (ch == RETURN || ch == '\n') {
			// Enter pressed - finish input
			break;
		}
		else if (ch == BACKSPACE)
		{
			// Backspace - remove last character if possible
			if (a > 0)
			{
				--a;
			}
		}
		else
		{
			// Regular character - add to password
			password[a++] = (char)ch;
		}
	}
	cout << endl;
	password[a] = '\0';  // Null-terminate the password string
}


/**
 * @brief Main entry point for EncFSy console application
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings (wide character)
 * @return EXIT_SUCCESS (0) on success, EXIT_FAILURE or error code on failure
 * 
 * Supports three main operations:
 * 1. Mount an encrypted volume
 * 2. Unmount an existing volume (-u option)
 * 3. List all mounted volumes (-l option)
 */
int __cdecl wmain(ULONG argc, PWCHAR argv[]) {
	ULONG command;

	// Operation flags
	bool unmount = false, list = false;
	
	// Encryption mode (STANDARD or PARANOIA)
	EncFSMode mode = STANDARD;
	
	// EncFS options structure
	EncFSOptions efo;
	ZeroMemory(&efo, sizeof(EncFSOptions));
	
	// Initialize default options
	efo.AltStream = FALSE;
	efo.CaseInsensitive = FALSE;
	efo.Reverse = FALSE;
	efo.Timeout = 120000;        // 2 minutes default timeout
	efo.SingleThread = FALSE;

	// Parse command-line arguments
	for (command = 1; command < argc; command++) {
		if (argv[command][0] == L'-') {
			// Process options (flags starting with -)
			switch (towlower(argv[command][1])) {
			case L'u':
				// Unmount operation
				command++;
				wcscpy_s(efo.MountPoint, sizeof(efo.MountPoint) / sizeof(WCHAR), argv[command]);
				unmount = true;
				break;
			case L'l':
				// List mounted volumes
				list = true;
				break;
			case L'v':
				// Enable debug output to debugger
				efo.g_DebugMode = TRUE;
				break;
			case L's':
				// Enable debug output to stderr
				efo.g_UseStdErr = TRUE;
				break;
			case L'i':
				// Set timeout in milliseconds
				command++;
				efo.Timeout = (ULONG)_wtol(argv[command]);
				break;
			case L't':
				// Enable single-threaded mode
				efo.SingleThread = TRUE;
				break;
			case L'-':
				// Process long options (starting with --)
				if (wcscmp(argv[command], L"--dokan-debug") == 0) {
					// Enable Dokan library debug output
					efo.g_DokanDebug = TRUE;
				}
				else if (wcscmp(argv[command], L"--dokan-network") == 0) {
					// Set up network drive with UNC path
					command++;
					wcscpy_s(efo.UNCName, sizeof(efo.UNCName) / sizeof(WCHAR), argv[command]);
					efo.DokanOptions |= DOKAN_OPTION_NETWORK;
				}
				else if (wcscmp(argv[command], L"--dokan-removable") == 0) {
					// Present volume as removable media
					efo.DokanOptions |= DOKAN_OPTION_REMOVABLE;
				}
				else if (wcscmp(argv[command], L"--dokan-write-protect") == 0) {
					// Mount as read-only
					efo.DokanOptions |= DOKAN_OPTION_WRITE_PROTECT;
				}
				else if (wcscmp(argv[command], L"--dokan-mount-manager") == 0) {
					// Register with Windows Mount Manager
					efo.DokanOptions |= DOKAN_OPTION_MOUNT_MANAGER;
				}
				else if (wcscmp(argv[command], L"--dokan-current-session") == 0) {
					// Make volume visible only in current session
					efo.DokanOptions |= DOKAN_OPTION_CURRENT_SESSION;
				}
				else if (wcscmp(argv[command], L"--dokan-filelock-user-mode") == 0) {
					// Handle file locks in user mode
					efo.DokanOptions |= DOKAN_OPTION_FILELOCK_USER_MODE;
				}
				else if (wcscmp(argv[command], L"--public") == 0) {
					// Impersonate calling user (requires admin)
					efo.g_ImpersonateCallerUser = TRUE;
				}
				else if (wcscmp(argv[command], L"--allocation-unit-size") == 0) {
					// Set allocation unit size
					command++;
					efo.AllocationUnitSize = (ULONG)_wtol(argv[command]);
				}
				else if (wcscmp(argv[command], L"--sector-size") == 0) {
					// Set sector size
					command++;
					efo.SectorSize = (ULONG)_wtol(argv[command]);
				}
				else if (wcscmp(argv[command], L"--paranoia") == 0) {
					// Enable paranoia mode (AES-256, full IV chaining)
					mode = PARANOIA;
				}
				else if (wcscmp(argv[command], L"--alt-stream") == 0) {
					// Enable NTFS alternate data streams
					efo.AltStream = TRUE;
				}
				else if (wcscmp(argv[command], L"--case-insensitive") == 0) {
					// Enable case-insensitive filename matching
					efo.CaseInsensitive = TRUE;
				}
				else if (wcscmp(argv[command], L"--reverse") == 0) {
					// Enable reverse encryption mode
					efo.Reverse = TRUE;
				}
				break;
			default:
				// Unknown option
				fwprintf(stderr, L"unknown command: %s\n", argv[command]);
				return EXIT_FAILURE;
			}
		}
		else {
			// Process positional arguments (rootDir and mountPoint)
			if (efo.RootDirectory[0] == L'\0') {
				// First positional argument is root directory
				wcscpy_s(efo.RootDirectory, sizeof(efo.RootDirectory) / sizeof(WCHAR), argv[command]);
			}
			else {
				// Second positional argument is mount point
				wcscpy_s(efo.MountPoint, sizeof(efo.MountPoint) / sizeof(WCHAR), argv[command]);
			}
		}
	}

	if (list) {
		// List all currently mounted volumes
		ULONG nbRead = 0;
		PDOKAN_MOUNT_POINT_INFO mountPoints = DokanGetMountPointList(FALSE, &nbRead);
		if (!mountPoints) {
			return -1;
		}

		// Convert and display each mount point
		wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
		for (ULONG i = 0; i < nbRead; ++i) {
			string cMountPoint = strConv.to_bytes(wstring(mountPoints[i].MountPoint));
			printf("%s\n", cMountPoint.c_str());
		}
	}
	else if (unmount) {
		// Unmount the specified volume
		if (argc < 3) {
			ShowUsage();
			return EXIT_FAILURE;
		}
		return DokanRemoveMountPoint(efo.MountPoint);
	}
	else {
		// Mount a new encrypted volume
		if (argc < 3) {
			ShowUsage();
			return EXIT_FAILURE;
		}

		char password[100];
		
		// Check if EncFS configuration exists
		if (!IsEncFSExists(efo.RootDirectory)) {
			// Configuration doesn't exist - create new volume
			printf("EncFS configuration file doesn't exist.\n");
			getpass("Enter new password: ", password, sizeof password);
			CreateEncFS(efo.RootDirectory, password, mode, efo.Reverse);
		}
		
		// Get password for unlocking the volume
		getpass("Enter password: ", password, sizeof password);

		// Start the EncFS filesystem
		return StartEncFS(efo, password);
	}
}
