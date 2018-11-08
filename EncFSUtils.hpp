#pragma once

#include <string>
#include <mutex>

#include <modes.h>
#include <pwdbased.h>
#include <sha.h>
#include <osrng.h>
#include <base64.h>

using namespace std;
using namespace CryptoPP;

namespace EncFS
{
	/**
	Path separator of this platform.
	*/
	static const string g_pathSeparator("\\");

	/**
	Apply left trim.
	*/
	static inline void ltrim(string &s) {
		s.erase(s.begin(), find_if(s.begin(), s.end(), [](int ch) {
			return !isspace(ch);
		}));
	}

	/**
	Apply right trim.
	*/
	static inline void rtrim(string &s) {
		s.erase(find_if(s.rbegin(), s.rend(), [](int ch) {
			return !isspace(ch);
		}).base(), s.end());
	}

	/**
	Trim both side.
	*/
	static inline void trim(string &s) {
		ltrim(s);
		rtrim(s);
	}

	/**
	Characters for a variant of base64.
	*/
	static const byte ALPHABET[] = ",-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

	/**
	Decode base64 encoded file name.
	*/
	inline void decodeBase64FileName(const int* lookup, const string &encodedName, string &decodedName) {
		string in;
		in.resize(encodedName.size());
		for (size_t i = 0; i < encodedName.size(); i++) {
			if (lookup[encodedName[i]] == -1) {
				return;
			}
			in[i] = (char)lookup[encodedName[i]];
		}

		size_t srcIdx = 0;
		int workBits = 0;
		unsigned int work = 0;
		while (srcIdx < in.size()) {
			work |= in[srcIdx++] << workBits;
			workBits += 6;

			while (workBits >= 8) {
				decodedName.append(1, work & 0xff);
				work >>= 8;
				workBits -= 8;
			}
		}
	}

	/**
	Encode binary string to base64 file name.
	*/
	inline void encodeBase64FileName(const string &in, string &out) {
		size_t outSize = in.size() * 8 / 6 + ((in.size() * 8 % 6) == 0 ? 0 : 1);
		long mask = (1 << 6) - 1;
		int workingBits = 0;
		long work = 0;
		for (int i = 0; i < in.size(); ++i) {
			int unsignedIntValue = in[i] & 0xFF;
			work |= unsignedIntValue << workingBits;

			workingBits += 8;

			while (workingBits > 6) {
				out.append(1, work & (mask & 0xFF));
				work >>= 6;
				workingBits -= 6;
			}
		}

		if (workingBits > 0) {
			out.append(1, work & (mask & 0xFF));
		}

		for (size_t i = 0; i < outSize; ++i) {
			size_t ii = out.size() - i - 1;
			out[ii] = ALPHABET[out[ii]];
		}
	}

	/**
	Pack 4byte string to 32bit int.
	*/
	inline int32_t bytesToIntByBE(const string &bytes) {
		int32_t num = (int32_t)bytes[3] & 0xFF;
		num |= ((int32_t)bytes[2] & 0xFF) << 8;
		num |= ((int32_t)bytes[1] & 0xFF) << 16;
		num |= ((int32_t)bytes[0] & 0xFF) << 24;
		return num;
	}

	/**
	Pack 8byte string to 64bit int.
	*/
	inline int64_t bytesToLongByBE(const string &bytes) {
		int64_t num = (int64_t)bytes[7] & 0xFF;
		num |= ((int64_t)bytes[6] & 0xFF) << 8;
		num |= ((int64_t)bytes[5] & 0xFF) << 16;
		num |= ((int64_t)bytes[4] & 0xFF) << 24;
		num |= ((int64_t)bytes[3] & 0xFF) << 32;
		num |= ((int64_t)bytes[2] & 0xFF) << 40;
		num |= ((int64_t)bytes[1] & 0xFF) << 48;
		num |= ((int64_t)bytes[0] & 0xFF) << 56;
		return num;
	}

	/**
	Unpack 32bit int to 4byte string.
	*/
	inline void intToBytesByBE(string &bytes, int32_t num) {
		bytes.resize(4);
		bytes[0] = (num >> 24) & 0xFF;
		bytes[1] = (num >> 16) & 0xFF;
		bytes[2] = (num >> 8) & 0xFF;
		bytes[3] = num & 0xFF;
	}

	/**
	Unpack 64bit int to 8byte string.
	*/
	inline void longToBytesByBE(string &bytes, int64_t num) {
		bytes.resize(8);
		bytes[0] = (num >> 56) & 0xFF;
		bytes[1] = (num >> 48) & 0xFF;
		bytes[2] = (num >> 40) & 0xFF;
		bytes[3] = (num >> 32) & 0xFF;
		bytes[4] = (num >> 24) & 0xFF;
		bytes[5] = (num >> 16) & 0xFF;
		bytes[6] = (num >> 8) & 0xFF;
		bytes[7] = num & 0xFF;
	}

	/**
	Add one to 4byte or 8byte initialization vector.
	*/
	inline void incrementIvSeedByOne(const string ivSeed, string &ivSeedPlusOne) {
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
	Flip binary string.
	*/
	inline void flipBytes(const string src, string &dest) {
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
	Generate initialization vector.
	*/
	inline void generateIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &iv, const string &ivSeed, char* ivResult) {
		string concat;
		concat.insert(concat.begin(), iv.begin(), iv.end());
		concat.resize(iv.size() + 8);

		if (ivSeed.size() == 4) {
			for (int i = 0; i < 4; ++i) {
				concat[iv.size() + i] = ivSeed[3 - i];
			}
			for (int i = 4; i < 8; ++i) {
				concat[iv.size() + i] = 0;
			}
		}
		else {
			for (int i = 0; i < 8; ++i) {
				concat[iv.size() + i] = ivSeed[7 - i];
			}
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
	Encrypt or decrypt a block of middle in file.
	*/
	inline void blockCipher(HMAC<SHA1> &hmac, mutex &hmacLock, const string key, const string iv, const string ivSeed, CipherModeBase &cipher, mutex &cipherLock, const string data, string &result) {
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
	Encrypt tail of file or file name.
	*/
	inline void streamEncrypt(HMAC<SHA1> &hmac, mutex &hmacLock, const string key, const string iv, const string ivSeed, CFB_Mode<AES>::Encryption &cipher, mutex &cipherLock, const string data, string &result) {
		// AES / CFB / NoPadding
		string ivSeedPlusOne;
		incrementIvSeedByOne(ivSeed, ivSeedPlusOne);

		string firstEncResult;
		{
			string buf = data;

			// suffleBytes
			for (int i = 0; i < buf.size() - 1; ++i) {
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

		//flip  bytes
		string flipBytesResult;
		flipBytes(firstEncResult, flipBytesResult);

		// suffleBytes
		for (int i = 0; i < flipBytesResult.size() - 1; ++i) {
			flipBytesResult[i + 1] ^= flipBytesResult[i];
		}

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
	Decrypt tail of file or file name.
	*/
	inline void streamDecrypt(HMAC<SHA1> &hmac, mutex &hmacLock, const string key, const string iv, const string ivSeed, CFB_Mode<AES>::Decryption &cipher, mutex &cipherLock, const string data, string &result) {
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

		// unsuffleBytes
		for (size_t i = (firstDecResult.size() - 1); i > 0; i--) {
			firstDecResult[i] ^= firstDecResult[i - 1];
		}

		//flip  bytes
		string flipBytesResult;
		flipBytes(firstDecResult, flipBytesResult);

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

		// unsuffleBytes
		for (size_t i = (result.size() - 1); i > 0; i--) {
			result[i] ^= result[i - 1];
		}
	}

	/**
	Calculate 64bit message authentication code.
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
			// Note the 19 not 20
			mac[i % 8] ^= macResult[i];
		}
	}

	/**
	Calculate 64bit message authentication code with initialization vector.
	*/
	inline void mac64withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, const char *chainIv, char* mac) {
		string concat;
		concat.insert(concat.begin(), data.begin(), data.end());
		concat.resize(data.size() + 8);
		for (size_t i = data.size(); i < data.size() + 8; i++) {
			concat[i] = chainIv[7 - (i - data.size())];
		}

		mac64(hmac, hmacLock, (const byte*)concat.data(), concat.size(), mac);
	}

	/**
	Calculate 32bit message authentication code.
	*/
	inline void mac32(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, char* mac) {
		char mac8b[8];
		mac64(hmac, hmacLock, (const byte*)data.data(), data.size(), mac8b);
		mac[0] = (mac8b[4] ^ mac8b[0]);
		mac[1] = (mac8b[5] ^ mac8b[1]);
		mac[2] = (mac8b[6] ^ mac8b[2]);
		mac[3] = (mac8b[7] ^ mac8b[3]);
	}

	/**
	Calculate 32bit message authentication code with initialization vector.
	*/
	inline void mac32withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, const char *chainIv, char* mac) {
		char mac8b[8];
		mac64withIv(hmac, hmacLock, data, chainIv, mac8b);
		mac[0] = (mac8b[4] ^ mac8b[0]);
		mac[1] = (mac8b[5] ^ mac8b[1]);
		mac[2] = (mac8b[6] ^ mac8b[2]);
		mac[3] = (mac8b[7] ^ mac8b[3]);
	}

	/**
	Calculate 16bit message authentication code with initialization vector.
	*/
	inline void mac16withIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, const char *chainIv, char* mac) {
		char mac4b[4];
		mac32withIv(hmac, hmacLock, data, chainIv, mac4b);
		mac[0] = (mac4b[2] ^ mac4b[0]);
		mac[1] = (mac4b[3] ^ mac4b[1]);
	}

	/**
	Calculate 16bit message authentication code.
	*/
	inline void mac16(HMAC<SHA1> &hmac, mutex &hmacLock, const string &data, char* mac) {
		char mac4b[4];
		mac32(hmac, hmacLock, data, mac4b);
		mac[0] = (mac4b[2] ^ mac4b[0]);
		mac[1] = (mac4b[3] ^ mac4b[1]);
	}

	/**
	Calculate initialization vector from plain file path string.
	*/
	inline void computeChainIv(HMAC<SHA1> &hmac, mutex &hmacLock, const string &filePath, char* chainIv) {
		for (int i = 0; i < 8; ++i) {
			chainIv[i] = 0;
		}

		string::size_type pos1 = 0;
		string::size_type pos2;
		do {
			pos2 = filePath.find(g_pathSeparator, pos1);
			if (pos2 == string::npos) {
				pos2 = filePath.size();
			}
			if (pos2 > pos1) {
				string curPath = filePath.substr(pos1, pos2 - pos1);

				// getBytesForBlockAlgorithm
				string encodeBytes;
				size_t byteLen = curPath.size();
				int padLen = 16 - (byteLen % 16);
				if (padLen == 0) {
					padLen = 16;
				}
				encodeBytes.resize(byteLen + padLen);
				for (int i = 0; i < byteLen; i++) {
					encodeBytes[i] = curPath[i];
				}
				for (int i = 0; i < padLen; i++) {
					encodeBytes[byteLen + i] = (char)padLen;
				}

				// Mac64
				mac64withIv(hmac, hmacLock, encodeBytes, chainIv, chainIv);
			}
			pos1 = pos2 + 1;
		} while (pos2 != filePath.size());
	}
}