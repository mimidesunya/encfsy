#include "EncFSy_path.h"
#include "EncFSy_globals.h"
#include "EncFSy_logging.h"
#include <dokan.h>
#include "EncFSUtils.hpp"
#include <memory>

// Forward declaration for file exists callback
static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>* g_strConvPtr = nullptr;

/**
 * @brief Callback function for checking if encoded file exists
 * @param encodedPath UTF-8 encoded path to check
 * @return true if file exists, false otherwise
 * 
 * Used by encodeFilePath for conflict suffix handling.
 */
static bool EncodedFileExistsCallback(const std::string& encodedPath) {
	if (g_strConvPtr == nullptr) {
		return false;
	}
	
	std::wstring wPath = g_strConvPtr->from_bytes(encodedPath);
	std::wstring fullPath = L"\\\\?\\";
	fullPath += g_efo.RootDirectory;
	fullPath += wPath;
	
	DWORD attrs = GetFileAttributesW(fullPath.c_str());
	if (attrs != INVALID_FILE_ATTRIBUTES) {
		return true;
	}
	DWORD err = GetLastError();
	return (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND);
}

/**
 * @brief Detect whether a wide path is already absolute (drive-rooted or UNC/NT prefix)
 * @param p The path to check
 * @return true if the path is absolute, false if relative
 * 
 * Absolute paths include those with:
 * - NT prefix (\\?\)
 * - UNC prefix (//server/share)
 * - Drive letter root (C:\ or C:/)
 */
static bool IsAbsoluteWindowsPath(const std::wstring& p) {
	if (p.empty()) return false;
	// NT path prefix
	if (p.rfind(L"\\\\?\\", 0) == 0) return true;
	// UNC path
	if (p.rfind(L"\\\\", 0) == 0) return true;
	// Drive rooted: "C:\\..."
	return (p.size() >= 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/'));
}

/**
 * @brief Internal implementation: Converts UTF-8 encoded filename to Windows path with NT prefix
 * @param strConv UTF-8/UTF-16 string converter
 * @param cEncodedFileName UTF-8 encoded filename
 * @param encodedFilePath Output buffer for wide-character path
 * 
 * Constructs: \\?\<RootDirectory><filename>
 * Handles UNC path prefixes by stripping the UNC name if present.
 */
static void ToWFilePathImpl(std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>& strConv,
	std::string& cEncodedFileName, PWCHAR encodedFilePath) {
	std::wstring wFilePath = strConv.from_bytes(cEncodedFileName);
	
	// If the encoded path is already absolute (shouldn't normally happen, but can in case-insensitive
	// re-encoding paths), do not prepend \\?\ + RootDirectory again.
	if (IsAbsoluteWindowsPath(wFilePath)) {
		wcsncpy_s(encodedFilePath, DOKAN_MAX_PATH, wFilePath.c_str(), _TRUNCATE);
		return;
	}
	
	auto filePath = std::make_unique<WCHAR[]>(DOKAN_MAX_PATH);
	wcscpy_s(filePath.get(), DOKAN_MAX_PATH, wFilePath.c_str());

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
 * @brief Case-insensitive string comparison using Unicode normalization
 * @param str1 First string
 * @param str2 Second string
 * @return true if strings are equal (case-insensitive), false otherwise
 * 
 * Uses CompareStringOrdinal for consistent, locale-independent comparison.
 * This is more reliable than lstrcmpiW which can behave differently based on locale.
 */
static bool CaseInsensitiveEqual(const std::wstring& str1, const std::wstring& str2) {
	if (str1.length() != str2.length()) {
		return false;
	}
	// CompareStringOrdinal is locale-independent and more predictable
	int result = CompareStringOrdinal(
		str1.c_str(), static_cast<int>(str1.length()),
		str2.c_str(), static_cast<int>(str2.length()),
		TRUE  // bIgnoreCase
	);
	return (result == CSTR_EQUAL);
}

/**
 * @brief RAII wrapper for FindFirstFile handle
 */
class FindHandle {
private:
	HANDLE handle_;
public:
	explicit FindHandle(HANDLE h) : handle_(h) {}
	~FindHandle() {
		if (handle_ != INVALID_HANDLE_VALUE) {
			FindClose(handle_);
		}
	}
	FindHandle(const FindHandle&) = delete;
	FindHandle& operator=(const FindHandle&) = delete;
	
	HANDLE get() const { return handle_; }
	bool isValid() const { return handle_ != INVALID_HANDLE_VALUE; }
};

/**
 * @brief Check if a plaintext path exists on disk (case-insensitive)
 * @param plainFilePath Virtual plaintext file path (e.g., "\\dir\\Name.txt")
 * @return true if a file/dir with same plaintext name exists ignoring case
 */
bool PlainPathExistsCaseInsensitive(LPCWSTR plainFilePath) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> strConv;
	std::wstring wPlain(plainFilePath);
	std::string cPlain = strConv.to_bytes(wPlain);

	// Split into parent and leaf
	std::string::size_type pos = cPlain.find_last_of(EncFS::g_pathSeparator);
	if (pos == std::string::npos || pos == 0) {
		return false; // root-only, treat as not existing
	}
	std::string parentPlain = cPlain.substr(0, pos);
	std::string leafPlain = cPlain.substr(pos + 1);
	if (leafPlain.empty()) return false;

	// Encode parent to physical and enumerate children
	std::string parentEnc;
	encfs.encodeFilePath(parentPlain, parentEnc);
	// Trim trailing separator to avoid "\\*" becoming "\\\\*"
	if (!parentEnc.empty()) {
		char last = parentEnc.back();
		if (last == '\\' || last == '/') {
			parentEnc.pop_back();
		}
	}
	std::string search = parentEnc + EncFS::g_pathSeparator + "*";
	WCHAR searchPath[DOKAN_MAX_PATH];
	ToWFilePathImpl(strConv, search, searchPath);

	WIN32_FIND_DATAW find;
	ZeroMemory(&find, sizeof(find));
	HANDLE hFind = FindFirstFileW(searchPath, &find);
	FindHandle guard(hFind);
	if (!guard.isValid()) {
		return false;
	}

	std::wstring wLeaf = strConv.from_bytes(leafPlain);
	do {
		if (wcscmp(find.cFileName, L".") == 0 || wcscmp(find.cFileName, L"..") == 0) continue;
		std::wstring wEncName(find.cFileName);
		std::string cEncName = strConv.to_bytes(wEncName);
		std::string cPlainChild;
		try {
			encfs.decodeFileName(cEncName, parentEnc, cPlainChild);
		}
		catch (const EncFS::EncFSInvalidBlockException&) {
			continue;
		}
		std::wstring wPlainChild = strConv.from_bytes(cPlainChild);
		if (CaseInsensitiveEqual(wLeaf, wPlainChild)) {
			return true;
		}
	} while (FindNextFileW(hFind, &find) != 0);
	return false;
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
	std::string cFilePath;
	
	// Convert with error checking
	try {
		cFilePath = strConv.to_bytes(wFilePath);
		// Check for conversion failure (returns empty on some errors)
		if (wFilePath.length() > 0 && cFilePath.empty()) {
			throw std::runtime_error("UTF-16 to UTF-8 conversion failed");
		}
	}
	catch (const std::exception& ex) {
		ErrorPrint(L"GetFilePath: UTF-16->UTF-8 conversion failed for '%s': %S\n", plainFilePath, ex.what());
		throw;
	}

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
		// Only use conflict suffix callback when NOT creating a new file
		// For new files, we want normal encryption without conflict handling
		if (createNew) {
			encfs.encodeFilePath(cFilePath, cEncodedFileName);
		} else {
			g_strConvPtr = &strConv;
			encfs.encodeFilePath(cFilePath, cEncodedFileName, EncodedFileExistsCallback);
			g_strConvPtr = nullptr;
		}
		ToWFilePathImpl(strConv, cEncodedFileName, encodedFilePath);

		// Perform case-insensitive lookup if enabled
		if (g_efo.CaseInsensitive) {
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

				// Skip empty components
				if (component.empty()) {
					if (pos2 == std::string::npos) {
						break;
					}
					pos1 = pos2 + 1;
					continue;
				}

				// Construct the path up to the current component
				currentPlainPath.assign(cFilePath, 0, (pos2 == std::string::npos) ? cFilePath.length() : pos2);

				// Don't lookup if creating a new file at the leaf level
				if (pos2 == std::string::npos && createNew) {
					break;
				}
				
				currentEncPath.clear();
				// Don't use conflict callback in case-insensitive lookup - just encode normally
				encfs.encodeFilePath(currentPlainPath, currentEncPath);
				ToWFilePathImpl(strConv, currentEncPath, filePath);
				
				// Always search for case-insensitive match when CaseInsensitive is enabled
				bool found = false;
				
				// Get parent path
				std::string::size_type parentPos = currentEncPath.find_last_of(EncFS::g_pathSeparator);
				if (parentPos == std::string::npos) {
					// Root-level component
					if (pos2 == std::string::npos) {
						break;
					}
					pos1 = pos2 + 1;
					continue;
				}
				
				std::string parentEncPath = currentEncPath.substr(0, parentPos);
				std::string searchPath = parentEncPath + EncFS::g_pathSeparator + "*";
				
				ToWFilePathImpl(strConv, searchPath, filePath);
				
				ZeroMemory(&find, sizeof(WIN32_FIND_DATAW));
				HANDLE hFind = FindFirstFileW(filePath, &find);
				
				// Use RAII to ensure FindClose is always called
				FindHandle findGuard(hFind);
				
				if (!findGuard.isValid()) {
					// Parent directory doesn't exist or access denied
					pathChanged = false;
					break;
				}
				
				try {
					wsComponent = strConv.from_bytes(component);
					// Check for conversion failure
					if (component.length() > 0 && wsComponent.empty()) {
						throw std::runtime_error("UTF-8 to UTF-16 conversion failed for component");
					}
				}
				catch (const std::exception& ex) {
					ErrorPrint(L"GetFilePath: Component conversion failed: %S\n", ex.what());
					throw;
				}

				// Search for matching filename (case-insensitive)
				do {
					// Skip . and ..
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
						// Skip files with invalid encryption/encoding
						continue;
					}
					
					std::wstring wFileName = strConv.from_bytes(cPlainFileName);
					
					// Case-insensitive comparison using CompareStringOrdinal
					if (CaseInsensitiveEqual(wsComponent, wFileName)) {
						// Found match: update path with correct case from disk
						cFilePath.replace(pos1, component.length(), cPlainFileName);
						pathChanged = true;
						found = true;
						break;
					}
				} while (FindNextFileW(hFind, &find) != 0);
				
				// findGuard destructor will call FindClose automatically
				
				if (!found) {
					// No case-insensitive match found
					// Keep any prior corrections
					break;
				}
				
				// Move to next path component
				if (pos2 == std::string::npos) {
					break;  // Processed all components
				}
				pos1 = pos2 + 1;
			}
			
			// Re-encode path if case was corrected
			if (pathChanged) {
				cEncodedFileName.clear();
				// For re-encoding after case correction, use conflict callback only for existing files
				if (createNew) {
					encfs.encodeFilePath(cFilePath, cEncodedFileName);
				} else {
					g_strConvPtr = &strConv;
					encfs.encodeFilePath(cFilePath, cEncodedFileName, EncodedFileExistsCallback);
					g_strConvPtr = nullptr;
				}
				ToWFilePathImpl(strConv, cEncodedFileName, encodedFilePath);
				DbgPrint(L"GetFilePath: Case-corrected path for '%s'\n", plainFilePath);
			}
		}
	}
	
	// Final validation: ensure we have a valid path
	size_t finalLen = wcslen(encodedFilePath);
	if (finalLen < 7) {
		ErrorPrint(L"GetFilePath: Generated invalid path for '%s'\n", plainFilePath);
		throw std::runtime_error("Generated path is invalid");
	}
}
