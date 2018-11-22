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
	/**
	EncFSのボリューム設定です。
	このクラスは基礎的な暗号化、復号化機能を提供しています。
	loadConfig(), unlock() を除いてマルチスレッドに対応しています。
	**/
	class EncFSVolume {
	public:
		/** ブロックのヘッダーサイズ。 **/
		static const size_t HEADER_SIZE = 8;

	private:
		/** 暗号化キーのサイズ。192または256。 */
		int keySize;
		/** ファイル内のブロックサイズ。1024だけ。 */
		int blockSize;
		bool uniqueIV;
		/** ファイル名の暗号化を上位のディレクトリ名に依存させるか。 */
		bool chainedNameIV;
		/** ファイル内の暗号化を上位のディレクトリ名に依存させるか。 */
		bool externalIVChaining;
		int blockMACBytes;
		int blockMACRandBytes;
		bool allowHoles;

		int encodedKeySize;
		string encodedKeyData;

		int saltLen;
		string saltData;

		/** 鍵導出関数の繰り返し回数。 */
		int kdfIterations;
		/**  鍵導出関数にかかる見積もり時間。　*/
		int desiredKDFDuration;

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
		EncFSの設定を読み込みます。次の形式に固定されます。
		.encfs6.xml 形式
		cipherAlg ssl/aes 3.0
		nameAlg nameio/block 3.0
		**/
		void load(const string &xml);

		void create(char* password);

		void save(string &xml);

		inline size_t getHeaderSize() {
			return this->blockMACRandBytes + this->blockMACBytes;
		}
		inline size_t getBlockSize() {
			return this->blockSize;
		}
		inline bool isChainedNameIV() {
			return this->chainedNameIV;
		}
		inline bool isExternalIVChaining() {
			return this->externalIVChaining;
		}

		/**
		ボリュームの暗号化を解除して読み書きの操作を実行可能にします。
		**/
		void unlock(char* password);

		void encodeFileName(const string &plainFileName, const string &plainDirPath, string &encodedFileName);
		void decodeFileName(const string &encodedFileName, const string &plainDirPath, string &plainFileName);
		void encodeFilePath(const string &plainFilePath, string &encodedFilePath);
		size_t toDecodedLength(const size_t encodedLength);
		size_t toEncodedLength(const size_t decodedLength);

		void encodeFileIv(const string &plainFilePath, const int64_t fileIv, string &encodedFileHeader);
		int64_t decodeFileIv(const string &plainFilePath, const string &encodedFileHeader);

		void encodeBlock(const int64_t fileIv, const int64_t blockNum, const string &plainBlock, string &encodedBlock);
		void decodeBlock(const int64_t fileIv, const int64_t blockNum, const string &encodedBlock, string &plainBlock);

	private:
		void deriveKey(char* password, string &pbkdf2Key);
		void processFileName(SymmetricCipher &cipher, mutex &cipherLock, const string &fileIv, const string &binFileName, string &fileName);
		void codeBlock(const int64_t fileIv, const int64_t blockNum, const bool encode, const string &encodedBlock, string &plainBlock);
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
			return "キーを復号できませんでした。";
		}
	};

	class EncFSInvalidBlockException : exception {
	public:
		EncFSInvalidBlockException() {}
		~EncFSInvalidBlockException() {};

		const char* what() const noexcept {
			return "不適切な暗号化が行われています。";
		}
	};
}