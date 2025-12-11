#include "EncFSy.h"
#include "CredentialManager.h"

#include <malloc.h>
#include <iostream>
#include <windows.h>
#include <tchar.h>
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
		"  mountPoint   (e.g., M: or C:\\mount\\dokan)   Mount location - either a drive letter\n"
		"                                               such as M:\\ or an empty NTFS folder.\n"
		"\n"
		"Options:\n"
		"  -u <mountPoint>                              Unmount the specified volume.\n"
		"  -l                                           List currently mounted Dokan volumes.\n"
		"  -v                                           Send debug output to an attached debugger.\n"
		"  -s                                           Send debug output to stderr.\n"
		"  -i <ms>              (default: 120000)       Timeout (in milliseconds) before a running\n"
		"                                               operation is aborted and the volume unmounted.\n"
		"  --use-credential                             Read password from Windows Credential Manager\n"
		"                                               instead of prompting. Password is kept stored.\n"
		"  --use-credential-once                        Read password from Windows Credential Manager\n"
		"                                               and delete it after reading (one-time use).\n"
		"  --dokan-debug                                Enable Dokan debug output.\n"
		"  --dokan-network <UNC>                        UNC path for a network volume (e.g., \\\\host\\myfs).\n"
		"  --dokan-removable                            Present the volume as removable media.\n"
		"  --dokan-write-protect                        Mount the filesystem read-only.\n"
		"  --dokan-mount-manager                        Register the volume with the Windows Mount Manager\n"
		"                                               (enables Recycle Bin support, etc.).\n"
		"  --dokan-current-session                      Make the volume visible only in the current session.\n"
		"  --dokan-filelock-user-mode                   Handle LockFile/UnlockFile in user mode; otherwise\n"
		"                                               Dokan manages them automatically.\n"
		"  --dokan-enable-unmount-network-drive         Allow unmounting network drive via Explorer.\n"
		"  --dokan-dispatch-driver-logs                 Forward kernel driver logs to userland (slow).\n"
		"  --dokan-allow-ipc-batching                   Enable IPC batching for slow filesystems\n"
		"                                               (e.g., remote storage).\n"
		"  --public                                     Impersonate the calling user when opening handles\n"
		"                                               in CreateFile. Requires administrator privileges.\n"
		"  --allocation-unit-size <bytes>               Allocation-unit size reported by the volume.\n"
		"  --sector-size <bytes>                        Sector size reported by the volume.\n"
		"  --volume-name <name>                         Volume name shown in Explorer (default: EncFS).\n"
		"  --volume-serial <hex>                        Volume serial number in hex (default: from underlying).\n"
		"  --paranoia                                   Enable AES-256 encryption, renamed IVs, and external\n"
		"                                               IV chaining.\n"
		"  --alt-stream                                 Enable NTFS alternate data streams.\n"
		"  --case-insensitive                           Perform case-insensitive filename matching.\n"
		"  --reverse                                    Reverse mode: show plaintext rootDir as encrypted\n"
		"                                               at mountPoint.\n"
		"\n"
		"Examples:\n"
		"  encfs.exe C:\\Users M:                                    # Mount C:\\Users as drive M:\\\n"
		"  encfs.exe C:\\Users C:\\mount\\dokan                       # Mount C:\\Users at NTFS folder C:\\mount\\dokan\n"
		"  encfs.exe C:\\Users M: --dokan-network \\\\myfs\\share       # Mount as network drive with UNC \\\\myfs\\share\n"
		"  encfs.exe C:\\Data M: --volume-name \"My Secure Drive\"     # Mount with custom volume name\n"
		"  encfs.exe C:\\Data M: --use-credential                    # Use stored password (keep it stored)\n"
		"  encfs.exe C:\\Data M: --use-credential-once               # Use stored password (delete after use)\n"
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
 */
int __cdecl wmain(ULONG argc, PWCHAR argv[]) {
	ULONG command;

	// Operation flags
	bool unmount = false, list = false;
	bool useCredential = false;       // Use Windows Credential Manager (keep password)
	bool useCredentialOnce = false;   // Use Windows Credential Manager (delete after use)
	
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
			case L'-':
				// Process long options (starting with --)
				if (wcscmp(argv[command], L"--use-credential") == 0) {
					// Use Windows Credential Manager for password (keep it stored)
					useCredential = true;
				}
				else if (wcscmp(argv[command], L"--use-credential-once") == 0) {
					// Use Windows Credential Manager for password (delete after use)
					useCredentialOnce = true;
				}
				else if (wcscmp(argv[command], L"--dokan-debug") == 0) {
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
				else if (wcscmp(argv[command], L"--dokan-enable-unmount-network-drive") == 0) {
					// Allow unmounting network drive via Explorer
					efo.DokanOptions |= DOKAN_OPTION_ENABLE_UNMOUNT_NETWORK_DRIVE;
				}
				else if (wcscmp(argv[command], L"--dokan-dispatch-driver-logs") == 0) {
					// Forward kernel driver logs to userland (slow)
					efo.DokanOptions |= DOKAN_OPTION_DISPATCH_DRIVER_LOGS;
				}
				else if (wcscmp(argv[command], L"--dokan-allow-ipc-batching") == 0) {
					// Enable IPC batching for slow filesystems
					efo.DokanOptions |= DOKAN_OPTION_ALLOW_IPC_BATCHING;
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
				else if (wcscmp(argv[command], L"--volume-name") == 0) {
					// Set custom volume name
					command++;
					wcscpy_s(efo.VolumeName, sizeof(efo.VolumeName) / sizeof(WCHAR), argv[command]);
				}
				else if (wcscmp(argv[command], L"--volume-serial") == 0) {
					// Set custom volume serial number
					command++;
					efo.VolumeSerial = _tcstoul(argv[command], NULL, 16);
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

		char password[256];  // Increased buffer size for credential manager
		int result = EXIT_SUCCESS;
		bool passwordObtained = false;
		
		// Check if EncFS configuration exists
		if (!IsEncFSExists(efo.RootDirectory)) {
			// Configuration doesn't exist - create new volume
			// Note: Cannot use credential manager for new volume creation
			printf("EncFS configuration file doesn't exist.\n");
			getpass("Enter new password: ", password, sizeof password);
			CreateEncFS(efo.RootDirectory, password, mode, efo.Reverse);
			// Password is cleared by CreateEncFS/deriveKey, but ensure it's cleared
			SecureZeroMemory(password, sizeof(password));
		}
		
		// Get password for unlocking the volume
		if (useCredential || useCredentialOnce) {
			// Try to get password from Windows Credential Manager
			if (EncFS::CredentialManager::GetPassword(efo.RootDirectory, password, sizeof(password))) {
				passwordObtained = true;
				
				// Delete credential after reading if --use-credential-once was specified
				if (useCredentialOnce) {
					EncFS::CredentialManager::DeletePassword(efo.RootDirectory);
				}
			} else {
				fprintf(stderr, "Error: No stored password found in Credential Manager for this volume.\n");
				fprintf(stderr, "Please use the GUI to save a password first, or run without --use-credential.\n");
				return EXIT_FAILURE;
			}
		} else {
			// Prompt for password from console/stdin
			getpass("Enter password: ", password, sizeof password);
			passwordObtained = true;
		}

		if (passwordObtained) {
			// Start the EncFS filesystem
			result = StartEncFS(efo, password);
			
			// Securely clear password from memory after use
			SecureZeroMemory(password, sizeof(password));
		}
		
		return result;
	}
}
