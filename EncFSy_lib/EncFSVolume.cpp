#include "EncFSVolume.h"
#include "EncFSFileCodec.hpp"
#include "EncFSNameCodec.hpp"
#include "EncFSUtils.hpp"
#include "EncFSCloudConflict.hpp"

#include "rapidxml.hpp"

#include <aes.h>
#include <misc.h> // For SecureZeroMemory, VerifyBufsEqual
#include <sstream> // For ostringstream
#include <algorithm> // For std::all_of
#include <vector>
#include <regex>

#if defined(WIN32) || defined(_WIN32)
#include <windows.h> // For SecureZeroMemory
#else
#include <string.h>
#define SecureZeroMemory(p,n) memset((p),0,(n))
#endif

using namespace std;
using namespace rapidxml;
using namespace CryptoPP;

// Thread-local random number generator for cryptographic operations
static thread_local AutoSeededX917RNG<CryptoPP::AES> random;

namespace EncFS {

    namespace {
        // Define constants to avoid magic numbers
        constexpr int IV_SPEC_SIZE = 16;
        constexpr int CHAIN_IV_SIZE = 8;
        constexpr int MAC_16_SIZE = 2;
        constexpr int MAC_32_SIZE = 4;

        // Helper to parse integer from XML, with error checking
        long parseXmlLong(xml_node<>* parent, const char* childName) {
            xml_node<>* node = parent->first_node(childName);
            if (!node || !node->value()) {
                throw EncFSBadConfigurationException(string("Missing XML node: ") + childName);
            }
            char* end;
            long value = strtol(node->value(), &end, 10);
            if (end == node->value() || *end != '\0') { // Check if conversion failed or was partial
                throw EncFSBadConfigurationException(string("Invalid integer value for node: ") + childName);
            }
            return value;
        }

        // Helper to get chain IV
        void getChainIv(HMAC<SHA1>& hmac, std::mutex& hmacLock, bool chained, const string& path, char* chainIv) {
            if (chained) {
                computeChainIv(hmac, hmacLock, path, chainIv);
            } else {
                memset(chainIv, 0, CHAIN_IV_SIZE);
            }
        }
    }

    /**
     * @brief Constructor - initializes Base64 decoder lookup table and members
     */
    EncFSVolume::EncFSVolume() : altStream(false), cloudConflict(false), reverse(false), keySize(0), blockSize(0), uniqueIV(false),
                                 chainedNameIV(false), externalIVChaining(false), blockMACBytes(0),
                                 blockMACRandBytes(0), allowHoles(false), encodedKeySize(0), saltLen(0),
                                 kdfIterations(0), desiredKDFDuration(0)
    {
        Base64Decoder::InitializeDecodingLookupArray(this->base64Lookup, ALPHABET, 64, false);
    };

    /**
     * @brief Loads EncFS configuration from XML string
     * @param xml Configuration XML string (.encfs6.xml format)
     * @param reverse Enable reverse encryption mode
     * @throws EncFSBadConfigurationException if configuration is invalid or parsing fails
     */
    void EncFSVolume::load(const string& xml, bool reverse)
    {
        try {
            // RapidXML requires null-terminated buffer
            std::vector<char> buffer(xml.begin(), xml.end());
            buffer.push_back('\0');

            // Parse XML document with secure flags
            xml_document<> doc;
            doc.parse<parse_no_entity_translation>(buffer.data());

            // Validate root node
            xml_node<>* boost_serialization = doc.first_node("boost_serialization");
            if (!boost_serialization) {
                throw EncFSBadConfigurationException("Missing root node: boost_serialization");
            }
            
            // Get configuration node
            xml_node<>* cfg = boost_serialization->first_node("cfg");
            if (!cfg) {
                throw EncFSBadConfigurationException("Missing node: cfg");
            }

            xml_node<>* nameAlg = cfg->first_node("nameAlg");
            if (!nameAlg) {
                throw EncFSBadConfigurationException("Missing node: nameAlg");
            }

            xml_node<>* nameNode = nameAlg->first_node("name");
            xml_node<>* majorNode = nameAlg->first_node("major");
            xml_node<>* minorNode = nameAlg->first_node("minor");
            if (!nameNode || !nameNode->value() || !majorNode || !minorNode) {
                throw EncFSBadConfigurationException("Invalid node: nameAlg");
            }

            this->nameAlgorithmMajor = static_cast<int32_t>(strtol(majorNode->value(), nullptr, 10));
            this->nameAlgorithmMinor = static_cast<int32_t>(strtol(minorNode->value(), nullptr, 10));
            const string nameAlgorithmName(nameNode->value());
            if (nameAlgorithmName == "nameio/block") {
                this->nameAlgorithm = NameAlgorithm::Block;
            }
            else if (nameAlgorithmName == "nameio/stream") {
                this->nameAlgorithm = NameAlgorithm::Stream;
            }
            else {
                throw EncFSBadConfigurationException(string("Unsupported name algorithm: ") + nameAlgorithmName);
            }
            
            // Parse all integer values with validation
            this->keySize = parseXmlLong(cfg, "keySize");
            this->blockSize = parseXmlLong(cfg, "blockSize");
            this->uniqueIV = parseXmlLong(cfg, "uniqueIV");
            this->chainedNameIV = parseXmlLong(cfg, "chainedNameIV");
            this->externalIVChaining = parseXmlLong(cfg, "externalIVChaining");
            this->blockMACBytes = parseXmlLong(cfg, "blockMACBytes");
            this->blockMACRandBytes = parseXmlLong(cfg, "blockMACRandBytes");
            this->allowHoles = parseXmlLong(cfg, "allowHoles");
            this->encodedKeySize = parseXmlLong(cfg, "encodedKeySize");
            this->saltLen = parseXmlLong(cfg, "saltLen");
            this->kdfIterations = parseXmlLong(cfg, "kdfIterations");
            this->desiredKDFDuration = parseXmlLong(cfg, "desiredKDFDuration");

            // Parse encoded key data (Base64)
            {
                xml_node<>* node = cfg->first_node("encodedKeyData");
                if (!node || !node->value()) {
                    throw EncFSBadConfigurationException("encodedKeyData");
                }
                this->encodedKeyData.assign(node->value());
                trim(this->encodedKeyData);
            }
            
            // Parse salt data (Base64)
            {
                xml_node<>* node = cfg->first_node("saltData");
                if (!node || !node->value()) {
                    throw EncFSBadConfigurationException("saltData");
                }
                this->saltData.assign(node->value());
                trim(this->saltData);
            }

            // Apply reverse mode constraints
            this->reverse = reverse;
            if (this->reverse) {
                // Reverse mode disables certain security features
                this->uniqueIV = false;
                this->chainedNameIV = false;
                this->blockMACBytes = 0;
                this->blockMACRandBytes = 0;
            }
        }
        catch (const parse_error& ex) {
            throw EncFSBadConfigurationException(string("XML parse error: ") + ex.what() + " at " + ex.where<char>());
        }
        catch (const std::exception& ex) {
            // Catch other potential errors like bad_alloc from vector
            throw EncFSBadConfigurationException(string("Failed to load configuration: ") + ex.what());
        }
    }

    /**
     * @brief Creates a new EncFS volume with the specified security mode
     * @param password User password for volume encryption
     * @param mode Security mode (STANDARD or PARANOIA)
     * @param reverse Enable reverse encryption mode
     */
    void EncFSVolume::create(char* password, EncFSMode mode, bool reverse)
    {
        // Initialize basic configuration
        this->blockSize = 1024;
        this->uniqueIV = true;
        this->blockMACBytes = 8;
        this->blockMACRandBytes = 0;
        this->allowHoles = true;
        this->altStream = false;
        this->nameAlgorithm = NameAlgorithm::Block;
        this->nameAlgorithmMajor = 3;
        this->nameAlgorithmMinor = 0;
        
        // Configure security level based on mode
        switch (mode) {
        default:
            // Standard mode: 192-bit key, no IV chaining
            this->keySize = 192;
            this->chainedNameIV = false;
            this->externalIVChaining = false;
            break;
        case PARANOIA:
            // Paranoia mode: 256-bit key, full IV chaining
            this->keySize = 256;
            this->chainedNameIV = true;
            this->externalIVChaining = true;
            break;
        }

        // Generate random salt
        this->saltLen = 20;
        SecByteBlock salt(this->saltLen);
        random.GenerateBlock(salt, salt.size());
        
        // Encode salt as Base64
        {
            this->saltData.clear();
            Base64Encoder encoder(new StringSink(this->saltData), false); // false = no line breaks
            encoder.Put(salt, salt.size());
            encoder.MessageEnd();
            trim(this->saltData);
        }

        // Apply reverse mode constraints
        this->reverse = reverse;
        if (this->reverse) {
            this->uniqueIV = false;
            this->chainedNameIV = false;
            this->blockMACBytes = 0;
            this->blockMACRandBytes = 0;
        }

        // Set KDF parameters
        this->kdfIterations = 170203;
        this->desiredKDFDuration = 500;
        this->encodedKeySize = 44;

        // Generate random volume key
        SecByteBlock plainKey(this->encodedKeySize - MAC_32_SIZE);
        random.GenerateBlock(plainKey, plainKey.size());

        // Derive password-based encryption key
        SecByteBlock pbkdf2Key;
        this->deriveKey(password, pbkdf2Key);

        // Extract encryption key and IV from derived key
        SecByteBlock passKey(pbkdf2Key, this->keySize / 8);
        SecByteBlock passIv(pbkdf2Key.data() + this->keySize / 8, IV_SPEC_SIZE);

        // Generate MAC for integrity check
        HMAC<SHA1> passKeyHmac(passKey, passKey.size());
        byte mac[MAC_32_SIZE];
        mac32(passKeyHmac, this->hmacLock, string((char*)plainKey.data(), plainKey.size()), (char*)mac);
        string ivSeed((char*)mac, MAC_32_SIZE);

        // Encrypt the volume key
        string encryptedKey;
        streamEncrypt(passKeyHmac, this->hmacLock, string((char*)passKey.data(), passKey.size()), string((char*)passIv.data(), passIv.size()), ivSeed,
            this->aesCfbEnc, this->aesCfbEncLock, string((char*)plainKey.data(), plainKey.size()), encryptedKey);
        encryptedKey.insert(0, ivSeed);

        // Encode encrypted key as Base64
        {
            this->encodedKeyData.clear();
            Base64Encoder encoder(new StringSink(this->encodedKeyData), false); // false = no line breaks
            encoder.Put((const byte*)encryptedKey.data(), encryptedKey.size());
            encoder.MessageEnd();
            trim(this->encodedKeyData);
        }
    }

    void EncFSVolume::setNameAlgorithm(NameAlgorithm algorithm)
    {
        this->nameAlgorithm = algorithm;
        if (algorithm == NameAlgorithm::Stream) {
            this->nameAlgorithmMajor = 2;
            this->nameAlgorithmMinor = 1;
            return;
        }

        this->nameAlgorithmMajor = 3;
        this->nameAlgorithmMinor = 0;
    }

    /**
     * @brief Saves volume configuration to XML string
     * @param xml Output parameter receiving XML configuration
     */
    void EncFSVolume::save(string& xml) {
        // Use ostringstream for safe XML generation
        std::ostringstream oss;
        oss << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n"
            << "<!DOCTYPE boost_serialization>\n"
            << "<boost_serialization signature=\"serialization::archive\" version=\"13\">\n"
            << "<cfg class_id=\"0\" tracking_level=\"0\" version=\"20\">\n"
            << "\t<version>20100713</version>\n"
            << "\t<creator>EncFSy</creator>\n"
            << "\t<cipherAlg class_id=\"1\" tracking_level=\"0\" version=\"0\">\n"
            << "\t\t<name>ssl/aes</name>\n"
            << "\t\t<major>3</major>\n"
            << "\t\t<minor>0</minor>\n"
            << "\t</cipherAlg>\n"
            << "\t<nameAlg>\n"
            << "\t\t<name>" << (this->nameAlgorithm == NameAlgorithm::Stream ? "nameio/stream" : "nameio/block") << "</name>\n"
            << "\t\t<major>" << this->nameAlgorithmMajor << "</major>\n"
            << "\t\t<minor>" << this->nameAlgorithmMinor << "</minor>\n"
            << "\t</nameAlg>\n"
            << "\t<keySize>" << this->keySize << "</keySize>\n"
            << "\t<blockSize>" << this->blockSize << "</blockSize>\n"
            << "\t<uniqueIV>" << this->uniqueIV << "</uniqueIV>\n"
            << "\t<chainedNameIV>" << this->chainedNameIV << "</chainedNameIV>\n"
            << "\t<externalIVChaining>" << this->externalIVChaining << "</externalIVChaining>\n"
            << "\t<blockMACBytes>" << this->blockMACBytes << "</blockMACBytes>\n"
            << "\t<blockMACRandBytes>" << this->blockMACRandBytes << "</blockMACRandBytes>\n"
            << "\t<allowHoles>" << this->allowHoles << "</allowHoles>\n"
            << "\t<encodedKeySize>" << this->encodedKeySize << "</encodedKeySize>\n"
            << "\t<encodedKeyData>\n" << this->encodedKeyData << "\n\t</encodedKeyData>\n"
            << "\t<saltLen>" << this->saltLen << "</saltLen>\n"
            << "\t<saltData>\n" << this->saltData << "\n\t</saltData>\n"
            << "\t<kdfIterations>" << this->kdfIterations << "</kdfIterations>\n"
            << "\t<desiredKDFDuration>" << this->desiredKDFDuration << "</desiredKDFDuration>\n"
            << "</cfg>\n"
            << "</boost_serialization>\n";
        
        xml = oss.str();
    }

    /**
     * @brief Derives encryption key from password using PBKDF2-HMAC-SHA1
     * @param password User password (will be zeroed after use for security)
     * @param pbkdf2Key Output parameter receiving derived key
     * @throws EncFSUnlockFailedException if salt decoding fails
     */
    void EncFSVolume::deriveKey(char* password, SecByteBlock& pbkdf2Key) {
        // Allocate buffer for derived key (key + IV)
        pbkdf2Key.resize(this->keySize / 8 + IV_SPEC_SIZE);
        PKCS5_PBKDF2_HMAC<SHA1> pbkdf2;

        // Decode Base64 salt to binary
        SecByteBlock salt(this->saltLen);
        {
            Base64Decoder decoder;
            decoder.Put((const byte*)this->saltData.data(), this->saltData.size());
            decoder.MessageEnd();
            if (decoder.MaxRetrievable() < salt.size()) {
                throw EncFSUnlockFailedException();
            }
            decoder.Get(salt, salt.size());
        }

        // Derive key using PBKDF2
        size_t passLen = strlen(password);
        pbkdf2.DeriveKey(
            pbkdf2Key,
            pbkdf2Key.size(),
            0,
            (const byte*)password,
            passLen,
            salt,
            salt.size(),
            this->kdfIterations
        );

        // Securely erase password from memory
        SecureZeroMemory(password, passLen);
    }

    /**
     * @brief Unlocks the volume by deriving and verifying the encryption key
     * @param password User password
     * @throws EncFSUnlockFailedException if password is incorrect or key verification fails
     */
    void EncFSVolume::unlock(char* password) {
        // Derive key from password
        SecByteBlock pbkdf2Key;
        this->deriveKey(password, pbkdf2Key);

        // Decode Base64 encrypted key
        SecByteBlock encodedKey(this->encodedKeySize);
        {
            Base64Decoder decoder;
            decoder.Put((const byte*)this->encodedKeyData.data(), this->encodedKeyData.size());
            decoder.MessageEnd();
            if (decoder.MaxRetrievable() < encodedKey.size()) {
                throw EncFSUnlockFailedException();
            }
            decoder.Get(encodedKey, encodedKey.size());
        }

        // Extract components from encoded key
        string encryptedKey((const char*)encodedKey.data() + MAC_32_SIZE, encodedKey.size() - MAC_32_SIZE);
        SecByteBlock passKey(pbkdf2Key, this->keySize / 8);
        SecByteBlock passIv(pbkdf2Key.data() + this->keySize / 8, IV_SPEC_SIZE);
        string ivSeed((const char*)encodedKey.data(), MAC_32_SIZE);
        HMAC<SHA1> passKeyHmac(passKey, passKey.size());

        // Decrypt volume key
        string plainKey;
        streamDecrypt(
            passKeyHmac, this->hmacLock,
            string((const char*)passKey.data(), passKey.size()), string((const char*)passIv.data(), passIv.size()), ivSeed,
            this->aesCfbDec, this->aesCfbDecLock,
            encryptedKey, plainKey
        );

        // Verify key integrity using MAC (constant time comparison)
        byte mac[MAC_32_SIZE];
        mac32(passKeyHmac, this->hmacLock, plainKey, (char*)mac);
        if (!CryptoPP::VerifyBufsEqual(mac, (const byte*)ivSeed.data(), MAC_32_SIZE)) {
            throw EncFSUnlockFailedException();
        }

        // Extract volume key and IV into SecByteBlock for security
        this->volumeKey.Assign((const byte*)plainKey.data(), this->keySize / 8);
        this->volumeIv.Assign((const byte*)plainKey.data() + this->keySize / 8, plainKey.size() - this->keySize / 8);
        
        // Initialize volume HMAC
        this->volumeHmac.SetKey(
            this->volumeKey,
            this->volumeKey.size()
        );
    }

    /**
     * @brief Encrypts a filename with padding and MAC
     * @param plainFileName Plain filename to encrypt
     * @param plainDirPath Parent directory path (for chained IV)
     * @param encodedFileName Output parameter receiving encrypted filename (Base64)
     */
    void EncFSVolume::encodeFileName(
        const string& plainFileName,
        const string& plainDirPath,
        string& encodedFileName
    ) {
        NameCodecContext context{
            this->volumeHmac,
            this->hmacLock,
            this->volumeKey,
            this->volumeIv,
            this->aesCbcEnc,
            this->aesCbcEncLock,
            this->aesCbcDec,
            this->aesCbcDecLock,
            this->aesCfbEnc,
            this->aesCfbEncLock,
            this->aesCfbDec,
            this->aesCfbDecLock,
            this->chainedNameIV,
            this->nameAlgorithm == NameAlgorithm::Stream,
            this->nameAlgorithmMajor
        };
        EncodeFileName(context, plainFileName, plainDirPath, this->base64Lookup, encodedFileName);
    }

    /**
     * @brief Decrypts a filename and verifies MAC
     * @param encodedFileName Encrypted filename (Base64)
     * @param plainDirPath Parent directory path (for chained IV)
     * @param plainFileName Output parameter receiving decrypted filename
     * @throws EncFSInvalidBlockException if MAC verification fails or padding is invalid
     */
    void EncFSVolume::decodeFileName(
        const string& encodedFileName,
        const string& plainDirPath,
        string& plainFileName
    ) {
        NameCodecContext context{
            this->volumeHmac,
            this->hmacLock,
            this->volumeKey,
            this->volumeIv,
            this->aesCbcEnc,
            this->aesCbcEncLock,
            this->aesCbcDec,
            this->aesCbcDecLock,
            this->aesCfbEnc,
            this->aesCfbEncLock,
            this->aesCfbDec,
            this->aesCfbDecLock,
            this->chainedNameIV,
            this->nameAlgorithm == NameAlgorithm::Stream,
            this->nameAlgorithmMajor
        };

        auto tryDecode = [&](const string& target, string& decoded) -> bool {
            return TryDecodeFileName(context, target, plainDirPath, this->base64Lookup, decoded);
        };

        string decoded;
        if (tryDecode(encodedFileName, decoded)) {
            plainFileName.append(decoded);
            return;
        }

        // Try cloud conflict suffix detection (only when cloudConflict is enabled and chainedNameIV is disabled)
        if (this->cloudConflict && !this->chainedNameIV) {
            // Try to extract cloud conflict suffix from the encoded filename
            // Supports: Dropbox, Google Drive, OneDrive patterns
            ConflictSuffixResult conflict = tryExtractCloudConflictSuffix(encodedFileName);
            
            if (conflict.found) {
                decoded.clear();
                if (tryDecode(conflict.core, decoded)) {
                    // Successfully decoded the core filename
                    // Insert the conflict suffix in the correct position
                    string conflictName = insertConflictSuffix(decoded, conflict.suffix);
                    plainFileName.append(conflictName);
                    return;
                }
            }
        }

        throw EncFSInvalidBlockException();
    }

    /**
     * @brief Internal implementation for file path encoding/decoding
     * @param srcFilePath Source file path
     * @param destFilePath Output parameter receiving processed path
     * @param encode True for encryption, false for decryption
     * @param fileExists Callback to check if encoded file exists (for conflict handling)
     * 
     * Processes each path component separately while maintaining hierarchy.
     * Handles alternate data streams if enabled.
     * 
     * For conflict suffix handling (encode only, when cloudConflict is enabled and fileExists is provided):
     * If the leaf filename has a conflict suffix (e.g., "file (conflict).txt"),
     * first try normal encoding. If the resulting encoded file doesn't exist,
     * encode the core filename and append the conflict suffix to the encoded result.
     * 
     * Supported cloud conflict formats:
     * - Dropbox: "filename (COMPUTER conflict DATE).ext"
     * - Google Drive: "filename_conf(N).ext"
     * - OneDrive: "filename-COMPUTER.ext"
     */
    void EncFSVolume::codeFilePath(
        const string& srcFilePath,
        string& destFilePath,
        bool encode,
        FileExistsCallback fileExists
    ) {
        string dirPath;
        string::size_type pos1 = 0;
        string::size_type pos2;

        // Process path component by component
        do {
            bool alt = false;
            pos2 = srcFilePath.find(g_pathSeparator, pos1);
            
            if (pos2 == string::npos) {
                if (this->altStream) {
                    // Check for alternate data stream separator
                    string::size_type altPos = srcFilePath.find(g_altSeparator, pos1);
                    if (altPos == string::npos) {
                        pos2 = srcFilePath.size();
                    }
                    else {
                        pos2 = altPos;
                        alt = true;
                    }
                }
                else {
                    pos2 = srcFilePath.size();
                }
            }
            
            if (pos2 > pos1) {
                string srcName = srcFilePath.substr(pos1, pos2 - pos1);
                string encodedPart;
                bool isLeaf = (pos2 == srcFilePath.size() || (alt && pos2 + (srcFilePath.size() - pos2) == srcFilePath.size()));

                if (encode) {
                    this->encodeFileName(srcName, dirPath, encodedPart);
                    
                    // Conflict suffix handling for leaf filename only
                    // Only applies when cloudConflict is enabled, fileExists callback is provided, and chainedNameIV is disabled
                    if (isLeaf && this->cloudConflict && fileExists != nullptr && !this->chainedNameIV) {
                        // Try to extract cloud conflict suffix from the plain filename
                        ConflictSuffixResult conflict = tryExtractCloudConflictSuffix(srcName);
                        
                        if (conflict.found) {
                            // Build full encoded path to check existence
                            string testPath = destFilePath + g_pathSeparator + encodedPart;
                            
                            // If encoded file doesn't exist, try conflict suffix approach
                            if (!fileExists(testPath)) {
                                // Re-encode without conflict suffix
                                string encodedCore;
                                this->encodeFileName(conflict.core, dirPath, encodedCore);
                                // Append conflict suffix to encoded name
                                encodedPart = encodedCore + conflict.suffix;
                            }
                        }
                    }
                }
                else {
                    this->decodeFileName(srcName, dirPath, encodedPart);
                }
                destFilePath += g_pathSeparator;
                destFilePath += encodedPart;

                // Handle alternate data stream (keep as plaintext)
                if (alt) {
                    string altName = srcFilePath.substr(pos2, srcFilePath.size() - pos2);
                    destFilePath += altName;
                    encodedPart += altName;
                    pos2 = srcFilePath.size();
                }

                // Update directory path for next component
                dirPath += g_pathSeparator;
                dirPath += encodedPart;
            }
            pos1 = pos2 + 1;
        } while (pos2 != srcFilePath.size());

        // Ensure path has at least root separator
        if (destFilePath.empty()) {
            destFilePath += g_pathSeparator;
        }
    }

    /**
     * @brief Encrypts a file path (all components)
     * @param plainFilePath Plain file path
     * @param encodedFilePath Output parameter receiving encrypted path
     */
    void EncFSVolume::encodeFilePath(
        const string& plainFilePath,
        string& encodedFilePath
    ) {
        this->codeFilePath(plainFilePath, encodedFilePath, true, nullptr);
    }

    /**
     * @brief Encrypts a file path with conflict suffix support
     * @param plainFilePath Plain file path
     * @param encodedFilePath Output parameter receiving encrypted path
     * @param fileExists Callback to check if encoded file exists
     */
    void EncFSVolume::encodeFilePath(
        const string& plainFilePath,
        string& encodedFilePath,
        FileExistsCallback fileExists
    ) {
        this->codeFilePath(plainFilePath, encodedFilePath, true, fileExists);
    }

    /**
     * @brief Decrypts a file path (all components)
     * @param encodedFilePath Encrypted file path
     * @param plainFilePath Output parameter receiving plain path
     * @throws EncFSInvalidBlockException if decryption fails
     */
    void EncFSVolume::decodeFilePath(
        const string& encodedFilePath,
        string& plainFilePath
    ) {
        this->codeFilePath(encodedFilePath, plainFilePath, false, nullptr);
    }

    /**
     * @brief Converts encrypted file size to logical (decrypted) size
     * @param encodedLength Encrypted file size in bytes
     * @return Logical file size in bytes
     */
    int64_t EncFSVolume::toDecodedLength(const int64_t encodedLength) {
        FileCodecContext context{
            this->volumeHmac,
            this->hmacLock,
            this->volumeKey,
            this->volumeIv,
            this->aesCbcEnc,
            this->aesCbcEncLock,
            this->aesCbcDec,
            this->aesCbcDecLock,
            this->aesCfbEnc,
            this->aesCfbEncLock,
            this->aesCfbDec,
            this->aesCfbDecLock,
            this->uniqueIV,
            this->externalIVChaining,
            this->allowHoles,
            this->blockSize,
            this->blockMACBytes,
            this->blockMACRandBytes
        };
        return ToDecodedLength(context, encodedLength);
    }

    /**
     * @brief Converts logical (decrypted) size to encrypted file size
     * @param decodedLength Logical file size in bytes
     * @return Encrypted file size in bytes
     */
    int64_t EncFSVolume::toEncodedLength(const int64_t decodedLength) {
        FileCodecContext context{
            this->volumeHmac,
            this->hmacLock,
            this->volumeKey,
            this->volumeIv,
            this->aesCbcEnc,
            this->aesCbcEncLock,
            this->aesCbcDec,
            this->aesCbcDecLock,
            this->aesCfbEnc,
            this->aesCfbEncLock,
            this->aesCfbDec,
            this->aesCfbDecLock,
            this->uniqueIV,
            this->externalIVChaining,
            this->allowHoles,
            this->blockSize,
            this->blockMACBytes,
            this->blockMACRandBytes
        };
        return ToEncodedLength(context, decodedLength);
    }

    /**
     * @brief Encrypts file IV to file header
     * @param plainFilePath File path (for external IV chaining)
     * @param fileIv File initialization vector (64-bit)
     * @param encodedFileHeader Output parameter receiving encrypted header (8 bytes)
     */
    void EncFSVolume::encodeFileIv(
        const string& plainFilePath,
        const int64_t fileIv,
        string& encodedFileHeader
    ) {
        FileCodecContext context{
            this->volumeHmac,
            this->hmacLock,
            this->volumeKey,
            this->volumeIv,
            this->aesCbcEnc,
            this->aesCbcEncLock,
            this->aesCbcDec,
            this->aesCbcDecLock,
            this->aesCfbEnc,
            this->aesCfbEncLock,
            this->aesCfbDec,
            this->aesCfbDecLock,
            this->uniqueIV,
            this->externalIVChaining,
            this->allowHoles,
            this->blockSize,
            this->blockMACBytes,
            this->blockMACRandBytes
        };
        EncodeFileIv(context, plainFilePath, fileIv, encodedFileHeader);
    }

    /**
     * @brief Decrypts file IV from file header
     * @param plainFilePath File path (for external IV chaining)
     * @param encodedFileHeader Encrypted file header (8 bytes)
     * @return Decrypted file initialization vector (64-bit)
     */
    int64_t EncFSVolume::decodeFileIv(
        const string& plainFilePath,
        const string& encodedFileHeader
    ) {
        FileCodecContext context{
            this->volumeHmac,
            this->hmacLock,
            this->volumeKey,
            this->volumeIv,
            this->aesCbcEnc,
            this->aesCbcEncLock,
            this->aesCbcDec,
            this->aesCbcDecLock,
            this->aesCfbEnc,
            this->aesCfbEncLock,
            this->aesCfbDec,
            this->aesCfbDecLock,
            this->uniqueIV,
            this->externalIVChaining,
            this->allowHoles,
            this->blockSize,
            this->blockMACBytes,
            this->blockMACRandBytes
        };
        return DecodeFileIv(context, plainFilePath, encodedFileHeader);
    }

    /**
     * @brief Encrypts a data block
     * @param fileIv File initialization vector
     * @param blockNum Block number (0-based)
     * @param plainBlock Plain data block
     * @param encodedBlock Output parameter receiving encrypted block
     */
    void EncFSVolume::encodeBlock(
        const int64_t fileIv,
        const int64_t blockNum,
        const string& plainBlock,
        string& encodedBlock
    ) {
        FileCodecContext context{
            this->volumeHmac,
            this->hmacLock,
            this->volumeKey,
            this->volumeIv,
            this->aesCbcEnc,
            this->aesCbcEncLock,
            this->aesCbcDec,
            this->aesCbcDecLock,
            this->aesCfbEnc,
            this->aesCfbEncLock,
            this->aesCfbDec,
            this->aesCfbDecLock,
            this->uniqueIV,
            this->externalIVChaining,
            this->allowHoles,
            this->blockSize,
            this->blockMACBytes,
            this->blockMACRandBytes
        };
        EncodeBlock(context, fileIv, blockNum, plainBlock, encodedBlock);
    }

    /**
     * @brief Decrypts a data block
     * @param fileIv File initialization vector
     * @param blockNum Block number (0-based)
     * @param encodedBlock Encrypted data block
     * @param plainBlock Output parameter receiving plain data
     * @throws EncFSInvalidBlockException if MAC verification fails
     */
    void EncFSVolume::decodeBlock(
        const int64_t fileIv,
        const int64_t blockNum,
        const string& encodedBlock,
        string& plainBlock
    ) {
        FileCodecContext context{
            this->volumeHmac,
            this->hmacLock,
            this->volumeKey,
            this->volumeIv,
            this->aesCbcEnc,
            this->aesCbcEncLock,
            this->aesCbcDec,
            this->aesCbcDecLock,
            this->aesCfbEnc,
            this->aesCfbEncLock,
            this->aesCfbDec,
            this->aesCfbDecLock,
            this->uniqueIV,
            this->externalIVChaining,
            this->allowHoles,
            this->blockSize,
            this->blockMACBytes,
            this->blockMACRandBytes
        };
        DecodeBlock(context, fileIv, blockNum, encodedBlock, plainBlock);
    }

} // namespace EncFS
