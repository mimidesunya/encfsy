#include "EncFSy_path.h"
#include "EncFSy_globals.h"
#include "EncFSy_logging.h"
#include <dokan.h>
#include "EncFSUtils.hpp"

/**
 * @brief Internal implementation: Converts UTF-8 encoded filename to Windows path with NT prefix
 * @param strConv UTF-8/UTF-16 string converter
 * @param cEncodedFileName UTF-8 encoded filename
 * @param encodedFilePath Output buffer for wide-character path
 * 
 * Constructs: \\?\<RootDirectory>\<filename>
 * Handles UNC path prefixes by stripping the UNC name if present.
 */
static void ToWFilePathImpl(std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>& strConv,
	std::string& cEncodedFileName, PWCHAR encodedFilePath) {
	std::wstring wFilePath = strConv.from_bytes(cEncodedFileName);

	auto filePath = std::make_unique<WCHAR[]>(DOKAN_MAX_PATH);
	wcscpy_s(filePath.get(), DOKAN_MAX_PATH, wFilePath.c_str());

	// Use NT path prefix "\\?\\" to support long paths (>260 characters)
	std::wstring resultPath = L"\\\\?\\";
	resultPath += g_efo.RootDirectory;
	
	// Handle UNC path: strip UNC name prefix if present
	size_t unclen = wcslen(g_efo.UNCName);
	if (unclen > 0 && _wcsnicmp(filePath.get(), g_efo.UNCName, unclen) == 0) {
		if (_wcsnicmp(filePath.get() + unclen, L".", 1) != 0) {
			resultPath += (filePath.get() + unclen);
		}
	}
	else {
		resultPath += filePath.get();
	}
	wcsncpy_s(encodedFilePath, DOKAN_MAX_PATH, resultPath.c_str(), _TRUNCATE);
}

void ToWFilePath(std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>& strConv,
	std::string& cEncodedFileName, PWCHAR encodedFilePath) {
	ToWFilePathImpl(strConv, cEncodedFileName, encodedFilePath);
}

bool FileExists(PWCHAR path) {
	if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
		return true;
	}
	DWORD err = GetLastError();
	// Distinguish "not found" from other errors (e.g., access denied)
	if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
		return false;
	}
	// For other errors, assume file exists (conservative approach)
	return true;
}

/**
 * @brief Converts virtual (plaintext) path to physical (encrypted) path
 * @param encodedFilePath Output buffer for physical path
 * @param plainFilePath Virtual plaintext file path
 * @param createNew True if creating a new file (skips case-insensitive lookup)
 * 
 * In reverse mode: Decrypts filenames (plaintext on disk, encrypted view)
 * In normal mode: Encrypts filenames and performs case-insensitive lookup if enabled
 */
void GetFilePath(PWCHAR encodedFilePath, LPCWSTR plainFilePath, bool createNew) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> strConv;
	std::wstring wFilePath(plainFilePath);
	std::string cFilePath = strConv.to_bytes(wFilePath);

	std::string cEncodedFileName;
	if (encfs.isReverse()) {
		// Reverse mode: Decrypt filename (if decryption fails, use as-is)
		try {
			encfs.decodeFilePath(cFilePath, cEncodedFileName);
		}
		catch (const EncFS::EncFSInvalidBlockException&) {
			cEncodedFileName = cFilePath;
		}
		ToWFilePathImpl(strConv, cEncodedFileName, encodedFilePath);
	}
	else {
		// Normal mode: Encrypt filename
		encfs.encodeFilePath(cFilePath, cEncodedFileName);
		ToWFilePathImpl(strConv, cEncodedFileName, encodedFilePath);

		// Perform case-insensitive lookup if enabled and file doesn't exist
		if (g_efo.CaseInsensitive && !FileExists(encodedFilePath)) {
			WCHAR filePath[DOKAN_MAX_PATH];
			bool pathChanged = false;
			std::string::size_type pos1 = 1;  // Skip leading separator
			std::string::size_type pos2;
			std::string currentPlainPath;
			std::string currentEncPath;
			std::string component;
			std::wstring wsComponent;
			WIN32_FIND_DATAW find;
			
			// Walk through each path component
			for (;;) {
				pos2 = cFilePath.find(EncFS::g_pathSeparator, pos1);
				
				// Extract current path component
				if (pos2 == std::string::npos) {
					component.assign(cFilePath, pos1, std::string::npos);
				} else {
					component.assign(cFilePath, pos1, pos2 - pos1);
				}

				// Construct the path up to the current component
				currentPlainPath.assign(cFilePath, 0, (pos2 == std::string::npos) ? cFilePath.length() : pos2);

				if (pos2 == std::string::npos && createNew) {
					break; // Don't lookup if creating a new file, just use the constructed path
				}
				
				currentEncPath.clear();
				encfs.encodeFilePath(currentPlainPath, currentEncPath);
				ToWFilePathImpl(strConv, currentEncPath, filePath);
				
				if (!FileExists(filePath)) {
					// Component not found: search parent directory for case-insensitive match
					bool found = false;
					
					// Get parent path
					std::string::size_type parentPos = currentEncPath.find_last_of(EncFS::g_pathSeparator);
					if (parentPos == std::string::npos) {
						break; // No parent, cannot search
					}
					std::string parentEncPath = currentEncPath.substr(0, parentPos);
					std::string searchPath = parentEncPath + EncFS::g_pathSeparator + "*.*";
					
					ToWFilePathImpl(strConv, searchPath, filePath);
					
					ZeroMemory(&find, sizeof(WIN32_FIND_DATAW));
					HANDLE hFind = FindFirstFileW(filePath, &find);
					if (hFind == INVALID_HANDLE_VALUE) {
						break;
					}
					
					wsComponent = strConv.from_bytes(component);

					// Search for matching filename (case-insensitive)
					do {
						if (wcscmp(find.cFileName, L"..") == 0 ||
							wcscmp(find.cFileName, L".") == 0) {
							continue;
						}
						std::wstring wcFileName(find.cFileName);
						std::string ccFileName = strConv.to_bytes(wcFileName);
						std::string cPlainFileName;
						try {
							encfs.decodeFileName(ccFileName, parentEncPath, cPlainFileName);
						}
						catch (const EncFS::EncFSInvalidBlockException&) {
							continue;  // Skip files with invalid encryption
						}
						std::wstring wFileName = strConv.from_bytes(cPlainFileName);
						
						// Case-insensitive comparison
						if (lstrcmpiW(wsComponent.c_str(), wFileName.c_str()) == 0) {
							// Found match: update path with correct case
							cFilePath.replace(pos1, component.length(), cPlainFileName);
							pathChanged = true;
							found = true;
							break;
						}
					} while (FindNextFileW(hFind, &find) != 0);
					FindClose(hFind);
					
					if (!found) {
						pathChanged = false;
						break;
					}
				}
				
				if (pos2 == std::string::npos) {
					break;  // Processed all components
				}
				pos1 = pos2 + 1;
			}
			
			// Re-encode path if case was corrected
			if (pathChanged) {
				cEncodedFileName.clear();
				encfs.encodeFilePath(cFilePath, cEncodedFileName);
				ToWFilePathImpl(strConv, cEncodedFileName, encodedFilePath);
			}
		}
	}
}
