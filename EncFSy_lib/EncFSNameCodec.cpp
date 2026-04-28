#include "EncFSNameCodec.hpp"
#include "EncFSUtils.hpp"

#include <misc.h>

using namespace std;
using namespace CryptoPP;

namespace EncFS {
    namespace {
        constexpr int IV_SPEC_SIZE = 16;
        constexpr int CHAIN_IV_SIZE = 8;
        constexpr int MAC_16_SIZE = 2;

        uint64_t readUint64BE(const char* value) {
            uint64_t result = 0;
            for (size_t i = 0; i < 8; ++i) {
                result = (result << 8) | static_cast<unsigned char>(value[i]);
            }
            return result;
        }

        unsigned int mac16ToUInt(const char* mac) {
            return (static_cast<unsigned int>(static_cast<unsigned char>(mac[0])) << 8)
                | static_cast<unsigned int>(static_cast<unsigned char>(mac[1]));
        }

      template <typename TCipher>
        void processBlockFileName(const NameCodecContext& context,
            TCipher& cipher,
            std::mutex& cipherLock,
            const std::string& fileIv,
            const std::string& input,
            std::string& output) {
            byte ivSpec[IV_SPEC_SIZE];
            generateIv(context.volumeHmac, context.hmacLock,
                string((const char*)context.volumeIv.data(), context.volumeIv.size()),
                fileIv,
                (char*)ivSpec);

            lock_guard<mutex> lock(cipherLock);
            cipher.SetKeyWithIV(context.volumeKey, context.volumeKey.size(), ivSpec);
            StreamTransformationFilter filter(cipher, new StringSink(output), StreamTransformationFilter::ZEROS_PADDING);
            filter.Put((const byte*)input.data(), input.size());
            filter.MessageEnd();
        }

        void computeStreamChainIv(const NameCodecContext& context, const string& filePath, char* chainIv) {
            if (!context.chainedNameIV) {
                memset(chainIv, 0, CHAIN_IV_SIZE);
                return;
            }

            memset(chainIv, 0, CHAIN_IV_SIZE);
            string::size_type pos1 = 0;
            string::size_type pos2;
            do {
                pos2 = filePath.find(g_pathSeparator, pos1);
                if (pos2 == string::npos) {
                    pos2 = filePath.size();
                }
                if (pos2 > pos1) {
                    const string component = filePath.substr(pos1, pos2 - pos1);
                    if (component != "." && component != "..") {
                        mac64withIv(context.volumeHmac, context.hmacLock, component, chainIv, chainIv);
                    }
                }
                pos1 = pos2 + 1;
            } while (pos2 != filePath.size());
        }

        void encodeStreamFileName(const NameCodecContext& context,
            const string& plainFileName,
            const string& plainDirPath,
            string& encodedFileName) {
            char chainIv[CHAIN_IV_SIZE];
            computeStreamChainIv(context, plainDirPath, chainIv);

            uint64_t tmpIv = 0;
            if (context.chainedNameIV && context.nameAlgorithmMajor >= 2) {
                tmpIv = readUint64BE(chainIv);
            }

            char mac[MAC_16_SIZE];
            if (context.chainedNameIV) {
                mac16withIv(context.volumeHmac, context.hmacLock, plainFileName, chainIv, mac);
            }
            else {
                mac16(context.volumeHmac, context.hmacLock, plainFileName, mac);
            }

            string ivSeed;
            longToBytesByBE(ivSeed, static_cast<int64_t>(mac16ToUInt(mac) ^ tmpIv));

            string body;
            streamEncrypt(
                context.volumeHmac, context.hmacLock,
                string((const char*)context.volumeKey.data(), context.volumeKey.size()),
                string((const char*)context.volumeIv.data(), context.volumeIv.size()),
                ivSeed,
                context.aesCfbEnc, context.aesCfbEncLock,
                plainFileName, body);

            string raw;
            raw.reserve(body.size() + MAC_16_SIZE);
            if (context.nameAlgorithmMajor >= 1) {
                raw.append(mac, MAC_16_SIZE);
                raw.append(body);
            }
            else {
                raw.append(body);
                raw.append(mac, MAC_16_SIZE);
            }

            encodeBase64FileName(raw, encodedFileName);
        }

        bool tryDecodeStreamFileName(const NameCodecContext& context,
            const string& encodedFileName,
            const string& plainDirPath,
            const int* base64Lookup,
            string& plainFileName) {
            string raw;
            if (!decodeBase64FileName(base64Lookup, encodedFileName, raw) || raw.size() <= MAC_16_SIZE) {
                return false;
            }

            char chainIv[CHAIN_IV_SIZE];
            computeStreamChainIv(context, plainDirPath, chainIv);

            char storedMac[MAC_16_SIZE];
            string body;
            if (context.nameAlgorithmMajor >= 1) {
                storedMac[0] = raw[0];
                storedMac[1] = raw[1];
                body.assign(raw.data() + MAC_16_SIZE, raw.size() - MAC_16_SIZE);
            }
            else {
                storedMac[0] = raw[raw.size() - MAC_16_SIZE];
                storedMac[1] = raw[raw.size() - 1];
                body.assign(raw.data(), raw.size() - MAC_16_SIZE);
            }

            uint64_t tmpIv = 0;
            if (context.chainedNameIV && context.nameAlgorithmMajor >= 2) {
                tmpIv = readUint64BE(chainIv);
            }

            string ivSeed;
            longToBytesByBE(ivSeed, static_cast<int64_t>(mac16ToUInt(storedMac) ^ tmpIv));

            string decoded;
            streamDecrypt(
                context.volumeHmac, context.hmacLock,
                string((const char*)context.volumeKey.data(), context.volumeKey.size()),
                string((const char*)context.volumeIv.data(), context.volumeIv.size()),
                ivSeed,
                context.aesCfbDec, context.aesCfbDecLock,
                body, decoded);

            char computedMac[MAC_16_SIZE];
            if (context.chainedNameIV) {
                mac16withIv(context.volumeHmac, context.hmacLock, decoded, chainIv, computedMac);
            }
            else {
                mac16(context.volumeHmac, context.hmacLock, decoded, computedMac);
            }

            if (!VerifyBufsEqual((const byte*)storedMac, (const byte*)computedMac, MAC_16_SIZE)) {
                return false;
            }

            plainFileName = decoded;
            return true;
        }
    }

    void EncodeFileName(const NameCodecContext& context,
        const string& plainFileName,
        const string& plainDirPath,
        const int* /*base64Lookup*/,
        string& encodedFileName) {
        if (plainFileName == "." || plainFileName == "..") {
            encodedFileName.append(plainFileName);
            return;
        }

        if (context.useStreamAlgorithm) {
            encodeStreamFileName(context, plainFileName, plainDirPath, encodedFileName);
            return;
        }

        size_t padLen = IV_SPEC_SIZE - (plainFileName.size() % IV_SPEC_SIZE);
        char chainIv[CHAIN_IV_SIZE];
        if (context.chainedNameIV) {
            computeChainIv(context.volumeHmac, context.hmacLock, plainDirPath, chainIv);
        }
        else {
            memset(chainIv, 0, CHAIN_IV_SIZE);
        }

        string paddedFileName = plainFileName;
        paddedFileName.append(padLen, (char)padLen);

        char iv[MAC_16_SIZE];
        if (context.chainedNameIV) {
            mac16withIv(context.volumeHmac, context.hmacLock, paddedFileName, chainIv, iv);
        }
        else {
            mac16(context.volumeHmac, context.hmacLock, paddedFileName, iv);
        }

        string fileIv(CHAIN_IV_SIZE, '\0');
        memcpy(&fileIv[0], chainIv, CHAIN_IV_SIZE);
        fileIv[6] ^= iv[0];
        fileIv[7] ^= iv[1];

        string binFileName;
        processBlockFileName(context, context.aesCbcEnc, context.aesCbcEncLock, fileIv, paddedFileName, binFileName);
        binFileName.insert(0, iv, MAC_16_SIZE);
        encodeBase64FileName(binFileName, encodedFileName);
    }

    bool TryDecodeFileName(const NameCodecContext& context,
        const string& encodedFileName,
        const string& plainDirPath,
        const int* base64Lookup,
        string& plainFileName) {
        plainFileName.clear();
        if (encodedFileName == "." || encodedFileName == "..") {
            plainFileName.append(encodedFileName);
            return true;
        }

        if (context.useStreamAlgorithm) {
            return tryDecodeStreamFileName(context, encodedFileName, plainDirPath, base64Lookup, plainFileName);
        }

        string binFileName;
        if (!decodeBase64FileName(base64Lookup, encodedFileName, binFileName)) {
            return false;
        }
        if (binFileName.size() < MAC_16_SIZE + context.aesCbcDec.MandatoryBlockSize()) {
            return false;
        }

        char chainIv[CHAIN_IV_SIZE];
        if (context.chainedNameIV) {
            computeChainIv(context.volumeHmac, context.hmacLock, plainDirPath, chainIv);
        }
        else {
            memset(chainIv, 0, CHAIN_IV_SIZE);
        }

        string fileIv(CHAIN_IV_SIZE, '\0');
        memcpy(&fileIv[0], chainIv, CHAIN_IV_SIZE);
        fileIv[6] ^= binFileName[0];
        fileIv[7] ^= binFileName[1];

        char iv1[MAC_16_SIZE];
        iv1[0] = binFileName[0];
        iv1[1] = binFileName[1];
        binFileName.erase(0, MAC_16_SIZE);

        string plainCandidate;
        processBlockFileName(context, context.aesCbcDec, context.aesCbcDecLock, fileIv, binFileName, plainCandidate);

        char iv2[MAC_16_SIZE];
        if (context.chainedNameIV) {
            mac16withIv(context.volumeHmac, context.hmacLock, plainCandidate.data(), plainCandidate.size(), chainIv, iv2);
        }
        else {
            mac16(context.volumeHmac, context.hmacLock, plainCandidate.data(), plainCandidate.size(), iv2);
        }
        if (!VerifyBufsEqual((const byte*)iv1, (const byte*)iv2, MAC_16_SIZE)) {
            return false;
        }

        if (plainCandidate.empty()) {
            return false;
        }
        const size_t padLen = (unsigned char)plainCandidate.back();
        if (padLen == 0 || padLen > IV_SPEC_SIZE || padLen > plainCandidate.size()) {
            return false;
        }
        for (size_t i = 0; i < padLen; ++i) {
            if ((unsigned char)plainCandidate[plainCandidate.size() - padLen + i] != padLen) {
                return false;
            }
        }
        plainCandidate.erase(plainCandidate.size() - padLen);
        plainFileName = plainCandidate;
        return true;
    }
}
