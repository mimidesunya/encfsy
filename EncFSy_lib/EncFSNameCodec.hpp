#pragma once

#include <string>
#include <mutex>

#include <aes.h>
#include <modes.h>
#include <sha.h>
#include <hmac.h>
#include <secblock.h>

namespace EncFS {
    struct NameCodecContext {
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
        bool chainedNameIV;
        bool useStreamAlgorithm;
        int32_t nameAlgorithmMajor;
    };

    void EncodeFileName(const NameCodecContext& context,
        const std::string& plainFileName,
        const std::string& plainDirPath,
        const int* base64Lookup,
        std::string& encodedFileName);

    bool TryDecodeFileName(const NameCodecContext& context,
        const std::string& encodedFileName,
        const std::string& plainDirPath,
        const int* base64Lookup,
        std::string& plainFileName);
}
