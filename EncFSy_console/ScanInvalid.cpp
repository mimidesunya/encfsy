#include "ScanInvalid.h"
#include "EncFSy_globals.h"

#include <windows.h>
#include <codecvt>
#include <fstream>
#include <vector>
#include <sstream>

// Helper: convert UTF-16 to UTF-8 (returns false on failure)
static bool WideToUtf8(const std::wstring& wstr, std::string& utf8) {
    if (wstr.empty()) { utf8.clear(); return true; }
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (sizeNeeded <= 0) return false;
    utf8.resize(static_cast<size_t>(sizeNeeded) - 1);
    return WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], sizeNeeded, NULL, NULL) > 0;
}

// Helper: escape JSON string
static std::string EscapeJson(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", static_cast<unsigned char>(c));
                    oss << buf;
                } else {
                    oss << c;
                }
                break;
        }
    }
    return oss.str();
}

// Structure to hold invalid file information
struct InvalidFileInfo {
    std::string fileName;           // Encoded filename (that failed to decode)
    std::string encodedParentPath;  // Encoded parent directory path (relative to root)
    std::string decodedParentPath;  // Decoded parent directory path
};

// Streaming JSON output context
struct JsonStreamContext {
    size_t count;           // Number of items output so far
    bool headerPrinted;     // Whether the JSON header has been printed
    
    JsonStreamContext() : count(0), headerPrinted(false) {}
    
    // Print JSON header (called before first item)
    void printHeader() {
        if (!headerPrinted) {
            printf("{\n");
            printf("  \"invalidFiles\": [\n");
            fflush(stdout);
            headerPrinted = true;
        }
    }
    
    // Print a single invalid file entry (streaming)
    void printEntry(const InvalidFileInfo& info) {
        printHeader();
        
        // Print comma before this entry if not the first
        if (count > 0) {
            printf(",\n");
        }
        
        printf("    {\n");
        printf("      \"fileName\": \"%s\",\n", EscapeJson(info.fileName).c_str());
        printf("      \"encodedParentPath\": \"%s\",\n", EscapeJson(info.encodedParentPath).c_str());
        printf("      \"decodedParentPath\": \"%s\"\n", EscapeJson(info.decodedParentPath).c_str());
        printf("    }");
        fflush(stdout);
        
        count++;
    }
    
    // Print JSON footer (called after scanning completes)
    void printFooter() {
        if (headerPrinted) {
            printf("\n");  // Newline after last entry
            printf("  ],\n");
            printf("  \"totalCount\": %zu\n", count);
            printf("}\n");
        } else {
            // No invalid files found - print complete JSON
            printf("{\n");
            printf("  \"invalidFiles\": [],\n");
            printf("  \"totalCount\": 0\n");
            printf("}\n");
        }
        fflush(stdout);
    }
};

// Load EncFS config (without mounting) and unlock volume
static bool LoadVolumeConfig(const EncFSOptions& efo, char* password) {
    encfs.altStream = efo.AltStream;
    encfs.cloudConflict = efo.CloudConflict;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> strConv;
    const std::wstring wRootDir(efo.RootDirectory);
    std::string cRootDir = strConv.to_bytes(wRootDir);
    std::string configFile = cRootDir + "\\.encfs6.xml";

    try {
        std::ifstream in(configFile, std::ios::binary);
        if (!in.is_open()) {
            fprintf(stderr, "Failed to open config: %s\n", configFile.c_str());
            return false;
        }
        std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        encfs.load(xml, efo.Reverse);
        encfs.unlock(password);
        return true;
    }
    catch (const EncFS::EncFSBadConfigurationException& ex) {
        fprintf(stderr, "Config error: %s\n", ex.what());
    }
    catch (const EncFS::EncFSUnlockFailedException& ex) {
        fprintf(stderr, "Unlock failed: %s\n", ex.what());
    }
    catch (const std::exception& ex) {
        fprintf(stderr, "Error: %s\n", ex.what());
    }
    return false;
}

// Recursive scan for filenames that cannot be decrypted (streaming output)
static void ScanInvalidNames(
    const std::wstring& physicalDir,
    const std::string& encodedDirPath,  // Relative encoded path from root (e.g., "encDir1\\encDir2")
    const std::string& plainDirPath,    // Decoded path (e.g., "dir1\\dir2")
    JsonStreamContext& jsonCtx
) {
    WIN32_FIND_DATAW findData;
    std::wstring search = physicalDir;
    if (!search.empty() && search.back() != L'\\') {
        search.push_back(L'\\');
    }
    search += L"*";

    HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }

        // Skip .encfs6.xml config file
        if (_wcsicmp(findData.cFileName, L".encfs6.xml") == 0) {
            continue;
        }

        std::wstring encNameW(findData.cFileName);
        std::string encNameUtf8;
        std::wstring physicalPath = physicalDir;
        if (!physicalPath.empty() && physicalPath.back() != L'\\') {
            physicalPath.push_back(L'\\');
        }
        physicalPath += encNameW;

        if (!WideToUtf8(encNameW, encNameUtf8)) {
            InvalidFileInfo info;
            info.fileName = "<UTF-8 conversion failed>";
            info.encodedParentPath = encodedDirPath;
            info.decodedParentPath = plainDirPath;
            jsonCtx.printEntry(info);
            continue;
        }

        std::string plainName;
        try {
            encfs.decodeFileName(encNameUtf8, plainDirPath, plainName);
        }
        catch (...) {
            InvalidFileInfo info;
            info.fileName = encNameUtf8;
            info.encodedParentPath = encodedDirPath;
            info.decodedParentPath = plainDirPath;
            jsonCtx.printEntry(info);
            continue;
        }

        bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir) {
            std::wstring childPhysical = physicalPath;
            
            // Build encoded path
            std::string childEncodedPath = encodedDirPath;
            if (!childEncodedPath.empty()) {
                childEncodedPath += "\\";
            }
            childEncodedPath += encNameUtf8;
            
            // Build decoded path
            std::string childPlainPath = plainDirPath;
            if (!childPlainPath.empty()) {
                childPlainPath += "\\";
            }
            childPlainPath += plainName;
            
            ScanInvalidNames(childPhysical, childEncodedPath, childPlainPath, jsonCtx);
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
}

int RunScanInvalid(const EncFSOptions& efo, char* password) {
    if (!LoadVolumeConfig(efo, password)) {
        // Output error as JSON
        printf("{\n");
        printf("  \"error\": \"Failed to load or unlock EncFS volume\",\n");
        printf("  \"invalidFiles\": [],\n");
        printf("  \"totalCount\": 0\n");
        printf("}\n");
        fflush(stdout);
        return EXIT_FAILURE;
    }

    JsonStreamContext jsonCtx;
    ScanInvalidNames(efo.RootDirectory, std::string(), std::string(), jsonCtx);
    jsonCtx.printFooter();

    return EXIT_SUCCESS;
}
