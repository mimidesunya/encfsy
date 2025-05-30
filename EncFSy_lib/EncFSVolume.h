#pragma once

#include <string>
#include <mutex>
#include <exception>

#include <modes.h>
#include <pwdbased.h>
#include <sha.h>
#include <osrng.h>

using namespace std;
using namespace CryptoPP;

namespace EncFS
{
	enum EncFSMode {
		STANDARD = 1,
		PARANOIA = 2
	};

	/**
	EncFS volume configuration.
	This class provides foundermental encode/decode functions.
	Functions are thread-safe except loadConfig(), unlock().
	**/
	class EncFSVolume {
	public:
		/** Block header size **/
		static const int32_t HEADER_SIZE = 8;

		/** NTFS alt data stream support. **/
		bool altStream;

	private:
		/** reverse mode */
		bool reverse;

		/** Key size. 192 or 256�B */
		int32_t keySize;
		/** Block size of data. Fixed to 1024. */
		int32_t blockSize;
		/** Generated different IV for each files. */
		bool uniqueIV;
		/** Generate filename IV from parent file path. */
		bool chainedNameIV;
		/** Generate content IV from parent file path. */
		bool externalIVChaining;
		int32_t blockMACBytes;
		int32_t blockMACRandBytes;
		bool allowHoles;

		int32_t encodedKeySize;
		string encodedKeyData;

		int32_t saltLen;
		string saltData;

		/** Iteration count of key derivation function. */
		int32_t kdfIterations;
		/** Expected time for execute key derivation function.�@*/
		int32_t desiredKDFDuration;

		string volumeKey;
		string volumeIv;

		HMAC<SHA1> volumeHmac;
		mutex hmacLock;

		int base64Lookup[256];

		// AES / CBC / NoPadding
		CBC_Mode<AES>::Encryption aesCbcEnc;
		mutex aesCbcEncLock;
		CBC_Mode<AES>::Decryption aesCbcDec;
		mutex aesCbcDecLock;

		// AES / CFB / NoPadding
		CFB_Mode<AES>::Encryption aesCfbEnc;
		mutex aesCfbEncLock;
		CFB_Mode<AES>::Decryption aesCfbDec;
		mutex aesCfbDecLock;

	public:
		EncFSVolume();
		~EncFSVolume() {};

		/**
		Load EncFS configuration file. There are rectrictions:
		.encfs6.xml format
		cipherAlg ssl/aes 3.0
		nameAlg nameio/block 3.0
		**/
		void load(const string &xml, bool reverse);

		void create(char* password, EncFSMode mode, bool reverse);

		void save(string &xml);

		inline int32_t getHeaderSize() {
			return this->blockMACRandBytes + this->blockMACBytes;
		}
		inline int32_t getBlockSize() {
			return this->blockSize;
		}
		inline bool isChainedNameIV() {
			return this->chainedNameIV;
		}
		inline bool isExternalIVChaining() {
			return this->externalIVChaining;
		}
		inline bool isUniqueIV() {
			return this->uniqueIV;
		}
		inline bool isReverse() {
			return this->reverse;
		}

		/**
		Decode volume key.
		**/
		void unlock(char* password);

		void encodeFileName(const string &plainFileName, const string &plainDirPath, string &encodedFileName);
		void decodeFileName(const string &encodedFileName, const string &plainDirPath, string &plainFileName);
		void encodeFilePath(const string &plainFilePath, string &encodedFilePath);
		void decodeFilePath(const string &plainFilePath, string &encodedFilePath);
		int64_t toDecodedLength(const int64_t encodedLength);
		int64_t toEncodedLength(const int64_t decodedLength);

		void encodeFileIv(const string &plainFilePath, const int64_t fileIv, string &encodedFileHeader);
		int64_t decodeFileIv(const string &plainFilePath, const string &encodedFileHeader);

		void encodeBlock(const int64_t fileIv, const int64_t blockNum, const string &plainBlock, string &encodedBlock);
		void decodeBlock(const int64_t fileIv, const int64_t blockNum, const string &encodedBlock, string &plainBlock);

	private:
		void deriveKey(char* password, string &pbkdf2Key);
		void processFileName(SymmetricCipher &cipher, mutex &cipherLock, const string &fileIv, const string &binFileName, string &fileName);
		void codeBlock(const int64_t fileIv, const int64_t blockNum, const bool encode, const string &encodedBlock, string &plainBlock);
		void codeFilePath(const string &srcFilePath, string &destFilePath, bool encode);
	};

	class EncFSBadConfigurationException : runtime_error {
	public:
		EncFSBadConfigurationException(const char* what) : runtime_error(what) {};
		EncFSBadConfigurationException(const string &what) : runtime_error(what) {};
		~EncFSBadConfigurationException() {};
		const char* what() const noexcept {
			return runtime_error::what();
		}
	};

	class EncFSUnlockFailedException : exception {
	public:
		EncFSUnlockFailedException() {}
		~EncFSUnlockFailedException() {};

		const char* what() const noexcept {
			return "Unlock failed.";
		}
	};

	class EncFSIllegalStateException : exception {
	public:
		EncFSIllegalStateException() {}
		~EncFSIllegalStateException() {};

		const char* what() const noexcept {
			return "Illegal state.";
		}
	};

	class EncFSInvalidBlockException : exception {
	public:
		EncFSInvalidBlockException() {}
		~EncFSInvalidBlockException() {};

		const char* what() const noexcept {
			return "Invalid block.";
		}
	};
}