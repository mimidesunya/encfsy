#pragma once

/**
 * @file EncFSUtils.hpp
 * @brief Cryptographic utility functions for EncFS encryption/decryption operations
 * 
 * This header contains inline utility functions for:
 * - Base64 encoding/decoding for filenames
 * - Big-endian integer conversion
 * - IV (Initialization Vector) generation and manipulation
 * - Block cipher operations (AES-CBC)
 * - Stream cipher operations (AES-CFB)
 * - MAC (Message Authentication Code) calculation using HMAC-SHA1
 * - Chain IV computation for directory-based encryption
 * 
 * All cryptographic operations use CryptoPP library and are thread-safe
 * through mutex locking where necessary.
 */

#include <string>
#include <mutex>
#include <algorithm>
#include <cctype>

#include <modes.h>
#include <pwdbased.h>
#include <sha.h>
#include <osrng.h>
#include <base64.h>

using namespace std;
using namespace CryptoPP;

namespace EncFS
{
	/** Path separator for Windows platform */
	static const string g_pathSeparator("\\");

	/** NTFS alternate data stream separator */
	static const string g_altSeparator(":");

	/** Remove leading whitespace from string */
	static inline void ltrim(string &s) {
		s.erase(s.begin(), find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !isspace(ch);
		}));
	}

	/** Remove trailing whitespace from string */
	static inline void rtrim(string &s) {
		s.erase(find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
			return !isspace(ch);
		}).base(), s.end());
	}

	/** Remove leading and trailing whitespace */
	static inline void trim(string &s) {
		ltrim(s);
		rtrim(s);
	}

	/** Custom base64 alphabet for filename encoding (avoids filesystem-unsafe characters) */
	static const byte ALPHABET[] = ",-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

	/**
	 * @brief Decode base64-encoded filename to binary string
	 * @param lookup Reverse lookup table for custom alphabet
	 * @param encodedName Base64-encoded filename
	 * @param decodedName Output binary string (APPENDS to existing content - DO NOT clear())
	 * @return true if successful, false if invalid character found
	 * 
	 * Uses 6-bit encoding to convert base64 characters back to original bytes.
	 * 
	 * WARNING: This function APPENDS to decodedName rather than replacing it.
	 * Do NOT add decodedName.clear() - it would break path encoding in encodeFilePath().
	 * The append behavior is required for EncFS compatibility.
	 */
	inline bool decodeBase64FileName(const int* lookup, const string &encodedName, string &decodedName) {
		if (encodedName.empty()) {
			return true;
		}

		// Pre-allocate space to avoid multiple reallocations
		const size_t estimatedSize = (encodedName.size() * 6) / 8;
		decodedName.reserve(decodedName.size() + estimatedSize);

		string in;
		in.resize(encodedName.size());
		for (size_t i = 0; i < encodedName.size(); i++) {
			unsigned char ch = static_cast<unsigned char>(encodedName[i]);
			if (ch >= 256 || lookup[ch] == -1) {
				return false;
			}
			in[i] = static_cast<char>(lookup[ch]);
		}

		size_t srcIdx = 0;
		int workBits = 0;
		unsigned int work = 0;
		while (srcIdx < in.size()) {
			work |= static_cast<unsigned char>(in[srcIdx++]) << workBits;
			workBits += 6;

			while (workBits >= 8) {
				decodedName.push_back(static_cast<char>(work & 0xff));
				work >>= 8;
				workBits -= 8;
			}
		}
		return true;
	}

	/**
	 * @brief Encode binary string to base64 filename
	 * @param in Binary input string
	 * @param out Base64-encoded filename (APPENDS to existing content - DO NOT clear())
	 * 
	 * Uses custom alphabet safe for filesystem names. Encodes 8-bit bytes
	 * into 6-bit characters.
	 * 
	 * WARNING: This function APPENDS to out rather than replacing it.
	 * Do NOT add out.clear() - it would break path encoding in encodeFilePath().
	 * The caller (EncFSVolume::encodeFileName) appends path separator first,
	 * then calls this function to append the encoded name.
	 * This append behavior is required for EncFS compatibility.
	 */
	inline void encodeBase64FileName(const string &in, string &out) {
		if (in.empty()) {
			return;
		}

		const size_t outSize = in.size() * 8 / 6 + ((in.size() * 8 % 6) == 0 ? 0 : 1);
		const size_t startPos = out.size();
		
		// Pre-allocate space to avoid multiple reallocations
		out.reserve(startPos + outSize);
		
		const int mask = (1 << 6) - 1;
		int workingBits = 0;
		unsigned long work = 0;
		
		for (size_t i = 0; i < in.size(); ++i) {
			unsigned char unsignedIntValue = static_cast<unsigned char>(in[i]);
			work |= static_cast<unsigned long>(unsignedIntValue) << workingBits;

			workingBits += 8;

			while (workingBits > 6) {
				out.push_back(static_cast<char>(work & mask));
				work >>= 6;
				workingBits -= 6;
			}
		}

		if (workingBits > 0) {
			out.push_back(static_cast<char>(work & mask));
		}

		// Convert to alphabet in-place (reverse order)
		for (size_t i = 0; i < outSize; ++i) {
			out[startPos + outSize - i - 1] = ALPHABET[static_cast<unsigned char>(out[startPos + outSize - i - 1])];
		}
	}

	/** Convert 4-byte big-endian string to 32-bit integer */
	inline int32_t bytesToIntByBE(const string &bytes) {
		// Optimized: single expression without intermediate assignments
		return (static_cast<int32_t>(static_cast<unsigned char>(bytes[0])) << 24) |
		       (static_cast<int32_t>(static_cast<unsigned char>(bytes[1])) << 16) |
		       (static_cast<int32_t>(static_cast<unsigned char>(bytes[2])) << 8) |
		        static_cast<int32_t>(static_cast<unsigned char>(bytes[3]));
	}

	/** Convert 8-byte big-endian string to 64-bit integer */
	inline int64_t bytesToLongByBE(const string &bytes) {
		// Optimized: single expression without intermediate assignments
		return (static_cast<int64_t>(static_cast<unsigned char>(bytes[0])) << 56) |
		       (static_cast<int64_t>(static_cast<unsigned char>(bytes[1])) << 48) |
		       (static_cast<int64_t>(static_cast<unsigned char>(bytes[2])) << 40) |
		       (static_cast<int64_t>(static_cast<unsigned char>(bytes[3])) << 32) |
		       (static_cast<int64_t>(static_cast<unsigned char>(bytes[4])) << 24) |
		       (static_cast<int64_t>(static_cast<unsigned char>(bytes[5])) << 16) |
		       (static_cast<int64_t>(static_cast<unsigned char>(bytes[6])) << 8) |
		        static_cast<int64_t>(static_cast<unsigned char>(bytes[7]));
	}

	/** Convert 32-bit integer to 4-byte big-endian string */
	inline void intToBytesByBE(string &bytes, int32_t num) {
		bytes.resize(4);
		bytes[0] = static_cast<char>((num >> 24) & 0xFF);
		bytes[1] = static_cast<char>((num >> 16) & 0xFF);
		bytes[2] = static_cast<char>((num >> 8) & 0xFF);
		bytes[3] = static_cast<char>(num & 0xFF);
	}

	/** Convert 64-bit integer to 8-byte big-endian string */
	inline void longToBytesByBE(string &bytes, int64_t num) {
		bytes.resize(8);
		bytes[0] = static_cast<char>((num >> 56) & 0xFF);
		bytes[1] = static_cast<char>((num >> 48) & 0xFF);
		bytes[2] = static_cast<char>((num >> 40) & 0xFF);
		bytes[3] = static_cast<char>((num >> 32) & 0xFF);
		bytes[4] = static_cast<char>((num >> 24) & 0xFF);
		bytes[5] = static_cast<char>((num >> 16) & 0xFF);
		bytes[6] = static_cast<char>((num >> 8) & 0xFF);
		bytes[7] = static_cast<char>(num & 0xFF);
	}

	/**
	 * @brief Increment IV seed by one (supports 4-byte or 8-byte seeds)
	 * @param ivSeed Original IV seed (4 or 8 bytes)
	 * @param ivSeedPlusOne Output incremented IV seed
	 * 
	 * Used for block counter mode in stream encryption.
	 */
	inline void incrementIvSeedByOne(const string &ivSeed, string &ivSeedPlusOne) {
		if (ivSeed.size() == 4) {
			int32_t num = bytesToIntByBE(ivSeed);
			++num;
			intToBytesByBE(ivSeedPlusOne, num);
		}
		else {
			int64_t num = bytesToLongByBE(ivSeed);
			++num;
			longToBytesByBE(ivSeedPlusOne, num);
		}
	}

	/**
	 * @brief Reverse byte order in blocks of 64 bytes
	 * @param src Source binary string
	 * @param dest Destination with flipped bytes
	 * 
	 * Used in stream cipher diffusion to improve security.
	 */
	inline void flipBytes(const string &src, string &dest) {
		dest.resize(src.size());

		size_t offset = 0;
		size_t bytesLeft = src.size();

		while (bytesLeft > 0) {
			size_t toFlip = bytesLeft > 64 ? 64 : bytesLeft;

			for (size_t i = 0; i < toFlip; i++)
				dest[offset + i] = src[offset + toFlip - i - 1];

			bytesLeft -= toFlip;
			offset += toFlip;
		}

	}

	/**
	 * @brief Generate 16-byte initialization vector using HMAC-SHA1
	 * @param hmac HMAC-SHA1 instance
	 * @param hmacLock Mutex for thread-safe HMAC operations
	 * @param iv Base initialization vector
	 * @param ivSeed Seed value to mix with IV (4 or 8 bytes)
	 * @param ivResult Output 16-byte IV for AES
	 * 
	 * Combines base IV with seed in little-endian order, then computes
	 * HMAC-SHA1 and truncates to 16 bytes for AES block size.
	 */
	inline void generateIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &iv, const string &ivSeed, char* ivResult) {
		string concat;
		const size_t totalSize = iv.size() + 8;
		concat.reserve(totalSize);
		concat.assign(iv.begin(), iv.end());
		concat.resize(totalSize, '\0');

		const size_t ivSize = iv.size();
		if (ivSeed.size() == 4) {
			// Optimized: use pointer arithmetic instead of indexing
			char* dest = &concat[ivSize];
			dest[0] = ivSeed[3];
			dest[1] = ivSeed[2];
			dest[2] = ivSeed[1];
			dest[3] = ivSeed[0];
			// dest[4] to dest[7] already initialized to 0 by resize
		}
		else {
			// Optimized: use pointer arithmetic
			char* dest = &concat[ivSize];
			const char* src = ivSeed.data();
			dest[0] = src[7];
			dest[1] = src[6];
			dest[2] = src[5];
			dest[3] = src[4];
			dest[4] = src[3];
			dest[5] = src[2];
			dest[6] = src[1];
			dest[7] = src[0];
		}

		byte d[HMAC<SHA1>::DIGESTSIZE];
		{
			lock_guard<decltype(hmacLock)> lock(hmacLock);
			hmac.Update((const byte*)concat.data(), concat.size());
			hmac.Final(d);
		}

		memcpy(ivResult, d, 16);
	}

	/**
	 * @brief Encrypt or decrypt data block using AES-CBC with custom IV
	 * @param hmac HMAC for IV generation
	 * @param hmacLock Mutex for HMAC thread safety
	 * @param key AES encryption key
	 * @param iv Base initialization vector
	 * @param ivSeed Seed for IV generation
	 * @param cipher AES cipher instance (Encryption or Decryption mode)
	 * @param cipherLock Mutex for cipher thread safety
	 * @param data Input data to encrypt/decrypt
	 * @param result Output encrypted/decrypted data
	 * 
	 * Used for middle blocks in files where block boundary alignment is maintained.
	 */
	inline void blockCipher(HMAC<SHA1> &hmac, mutex &hmacLock, const string &key, const string &iv, const string &ivSeed, CipherModeBase &cipher, mutex &cipherLock, const string &data, string &result) {
		char ivSpec[16];
		generateIv(hmac, hmacLock, iv, ivSeed, ivSpec);

		{
			lock_guard<decltype(cipherLock)> lock(cipherLock);
			cipher.SetKeyWithIV((const byte*)key.data(), key.size(), (const byte*)ivSpec);
			StreamTransformationFilter dec(cipher, new StringSink(result), StreamTransformationFilter::ZEROS_PADDING);
			dec.Put((byte*)data.data(), data.size());
			dec.MessageEnd();
		}
	}

	/**
	 * @brief Encrypt data using AES-CFB stream cipher with shuffle and flip
	 * @param hmac HMAC for IV generation
	 * @param hmacLock Mutex for HMAC thread safety
	 * @param key AES encryption key
	 * @param iv Base initialization vector
	 * @param ivSeed Seed for IV generation
	 * @param cipher AES-CFB encryption cipher
	 * @param cipherLock Mutex for cipher thread safety
	 * @param data Plaintext data to encrypt
	 * @param result Output encrypted data
	 * 
	 * Uses two-pass encryption with byte shuffling and flipping for enhanced
	 * diffusion. Used for file tails and filenames where length is variable.
	 */
	inline void streamEncrypt(HMAC<SHA1> &hmac, mutex &hmacLock, const string &key, const string &iv, const string &ivSeed, CFB_Mode<AES>::Encryption &cipher, mutex &cipherLock, const string &data, string &result) {
		// AES / CFB / NoPadding
		string ivSeedPlusOne;
		incrementIvSeedByOne(ivSeed, ivSeedPlusOne);

		string firstEncResult;
		{
			string buf = data;

			// suffleBytes: XOR each byte with previous byte for diffusion
			for (size_t i = 0; i < buf.size() - 1; ++i) {
				buf[i + 1] ^= buf[i];
			}

			char ivSpec[16];
			generateIv(hmac, hmacLock, iv, ivSeed, ivSpec);
			{
				lock_guard<decltype(cipherLock)> lock(cipherLock);
				cipher.SetKeyWithIV((const byte*)key.data(), key.size(), (const byte*)ivSpec);
				StreamTransformationFilter dec(cipher, new StringSink(firstEncResult), StreamTransformationFilter::ZEROS_PADDING);
				dec.Put((byte*)buf.data(), buf.size());
				dec.MessageEnd();
			}
		}

		// flip bytes for additional diffusion
		string flipBytesResult;
		flipBytes(firstEncResult, flipBytesResult);

		// suffleBytes again
		for (size_t i = 0; i < flipBytesResult.size() - 1; ++i) {
			flipBytesResult[i + 1] ^= flipBytesResult[i];
		}

		// Second pass encryption with incremented IV
		{
			char ivSpec[16];
			generateIv(hmac, hmacLock, iv, ivSeedPlusOne, ivSpec);

			{
				lock_guard<decltype(cipherLock)> lock(cipherLock);
				cipher.SetKeyWithIV((const byte*)key.data(), key.size(), (const byte*)ivSpec);
				StreamTransformationFilter dec(cipher, new StringSink(result), StreamTransformationFilter::ZEROS_PADDING);
				dec.Put((byte*)flipBytesResult.data(), flipBytesResult.size());
				dec.MessageEnd();
			}
		}
	}

	/**
	 * @brief Decrypt data using AES-CFB stream cipher with unshuffle and flip
	 * @param hmac HMAC for IV generation
	 * @param hmacLock Mutex for HMAC thread safety
	 * @param key AES encryption key
	 * @param iv Base initialization vector
	 * @param ivSeed Seed for IV generation
	 * @param cipher AES-CFB decryption cipher
	 * @param cipherLock Mutex for cipher thread safety
	 * @param data Encrypted data to decrypt
	 * @param result Output decrypted plaintext
	 * 
	 * Reverses the streamEncrypt process: decrypt twice, flip bytes, and
	 * unshuffle (reverse XOR operations).
	 */
	inline void streamDecrypt(HMAC<SHA1> &hmac, mutex &hmacLock, const string &key, const string &iv, const string &ivSeed, CFB_Mode<AES>::Decryption &cipher, mutex &cipherLock, const string &data, string &result) {
		// AES / CFB / NoPadding

		string firstDecResult;
		{
			string ivSeedPlusOne;
			incrementIvSeedByOne(ivSeed, ivSeedPlusOne);

			char ivSpec[16];
			generateIv(hmac, hmacLock, iv, ivSeedPlusOne, ivSpec);

			{
				lock_guard<decltype(cipherLock)> lock(cipherLock);
				cipher.SetKeyWithIV((const byte*)key.data(), key.size(), (const byte*)ivSpec);
				StreamTransformationFilter dec(cipher, new StringSink(firstDecResult), StreamTransformationFilter::ZEROS_PADDING);
				dec.Put((byte*)data.data(), data.size());
				dec.MessageEnd();
			}
		}

		// unsuffleBytes: reverse XOR in backward order
		for (size_t i = (firstDecResult.size() - 1); i > 0; i--) {
			firstDecResult[i] ^= firstDecResult[i - 1];
		}

		// flip bytes
		string flipBytesResult;
		flipBytes(firstDecResult, flipBytesResult);

		// Second pass decryption
		{
			char ivSpec[16];
			generateIv(hmac, hmacLock, iv, ivSeed, ivSpec);

			{
				lock_guard<decltype(cipherLock)> lock(cipherLock);
				cipher.SetKeyWithIV((const byte*)key.data(), key.size(), (const byte*)ivSpec);
				StreamTransformationFilter dec(cipher, new StringSink(result), StreamTransformationFilter::ZEROS_PADDING);
				dec.Put((byte*)flipBytesResult.data(), flipBytesResult.size());
				dec.MessageEnd();
			}
		}

		// unsuffleBytes again
		for (size_t i = (result.size() - 1); i > 0; i--) {
			result[i] ^= result[i - 1];
		}
	}

	/**
	 * @brief Calculate 64-bit MAC using HMAC-SHA1
	 * @param hmac HMAC-SHA1 instance
	 * @param hmacLock Mutex for thread safety
	 * @param data Input data to authenticate
	 * @param len Data length in bytes
	 * @param mac Output 8-byte MAC
	 * 
	 * Computes HMAC-SHA1 (20 bytes) and folds it into 8 bytes by XORing.
	 * Uses only first 19 bytes (not 20) to match original EncFS behavior.
	 */
	inline void mac64(HMAC<SHA1> &hmac, mutex &hmacLock, const byte* data, const size_t len, char* mac) {
		byte macResult[HMAC<SHA1>::DIGESTSIZE];
		{
			lock_guard<decltype(hmacLock)> lock(hmacLock);
			hmac.Update((const byte*)data, len);
			hmac.Final(macResult);
		}

		for (size_t i = 0; i < 8; ++i) {
			mac[i] = 0;
		}
		for (size_t i = 0; i < 19; i++) {
			// Note: 19, not 20 (original EncFS behavior - DO NOT CHANGE for compatibility)
			mac[i % 8] ^= macResult[i];
		}
	}

	/**
	 * @brief Calculate 64-bit MAC with chain IV appended
	 * @param hmac HMAC-SHA1 instance
	 * @param hmacLock Mutex for thread safety
	 * @param data Input data
	 * @param dataLen Length of the input data
	 * @param chainIv 8-byte chain IV to append in little-endian order
	 * @param mac Output 8-byte MAC
	 * 
	 * Used for chained IV mode where directory path affects encryption.
	 */
	inline void mac64withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const char *data, size_t dataLen, const char *chainIv, char* mac) {
		const size_t totalSize = dataLen + 8;
		
		string concat;
		concat.reserve(totalSize);
		concat.append(data, dataLen);
		concat.resize(totalSize);
		
		// Optimized: direct pointer access
		char* dest = &concat[dataLen];
		dest[0] = chainIv[7];
		dest[1] = chainIv[6];
		dest[2] = chainIv[5];
		dest[3] = chainIv[4];
		dest[4] = chainIv[3];
		dest[5] = chainIv[2];
		dest[6] = chainIv[1];
		dest[7] = chainIv[0];

		mac64(hmac, hmacLock, (const byte*)concat.data(), concat.size(), mac);
	}

	/**
	 * @brief Calculate 64-bit MAC with chain IV appended
	 * @param hmac HMAC-SHA1 instance
	 * @param hmacLock Mutex for thread safety
	 * @param data Input data
	 * @param chainIv 8-byte chain IV to append in little-endian order
	 * @param mac Output 8-byte MAC
	 * 
	 * Used for chained IV mode where directory path affects encryption.
	 */
	inline void mac64withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, const char *chainIv, char* mac) {
		mac64withIv(hmac, hmacLock, data.data(), data.size(), chainIv, mac);
	}

	/** Calculate 32-bit MAC by folding 64-bit MAC */
	inline void mac32(HMAC<SHA1> &hmac, mutex &hmacLock, const char* data, size_t len, char* mac) {
		char mac8b[8];
		mac64(hmac, hmacLock, (const byte*)data, len, mac8b);
		mac[0] = (mac8b[4] ^ mac8b[0]);
		mac[1] = (mac8b[5] ^ mac8b[1]);
		mac[2] = (mac8b[6] ^ mac8b[2]);
		mac[3] = (mac8b[7] ^ mac8b[3]);
	}

	/** Calculate 32-bit MAC by folding 64-bit MAC */
	inline void mac32(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, char* mac) {
		mac32(hmac, hmacLock, data.data(), data.size(), mac);
	}

	/** Calculate 32-bit MAC with chain IV */
	inline void mac32withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const char* data, size_t len, const char *chainIv, char* mac) {
		char mac8b[8];
		mac64withIv(hmac, hmacLock, data, len, chainIv, mac8b);
		mac[0] = (mac8b[4] ^ mac8b[0]);
		mac[1] = (mac8b[5] ^ mac8b[1]);
		mac[2] = (mac8b[6] ^ mac8b[2]);
		mac[3] = (mac8b[7] ^ mac8b[3]);
	}

	/** Calculate 32-bit MAC with chain IV */
	inline void mac32withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, const char *chainIv, char* mac) {
		mac32withIv(hmac, hmacLock, data.data(), data.size(), chainIv, mac);
	}

	/** Calculate 16-bit MAC by folding 32-bit MAC with chain IV */
	inline void mac16withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const char* data, size_t len, const char *chainIv, char* mac) {
		char mac4b[4];
		mac32withIv(hmac, hmacLock, data, len, chainIv, mac4b);
		mac[0] = (mac4b[2] ^ mac4b[0]);
		mac[1] = (mac4b[3] ^ mac4b[1]);
	}

	/** Calculate 16-bit MAC by folding 32-bit MAC with chain IV */
	inline void mac16withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, const char *chainIv, char* mac) {
		mac16withIv(hmac, hmacLock, data.data(), data.size(), chainIv, mac);
	}

	/** Calculate 16-bit MAC by folding 32-bit MAC */
	inline void mac16(HMAC<SHA1> &hmac, mutex &hmacLock, const char* data, size_t len, char* mac) {
		char mac4b[4];
		mac32(hmac, hmacLock, data, len, mac4b);
		mac[0] = (mac4b[2] ^ mac4b[0]);
		mac[1] = (mac4b[3] ^ mac4b[1]);
	}

	/** Calculate 16-bit MAC by folding 32-bit MAC */
	inline void mac16(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, char* mac) {
		mac16(hmac, hmacLock, data.data(), data.size(), mac);
	}

	/**
	 * @brief Compute chain IV from directory path
	 * @param hmac HMAC-SHA1 instance
	 * @param hmacLock Mutex for thread safety
	 * @param filePath Full plaintext file path
	 * @param chainIv Output 8-byte chain IV
	 * 
	 * Iteratively computes MAC for each path component from root to file,
	 * chaining the result. Used for directory-dependent encryption where
	 * moving a file to a different directory requires re-encryption.
	 * 
	 * Each path component is padded to 16-byte boundary with PKCS#7 padding.
	 */
	inline void computeChainIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &filePath, char* chainIv) {
		memset(chainIv, 0, 8);

		string::size_type pos1 = 0;
		string::size_type pos2;
		
		string encodeBytes;
		encodeBytes.reserve(256); // Pre-allocate reasonable buffer size
		
		do {
			pos2 = filePath.find(g_pathSeparator, pos1);
			if (pos2 == string::npos) {
				pos2 = filePath.size();
			}
			if (pos2 > pos1) {
				string curPath = filePath.substr(pos1, pos2 - pos1);

				// Apply PKCS#7 padding to 16-byte blocks
				const size_t byteLen = curPath.size();
				const size_t padLen = (byteLen % 16 == 0) ? 16 : (16 - (byteLen % 16));
				const size_t totalLen = byteLen + padLen;
				
				encodeBytes.clear();
				encodeBytes.reserve(totalLen);
				encodeBytes.assign(curPath.begin(), curPath.end());
				encodeBytes.resize(totalLen, static_cast<char>(padLen));

				// Chain MAC: use previous chainIv as input
				mac64withIv(hmac, hmacLock, encodeBytes, chainIv, chainIv);
			}
			pos1 = pos2 + 1;
		} while (pos2 != filePath.size());
	}
}