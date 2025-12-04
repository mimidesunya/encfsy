#pragma once
#include <windows.h>
#include <string>
#include <codecvt>

/**
 * @brief Checks if a file or directory exists at the specified path
 * @param path Wide-character path to check
 * @return True if the file/directory exists, false otherwise
 */
bool FileExists(PWCHAR path);

/**
 * @brief Converts virtual (plaintext) path to physical (encrypted) path
 * @param encodedFilePath Output buffer for the physical path with NT prefix (\\?\)
 * @param plainFilePath Virtual plaintext file path
 * @param createNew True if creating a new file (affects case-insensitive lookup)
 * 
 * Performs filename encryption and case-insensitive lookup if enabled.
 * In reverse mode, decrypts filenames instead of encrypting them.
 */
void GetFilePath(PWCHAR encodedFilePath, LPCWSTR plainFilePath, bool createNew);

/**
 * @brief Converts UTF-8 encoded filename to Windows path with NT prefix
 * @param strConv UTF-8/UTF-16 string converter
 * @param cEncodedFileName UTF-8 encoded filename
 * @param encodedFilePath Output buffer for wide-character path with \\?\ prefix
 * 
 * Constructs full physical path: \\?\<RootDirectory>\<filename>
 * Handles UNC path prefixes if configured.
 */
void ToWFilePath(std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>& strConv,
                 std::string& cEncodedFileName, PWCHAR encodedFilePath);
