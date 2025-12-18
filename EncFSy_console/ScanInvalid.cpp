#include "ScanInvalid.h"
#include "EncFSy_globals.h"

#include <windows.h>
#include <codecvt>
#include <fstream>
#include <vector>

// Helper: convert UTF-16 to UTF-8 (returns false on failure)
static bool WideToUtf8(const std::wstring& wstr, std::string& utf8) {
    if (wstr.empty()) { utf8.clear(); return true; }
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (sizeNeeded <= 0) return false;
    utf8.resize(static_cast<size_t>(sizeNeeded) - 1);
    return WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], sizeNeeded, NULL, NULL) > 0;
}

// Load EncFS config (without mounting) and unlock volume
static bool LoadVolumeConfig(const EncFSOptions& efo, char* password) {
    encfs.altStream = efo.AltStream;
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

// Recursive scan for filenames that cannot be decrypted
static void ScanInvalidNames(const std::wstring& physicalDir, const std::string& plainDirPath, std::vector<std::wstring>& invalidPaths) {
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

        std::wstring encNameW(findData.cFileName);
        std::string encNameUtf8;
        if (!WideToUtf8(encNameW, encNameUtf8)) {
            invalidPaths.push_back(physicalDir + L"\\" + encNameW);
            continue;
        }

        std::string plainName;
        try {
            encfs.decodeFileName(encNameUtf8, plainDirPath, plainName);
        }
        catch (...) {
            invalidPaths.push_back(physicalDir + L"\\" + encNameW);
            continue;
        }

        bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir) {
            std::wstring childPhysical = physicalDir + L"\\" + encNameW;
            std::string childPlainPath = plainDirPath;
            if (!childPlainPath.empty()) childPlainPath += "\\";
            childPlainPath += plainName;
            ScanInvalidNames(childPhysical, childPlainPath, invalidPaths);
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
}

int RunScanInvalid(const EncFSOptions& efo, char* password) {
    if (!LoadVolumeConfig(efo, password)) {
        return EXIT_FAILURE;
    }

    std::vector<std::wstring> invalidPaths;
    ScanInvalidNames(efo.RootDirectory, std::string(), invalidPaths);

    if (invalidPaths.empty()) {
        printf("No invalid filenames detected.\n");
    } else {
        printf("Invalid filenames (decryption failed):\n");
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        for (const auto& p : invalidPaths) {
            std::string utf8 = conv.to_bytes(p);
            printf("%s\n", utf8.c_str());
        }
        printf("Total: %zu\n", invalidPaths.size());
    }
    return EXIT_SUCCESS;
}
