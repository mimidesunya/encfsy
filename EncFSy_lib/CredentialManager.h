/**
 * @file CredentialManager.h
 * @brief Windows Credential Manager wrapper for secure password storage
 * 
 * This provides secure password storage using Windows Credential Manager (DPAPI).
 * Passwords are encrypted and tied to the current user account.
 */

#pragma once

#include <windows.h>
#include <wincred.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

namespace EncFS {

    /**
     * @brief Windows Credential Manager wrapper class
     * 
     * Provides secure password storage and retrieval using the Windows
     * Credential Manager. Passwords are protected by DPAPI and only
     * accessible to the current user.
     */
    class CredentialManager {
    public:
        static constexpr const wchar_t* CREDENTIAL_PREFIX = L"EncFSy:";

        /**
         * @brief Retrieves a stored password from Credential Manager
         * @param rootDirectory The encrypted directory path (used as identifier)
         * @param password Output buffer for the password (will be null-terminated)
         * @param passwordSize Size of the password buffer in bytes
         * @return true if password was retrieved, false if not found or error
         * 
         * The caller is responsible for calling SecureZeroMemory on the
         * password buffer after use.
         */
        static bool GetPassword(const wchar_t* rootDirectory, char* password, size_t passwordSize) {
            if (!rootDirectory || !password || passwordSize == 0) {
                return false;
            }

            // Initialize output
            password[0] = '\0';

            // Build target name
            std::wstring targetName = CREDENTIAL_PREFIX;
            targetName += NormalizePath(rootDirectory);

            // Read credential
            PCREDENTIALW credential = nullptr;
            if (!CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
                return false;
            }

            bool success = false;

            if (credential->CredentialBlobSize > 0 && credential->CredentialBlob != nullptr) {
                // Credential is stored as Unicode, convert to UTF-8/ANSI for our use
                size_t charCount = credential->CredentialBlobSize / sizeof(wchar_t);
                
                // Convert wide string to multibyte
                int requiredSize = WideCharToMultiByte(
                    CP_UTF8, 0,
                    reinterpret_cast<wchar_t*>(credential->CredentialBlob),
                    static_cast<int>(charCount),
                    nullptr, 0, nullptr, nullptr
                );

                if (requiredSize > 0 && static_cast<size_t>(requiredSize) < passwordSize) {
                    WideCharToMultiByte(
                        CP_UTF8, 0,
                        reinterpret_cast<wchar_t*>(credential->CredentialBlob),
                        static_cast<int>(charCount),
                        password, static_cast<int>(passwordSize),
                        nullptr, nullptr
                    );
                    password[requiredSize] = '\0';
                    success = true;
                }
            }

            // Free credential (Windows will zero the memory)
            CredFree(credential);

            return success;
        }

        /**
         * @brief Saves a password to Credential Manager
         * @param rootDirectory The encrypted directory path (used as identifier)
         * @param password The password to store (null-terminated)
         * @return true if saved successfully
         */
        static bool SavePassword(const wchar_t* rootDirectory, const char* password) {
            if (!rootDirectory || !password) {
                return false;
            }

            // Build target name
            std::wstring targetName = CREDENTIAL_PREFIX;
            targetName += NormalizePath(rootDirectory);

            // Convert password to wide string for storage
            int wideLen = MultiByteToWideChar(CP_UTF8, 0, password, -1, nullptr, 0);
            if (wideLen <= 0) {
                return false;
            }

            std::wstring widePassword(wideLen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, password, -1, &widePassword[0], wideLen);

            // Prepare credential
            CREDENTIALW credential = {};
            credential.Type = CRED_TYPE_GENERIC;
            credential.TargetName = const_cast<wchar_t*>(targetName.c_str());
            credential.CredentialBlobSize = static_cast<DWORD>((wideLen - 1) * sizeof(wchar_t)); // Exclude null terminator
            credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(widePassword.c_str()));
            credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
            credential.UserName = const_cast<wchar_t*>(L"EncFSy");

            bool result = CredWriteW(&credential, 0) != FALSE;

            // Securely clear the wide password
            SecureZeroMemory(&widePassword[0], widePassword.size() * sizeof(wchar_t));

            return result;
        }

        /**
         * @brief Deletes a stored password from Credential Manager
         * @param rootDirectory The encrypted directory path
         * @return true if deleted or not found
         */
        static bool DeletePassword(const wchar_t* rootDirectory) {
            if (!rootDirectory) {
                return false;
            }

            std::wstring targetName = CREDENTIAL_PREFIX;
            targetName += NormalizePath(rootDirectory);

            // CredDelete returns FALSE if not found, which is acceptable
            CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0);
            return true;
        }

        /**
         * @brief Checks if a password is stored for the given directory
         * @param rootDirectory The encrypted directory path
         * @return true if a password is stored
         */
        static bool HasStoredPassword(const wchar_t* rootDirectory) {
            if (!rootDirectory) {
                return false;
            }

            std::wstring targetName = CREDENTIAL_PREFIX;
            targetName += NormalizePath(rootDirectory);

            PCREDENTIALW credential = nullptr;
            if (CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
                CredFree(credential);
                return true;
            }
            return false;
        }

    private:
        /**
         * @brief Normalizes a path for use as credential identifier
         * @param path The path to normalize
         * @return Normalized lowercase path without trailing backslash
         */
        static std::wstring NormalizePath(const wchar_t* path) {
            std::wstring normalized(path);
            
            // Remove trailing backslash
            while (!normalized.empty() && normalized.back() == L'\\') {
                normalized.pop_back();
            }

            // Convert to lowercase for consistent matching
            for (auto& c : normalized) {
                c = towlower(c);
            }

            return normalized;
        }
    };

} // namespace EncFS
