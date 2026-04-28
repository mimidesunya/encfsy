#pragma once

#include <string>
#include <mutex>

#include <aes.h>
#include <modes.h>
#include <sha.h>
#include <hmac.h>
#include <secblock.h>

namespace EncFS {
    struct FileCodecContext {
        CryptoPP::HMAC<CryptoPP::SHA1>& volumeHmac;
        std::mutex& hmacLock;
        const CryptoPP::SecByteBlock& volumeKey;
        const CryptoPP::SecByteBlock& volumeIv;
        CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption& aesCbcEnc;
        std::mutex& aesCbcEncLock;
        CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption& aesCbcDec;
        std::mutex& aesCbcDecLock;
        CryptoPP::CFB_Mode<CryptoPP::AES>::Encryption& aesCfbEnc;
        std::mutex& aesCfbEncLock;
        CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption& aesCfbDec;
        std::mutex& aesCfbDecLock;
        bool uniqueIV;
        bool externalIVChaining;
        bool allowHoles;
        int32_t blockSize;
        int32_t blockMACBytes;
        int32_t blockMACRandBytes;
    };

    int64_t ToDecodedLength(const FileCodecContext& context, int64_t encodedLength);
    int64_t ToEncodedLength(const FileCodecContext& context, int64_t decodedLength);
    void EncodeFileIv(const FileCodecContext& context, const std::string& plainFilePath, int64_t fileIv, std::string& encodedFileHeader);
    int64_t DecodeFileIv(const FileCodecContext& context, const std::string& plainFilePath, const std::string& encodedFileHeader);
    void EncodeBlock(const FileCodecContext& context, int64_t fileIv, int64_t blockNum, const std::string& plainBlock, std::string& encodedBlock);
    void DecodeBlock(const FileCodecContext& context, int64_t fileIv, int64_t blockNum, const std::string& encodedBlock, std::string& plainBlock);
}
