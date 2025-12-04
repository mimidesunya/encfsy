#pragma once
#include <windows.h>
#include "EncFSy.h"

/**
 * @brief Checks if an EncFS configuration file exists in the specified directory
 * @param rootDir Root directory path to check for .encfs6.xml configuration file
 * @return True if the configuration file exists and can be opened, false otherwise
 * 
 * This function verifies the presence of the EncFS configuration file (.encfs6.xml)
 * in the specified root directory. It does not validate the file contents.
 */
bool IsEncFSExists(LPCWSTR rootDir);

/**
 * @brief Creates a new EncFS volume with the specified configuration
 * @param rootDir Root directory path where the encrypted volume will be created
 * @param password User password for encrypting the volume key (will be zeroed after use)
 * @param mode Security mode (STANDARD or PARANOIA)
 * @param reverse Enable reverse encryption mode (encrypt on-the-fly for read-only access)
 * @return EXIT_SUCCESS (0) on success, EXIT_FAILURE on error
 * 
 * This function:
 * - Generates a new EncFS volume configuration
 * - Derives an encryption key from the provided password using PBKDF2
 * - Creates a random volume key and encrypts it with the password-derived key
 * - Saves the configuration to .encfs6.xml in the root directory
 * 
 * Security modes:
 * - STANDARD: 192-bit key, no IV chaining
 * - PARANOIA: 256-bit key, full IV chaining for maximum security
 * 
 * Note: Returns EXIT_FAILURE if the configuration file already exists.
 */
int CreateEncFS(LPCWSTR rootDir, char* password, EncFSMode mode, bool reverse);

/**
 * @brief Starts the EncFS filesystem and mounts it via Dokan
 * @param efo EncFS options structure containing mount point, root directory, and Dokan options
 * @param password User password for unlocking the volume (will be zeroed after use)
 * @return EXIT_SUCCESS (0) on successful mount and unmount, EXIT_FAILURE on error
 * 
 * This function:
 * 1. Loads the EncFS configuration from .encfs6.xml in the root directory
 * 2. Unlocks the volume by deriving the encryption key from the password
 * 3. Configures Dokan options based on the EncFS settings
 * 4. Registers all Dokan filesystem operation callbacks
 * 5. Calls DokanMain() to start the filesystem (blocks until unmount)
 * 6. Returns status code after unmount
 * 
 * The function handles:
 * - Configuration validation and loading
 * - Password verification and key derivation
 * - Dokan driver initialization
 * - Console Ctrl+C handler registration
 * - Security privilege management (SE_SECURITY_NAME)
 * 
 * Common return codes:
 * - DOKAN_SUCCESS: Successfully mounted and unmounted
 * - DOKAN_ERROR: Generic error
 * - DOKAN_DRIVE_LETTER_ERROR: Invalid drive letter
 * - DOKAN_MOUNT_ERROR: Failed to assign drive letter
 * - DOKAN_VERSION_ERROR: Dokan version mismatch
 */
int StartEncFS(EncFSOptions& efo, char* password);

/**
 * @brief Console control handler for graceful shutdown on Ctrl+C, close, etc.
 * @param dwCtrlType Type of control signal received
 * @return TRUE if the signal was handled, FALSE otherwise
 * 
 * Handles the following console events:
 * - CTRL_C_EVENT: Ctrl+C pressed
 * - CTRL_BREAK_EVENT: Ctrl+Break pressed
 * - CTRL_CLOSE_EVENT: Console window closed
 * - CTRL_LOGOFF_EVENT: User logging off
 * - CTRL_SHUTDOWN_EVENT: System shutting down
 * 
 * When triggered, this handler:
 * 1. Unregisters itself to prevent recursion
 * 2. Calls DokanRemoveMountPoint() to gracefully unmount the filesystem
 * 3. Returns TRUE to indicate the event was handled
 * 
 * This ensures proper cleanup and prevents data loss when the application
 * is terminated via console signals.
 */
BOOL WINAPI CtrlHandler(DWORD dwCtrlType);
