#include "EncFSy.h"
#include "CredentialManager.h"
#include "Messages.h"
#include "ScanInvalid.h"

#include <malloc.h>
#include <iostream>
#include <windows.h>
#include <tchar.h>
#include <codecvt>
#include <io.h>
#include <fcntl.h>
#include "EncFSy_globals.h"

using namespace std;

/**
 * @brief Displays command-line usage information
 * 
 * Shows all available options, arguments, and example usage for the EncFSy console application.
 * Language is determined by Windows system settings.
 */
void ShowUsage() {
    fprintf(stderr, "%s", EncFSMessages::GetUsageTextWithVersion());
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
    // Initialize version from Version.txt
    EncFSMessages::InitVersion();
    
    // Initialize language settings based on system locale
    EncFSMessages::InitLanguage();
    
    // Set console output to UTF-8 for proper display of localized messages
    SetConsoleOutputCP(CP_UTF8);
    
    ULONG command;

    // Operation flags
    bool unmount = false, list = false;
    bool useCredential = false;       // Use Windows Credential Manager (keep password)
    bool useCredentialOnce = false;   // Use Windows Credential Manager (delete after use)
    bool scanInvalid = false;         // Scan for undecodable filenames only

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
                if (wcscmp(argv[command], L"--lang") == 0) {
                    // Override language setting for testing
                    command++;
                    if (command < argc) {
                        if (_wcsicmp(argv[command], L"ja") == 0 || _wcsicmp(argv[command], L"jp") == 0) {
                            EncFSMessages::g_CurrentLanguage = EncFSMessages::Language::Japanese;
                        }
                        else if (_wcsicmp(argv[command], L"ko") == 0 || _wcsicmp(argv[command], L"kr") == 0) {
                            EncFSMessages::g_CurrentLanguage = EncFSMessages::Language::Korean;
                        }
                        else if (_wcsicmp(argv[command], L"zh") == 0 || _wcsicmp(argv[command], L"zh-cn") == 0) {
                            EncFSMessages::g_CurrentLanguage = EncFSMessages::Language::ChineseSimplified;
                        }
                        else if (_wcsicmp(argv[command], L"zh-tw") == 0 || _wcsicmp(argv[command], L"zh-hk") == 0) {
                            EncFSMessages::g_CurrentLanguage = EncFSMessages::Language::ChineseTraditional;
                        }
                        else if (_wcsicmp(argv[command], L"ru") == 0) {
                            EncFSMessages::g_CurrentLanguage = EncFSMessages::Language::Russian;
                        }
                        else if (_wcsicmp(argv[command], L"ar") == 0) {
                            EncFSMessages::g_CurrentLanguage = EncFSMessages::Language::Arabic;
                        }
                        else if (_wcsicmp(argv[command], L"de") == 0) {
                            EncFSMessages::g_CurrentLanguage = EncFSMessages::Language::German;
                        }
                        else if (_wcsicmp(argv[command], L"en") == 0) {
                            EncFSMessages::g_CurrentLanguage = EncFSMessages::Language::English;
                        }
                    }
                }
                else if (wcscmp(argv[command], L"--use-credential") == 0) {
                    // Use Windows Credential Manager for password (keep it stored)
                    useCredential = true;
                }
                else if (wcscmp(argv[command], L"--use-credential-once") == 0) {
                    // Use Windows Credential Manager for password (delete after use)
                    useCredentialOnce = true;
                }
                else if (wcscmp(argv[command], L"--scan-invalid") == 0) {
                    // Scan encrypted tree and report filenames that fail to decrypt
                    scanInvalid = true;
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
                fwprintf(stderr, L"%hs", EncFSMessages::MSG_UNKNOWN_COMMAND());
                fwprintf(stderr, L"%s\n", argv[command]);
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
        if (efo.MountPoint[0] == L'\0') {
            ShowUsage();
            return EXIT_FAILURE;
        }
        return DokanRemoveMountPoint(efo.MountPoint);
    }
    else {
        // Mount a new encrypted volume or run scan
        // Check if required arguments (rootDir and mountPoint) are provided
        if (efo.RootDirectory[0] == L'\0' || (!scanInvalid && efo.MountPoint[0] == L'\0')) {
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
            printf("%s", EncFSMessages::MSG_CONFIG_NOT_EXIST());
            getpass(EncFSMessages::MSG_ENTER_NEW_PASSWORD(), password, sizeof password);
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
                fprintf(stderr, "%s", EncFSMessages::MSG_CREDENTIAL_NOT_FOUND());
                return EXIT_FAILURE;
            }
        } else {
            // Prompt for password from console/stdin
            getpass(EncFSMessages::MSG_ENTER_PASSWORD(), password, sizeof password);
            passwordObtained = true;
        }

        if (passwordObtained) {
            if (scanInvalid) {
                int scanResult = RunScanInvalid(efo, password);
                SecureZeroMemory(password, sizeof(password));
                return scanResult;
            }

            // Start the EncFS filesystem
            result = StartEncFS(efo, password);
            
            // Securely clear password from memory after use
            SecureZeroMemory(password, sizeof(password));
        }
        
        return result;
    }
}
