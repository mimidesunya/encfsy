#pragma once

#include <string>
#include <mutex>
#include <exception>

#include <modes.h>
#include <pwdbased.h>
#include <sha.h>
#include <osrng.h>
#include <secblock.h> // For SecByteBlock

namespace EncFS
{
	/**
	 * @brief EncFS encryption mode selection
	 */
	enum EncFSMode {
		STANDARD = 1,  // Standard security mode (192-bit key, basic IV chaining)
		PARANOIA = 2   // Paranoia security mode (256-bit key, full IV chaining)
	};

	/**
	 * @brief EncFS volume configuration and cryptographic operations
	 * 
	 * This class manages the EncFS volume configuration and provides fundamental
	 * encryption/decryption functions for filenames, file paths, and file blocks.
	 * 
	 * Thread Safety:
	 * - Most methods are thread-safe through internal mutex locks
	 * - Exception: load() and unlock() should be called before multi-threaded access
	 */
	class EncFSVolume {
	public:
		/**
		 * @brief Callback function type for checking if encoded file exists
		 * @param encodedPath Full encoded file path to check
		 * @return true if file exists, false otherwise
		 */
		using FileExistsCallback = bool(*)(const std::string& encodedPath);

		/** File IV header size in bytes */
		static const int32_t HEADER_SIZE = 8;

		/** Enable NTFS alternate data stream support */
		bool altStream = false;

	private:
		/** Reverse encryption mode flag */
		bool reverse = false;

		/** Encryption key size in bits (192 or 256) */
		int32_t keySize = 0;
		
		/** Block size for encryption (fixed at 1024 bytes) */
		int32_t blockSize = 0;
		
		/** Generate unique IV for each file */
		bool uniqueIV = false;
		
		/** Generate filename IV based on parent directory path */
		bool chainedNameIV = false;
		
		/** Generate file content IV based on file path */
		bool externalIVChaining = false;
		
		/** Size of block MAC (Message Authentication Code) in bytes */
		int32_t blockMACBytes = 0;
		
		/** Size of random bytes in block MAC */
		int32_t blockMACRandBytes = 0;
		
		/** Allow sparse file optimization (zero blocks) */
		bool allowHoles = false;

		/** Size of encoded encryption key */
		int32_t encodedKeySize = 0;
		
		/** Base64-encoded encrypted volume key */
		std::string encodedKeyData;

		/** Length of password salt */
		int32_t saltLen = 0;
		
		/** Base64-encoded password salt */
		std::string saltData;

		/** PBKDF2 iteration count for key derivation */
		int32_t kdfIterations = 0;
		
		/** Target duration for key derivation (milliseconds) */
		int32_t desiredKDFDuration = 0;

		/** Decrypted volume encryption key (using secure memory) */
		CryptoPP::SecByteBlock volumeKey;
		
		/** Volume initialization vector (using secure memory) */
		CryptoPP::SecByteBlock volumeIv;

		/** HMAC-SHA1 instance for volume operations */
		CryptoPP::HMAC<CryptoPP::SHA1> volumeHmac;
		
		/** Mutex for thread-safe HMAC operations */
		std::mutex hmacLock;

		/** Base64 decoding lookup table */
		int base64Lookup[256] = {0};

		// AES-CBC encryption/decryption (for fixed-size blocks)
		CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption aesCbcEnc;  // AES CBC encryption cipher
		std::mutex aesCbcEncLock;                   // Mutex for CBC encryption
		CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption aesCbcDec;  // AES CBC decryption cipher
		std::mutex aesCbcDecLock;                   // Mutex for CBC decryption

		// AES-CFB encryption/decryption (for variable-size data)
		CryptoPP::CFB_Mode<CryptoPP::AES>::Encryption aesCfbEnc;  // AES CFB encryption cipher
		std::mutex aesCfbEncLock;                   // Mutex for CFB encryption
		CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption aesCfbDec;  // AES CFB decryption cipher
		std::mutex aesCfbDecLock;                   // Mutex for CFB decryption

	public:
		/**
		 * @brief Constructor - initializes Base64 decoder
		 */
		EncFSVolume();
		
		/**
		 * @brief Destructor
		 */
		~EncFSVolume() = default;

		/**
		 * @brief Loads EncFS configuration from XML
		 * @param xml Configuration XML std::string (.encfs6.xml format)
		 * @param reverse Enable reverse encryption mode
		 * @throws EncFSBadConfigurationException if configuration is invalid
		 * 
		 * Requirements:
		 * - Format: .encfs6.xml
		 * - Cipher algorithm: ssl/aes 3.0
		 * - Name algorithm: nameio/block 3.0
		 * 
		 * Note: This method is NOT thread-safe. Call before multi-threaded access.
		 */
		void load(const std::string &xml, bool reverse);

		/**
		 * @brief Creates a new EncFS volume configuration
		 * @param password Password for volume encryption
		 * @param mode Security mode (STANDARD or PARANOIA)
		 * @param reverse Enable reverse encryption mode
		 * 
		 * Generates random salt, derives encryption key, and configures volume.
		 */
		void create(char* password, EncFSMode mode, bool reverse);

		/**
		 * @brief Saves volume configuration to XML std::string
		 * @param xml Output parameter receiving XML configuration
		 */
		void save(std::string &xml);

		/**
		 * @brief Gets the block header size (MAC + random bytes)
		 * @return Header size in bytes
		 */
		inline int32_t getHeaderSize() const {
			return this->blockMACRandBytes + this->blockMACBytes;
		}
		
		/**
		 * @brief Gets the encryption block size
		 * @return Block size in bytes (1024)
		 */
		inline int32_t getBlockSize() const {
			return this->blockSize;
		}
		
		/**
		 * @brief Checks if chained filename IV mode is enabled
		 * @return True if filename IV depends on directory path
		 */
		inline bool isChainedNameIV() const {
			return this->chainedNameIV;
		}
		
		/**
		 * @brief Checks if external IV chaining is enabled
		 * @return True if file content IV depends on file path
		 */
		inline bool isExternalIVChaining() const {
			return this->externalIVChaining;
		}
		
		/**
		 * @brief Checks if unique file IV mode is enabled
		 * @return True if each file has a unique IV
		 */
		inline bool isUniqueIV() const {
			return this->uniqueIV;
		}
		
		/**
		 * @brief Checks if reverse encryption mode is enabled
		 * @return True if in reverse mode
		 */
		inline bool isReverse() const {
			return this->reverse;
		}

		/**
		 * @brief Unlocks the volume by deriving the encryption key from password
		 * @param password User password (will be zeroed after use)
		 * @throws EncFSUnlockFailedException if password is incorrect
		 * 
		 * Note: This method is NOT thread-safe. Call before multi-threaded access.
		 */
		void unlock(char* password);

		/**
		 * @brief Encrypts a filename
		 * @param plainFileName Plain filename to encrypt
		 * @param plainDirPath Parent directory path (for chained IV)
		 * @param encodedFileName Output parameter receiving encrypted filename
		 */
		void encodeFileName(const std::string &plainFileName, const std::string &plainDirPath, std::string &encodedFileName);
		
		/**
		 * @brief Decrypts a filename
		 * @param encodedFileName Encrypted filename
		 * @param plainDirPath Parent directory path (for chained IV)
		 * @param plainFileName Output parameter receiving plain filename
		 * @throws EncFSInvalidBlockException if decryption fails or checksum mismatch
		 */
		void decodeFileName(const std::string &encodedFileName, const std::string &plainDirPath, std::string &plainFileName);
		
		/**
		 * @brief Encrypts a file path (all path components)
		 * @param plainFilePath Plain file path
		 * @param encodedFilePath Output parameter receiving encrypted path
		 */
		void encodeFilePath(const std::string &plainFilePath, std::string &encodedFilePath);
		
		/**
		 * @brief Encrypts a file path with conflict suffix support
		 * @param plainFilePath Plain file path
		 * @param encodedFilePath Output parameter receiving encrypted path
		 * @param fileExists Callback to check if encoded file exists (for conflict handling)
		 * 
		 * When the plain filename has a conflict suffix (e.g., "file (conflict).txt"),
		 * this method first tries normal encoding. If the file doesn't exist,
		 * it extracts the conflict suffix, encodes the core filename, and appends
		 * the suffix to the encoded result.
		 */
		void encodeFilePath(const std::string &plainFilePath, std::string &encodedFilePath, FileExistsCallback fileExists);
		
		/**
		 * @brief Decrypts a file path (all path components)
		 * @param plainFilePath Encrypted file path
		 * @param encodedFilePath Output parameter receiving plain path
		 * @throws EncFSInvalidBlockException if decryption fails
		 */
		void decodeFilePath(const std::string &plainFilePath, std::string &encodedFilePath);
		
		/**
		 * @brief Converts encrypted file size to logical (decrypted) size
		 * @param encodedLength Encrypted file size in bytes
		 * @return Logical file size in bytes
		 */
		int64_t toDecodedLength(const int64_t encodedLength);
		
		/**
		 * @brief Converts logical (decrypted) size to encrypted file size
		 * @param decodedLength Logical file size in bytes
		 * @return Encrypted file size in bytes
		 */
		int64_t toEncodedLength(const int64_t decodedLength);

		/**
		 * @brief Encrypts file IV to file header
		 * @param plainFilePath File path (for external IV chaining)
		 * @param fileIv File initialization vector
		 * @param encodedFileHeader Output parameter receiving encrypted header
		 */
		void encodeFileIv(const std::string &plainFilePath, const int64_t fileIv, std::string &encodedFileHeader);
		
		/**
		 * @brief Decrypts file IV from file header
		 * @param plainFilePath File path (for external IV chaining)
		 * @param encodedFileHeader Encrypted file header
		 * @return Decrypted file initialization vector
		 */
		int64_t decodeFileIv(const std::string &plainFilePath, const std::string &encodedFileHeader);

		/**
		 * @brief Encrypts a data block
		 * @param fileIv File initialization vector
		 * @param blockNum Block number (0-based)
		 * @param plainBlock Plain data block
		 * @param encodedBlock Output parameter receiving encrypted block
		 */
		void encodeBlock(const int64_t fileIv, const int64_t blockNum, const std::string &plainBlock, std::string &encodedBlock);
		
		/**
		 * @brief Decrypts a data block
		 * @param fileIv File initialization vector
		 * @param blockNum Block number (0-based)
		 * @param encodedBlock Encrypted data block
		 * @param plainBlock Output parameter receiving plain data
		 * @throws EncFSInvalidBlockException if MAC verification fails
		 */
		void decodeBlock(const int64_t fileIv, const int64_t blockNum, const std::string &encodedBlock, std::string &plainBlock);

	private:
		/**
		 * @brief Derives encryption key from password using PBKDF2
		 * @param password User password (will be zeroed after use)
		 * @param pbkdf2Key Output parameter receiving derived key
		 */
		void deriveKey(char* password, CryptoPP::SecByteBlock &pbkdf2Key);
		
		/**
		 * @brief Processes filename encryption/decryption
		 * @param cipher AES cipher instance (CBC mode)
		 * @param cipherLock Mutex for thread-safe cipher access
		 * @param fileIv File-specific initialization vector
		 * @param binFileName Input binary filename
		 * @param fileName Output parameter receiving processed filename
		 */
		void processFileName(CryptoPP::SymmetricCipher &cipher, std::mutex &cipherLock, const std::string &fileIv, const std::string &binFileName, std::string &fileName);
		
		/**
		 * @brief Internal block encryption/decryption implementation
		 * @param fileIv File initialization vector
		 * @param blockNum Block number
		 * @param encode True for encryption, false for decryption
		 * @param encodedBlock Input block (encrypted if decode, plain if encode)
		 * @param plainBlock Output block (plain if decode, plain if encode)
		 * @throws EncFSInvalidBlockException if MAC verification fails (decode only)
		 */
		void codeBlock(const int64_t fileIv, const int64_t blockNum, const bool encode, const std::string &encodedBlock, std::string &plainBlock);
		
		/**
		 * @brief Internal file path encoding/decoding implementation
		 * @param srcFilePath Source file path
		 * @param destFilePath Output parameter receiving processed path
		 * @param encode True for encryption, false for decryption
		 * @param fileExists Callback to check if encoded file exists (for conflict handling, may be nullptr)
		 */
		void codeFilePath(const std::string &srcFilePath, std::string &destFilePath, bool encode, FileExistsCallback fileExists);
	};

	/**
	 * @brief Exception thrown when configuration is invalid or corrupted
	 */
	class EncFSBadConfigurationException : public std::runtime_error {
	public:
		EncFSBadConfigurationException(const char* what) : std::runtime_error(what) {};
		EncFSBadConfigurationException(const std::string &what) : std::runtime_error(what) {};
		~EncFSBadConfigurationException() override = default;
		const char* what() const noexcept override {
			return std::runtime_error::what();
		}
	};

	/**
	 * @brief Exception thrown when password is incorrect or key derivation fails
	 */
	class EncFSUnlockFailedException : public std::exception {
	public:
		EncFSUnlockFailedException() = default;
		~EncFSUnlockFailedException() override = default;

		const char* what() const noexcept override {
			return "Unlock failed.";
		}
	};

	/**
	 * @brief Exception thrown for illegal state or invalid operations
	 */
	class EncFSIllegalStateException : public std::exception {
	public:
		EncFSIllegalStateException() = default;
		~EncFSIllegalStateException() override = default;

		const char* what() const noexcept override {
			return "Illegal state.";
		}
	};

	/**
	 * @brief Exception thrown when block decryption fails or MAC verification fails
	 */
	class EncFSInvalidBlockException : public std::exception {
	public:
		EncFSInvalidBlockException() = default;
		~EncFSInvalidBlockException() override = default;

		const char* what() const noexcept override {
			return "Invalid block.";
		}
	};
}