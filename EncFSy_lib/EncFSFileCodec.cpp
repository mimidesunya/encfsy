#include "EncFSFileCodec.hpp"
#include "EncFSVolume.h"
#include "EncFSUtils.hpp"

#include <misc.h>

using namespace std;
using namespace CryptoPP;

namespace EncFS {
    namespace {
        constexpr int HEADER_SIZE = 8;
        constexpr int CHAIN_IV_SIZE = 8;

        inline int32_t getHeaderSize(const FileCodecContext& context) {
            return context.blockMACRandBytes + context.blockMACBytes;
        }

        template <typename TCipher>
        void codeBlockCipher(const FileCodecContext& context,
            const string& blockIv,
            TCipher& cipher,
            mutex& cipherLock,
            const string& input,
            string& output) {
            if (input.size() == static_cast<size_t>(context.blockSize)) {
                blockCipher(
                    context.volumeHmac, context.hmacLock,
                    string((const char*)context.volumeKey.data(), context.volumeKey.size()),
                    string((const char*)context.volumeIv.data(), context.volumeIv.size()),
                    blockIv, cipher, cipherLock,
                    input, output);
            }
        }

        void computeExternalChainIv(const FileCodecContext& context, const string& plainFilePath, char* initIvBytes) {
            if (context.externalIVChaining) {
                computeChainIv(context.volumeHmac, context.hmacLock, plainFilePath, initIvBytes);
            }
            else {
                memset(initIvBytes, 0, CHAIN_IV_SIZE);
            }
        }

        void codeBlock(const FileCodecContext& context,
            int64_t fileIv,
            int64_t blockNum,
            bool encode,
            const string& srcBlock,
            string& destBlock) {
            const int64_t iv = blockNum ^ fileIv;
            const size_t headerSize = static_cast<size_t>(getHeaderSize(context));

            if (encode) {
                if (context.allowHoles && srcBlock.size() + headerSize == static_cast<size_t>(context.blockSize)) {
                    const char* p = srcBlock.data();
                    size_t size = srcBlock.size();
                    if (size > 0 && p[0] == 0 && memcmp(p, p + 1, size - 1) == 0) {
                        destBlock.assign(context.blockSize, (char)0);
                        return;
                    }
                }

                string block;
                block.reserve(headerSize + srcBlock.size());
                block.resize(headerSize);
                block.append(srcBlock);

                byte mac[8];
                mac64(context.volumeHmac, context.hmacLock, (const byte*)srcBlock.data(), srcBlock.size(), (char*)mac);
                for (size_t i = 0; i < static_cast<size_t>(context.blockMACBytes); ++i) {
                    block[i] = mac[7 - i];
                }

                string blockIv;
                longToBytesByBE(blockIv, iv);
                if (block.size() == static_cast<size_t>(context.blockSize)) {
                    blockCipher(
                        context.volumeHmac, context.hmacLock,
                        string((const char*)context.volumeKey.data(), context.volumeKey.size()),
                        string((const char*)context.volumeIv.data(), context.volumeIv.size()),
                        blockIv, context.aesCbcEnc, context.aesCbcEncLock,
                        block, destBlock);
                }
                else {
                    streamEncrypt(
                        context.volumeHmac, context.hmacLock,
                        string((const char*)context.volumeKey.data(), context.volumeKey.size()),
                        string((const char*)context.volumeIv.data(), context.volumeIv.size()),
                        blockIv, context.aesCfbEnc, context.aesCfbEncLock,
                        block, destBlock);
                }
                return;
            }

            if (context.allowHoles && srcBlock.size() == static_cast<size_t>(context.blockSize)) {
                const char* p = srcBlock.data();
                size_t size = srcBlock.size();
                if (size > 0 && p[0] == 0 && memcmp(p, p + 1, size - 1) == 0) {
                    destBlock.assign(context.blockSize - headerSize, (char)0);
                    return;
                }
            }

            string blockIv;
            longToBytesByBE(blockIv, iv);
            if (srcBlock.size() == static_cast<size_t>(context.blockSize)) {
                blockCipher(
                    context.volumeHmac, context.hmacLock,
                    string((const char*)context.volumeKey.data(), context.volumeKey.size()),
                    string((const char*)context.volumeIv.data(), context.volumeIv.size()),
                    blockIv, context.aesCbcDec, context.aesCbcDecLock,
                    srcBlock, destBlock);
            }
            else {
                streamDecrypt(
                    context.volumeHmac, context.hmacLock,
                    string((const char*)context.volumeKey.data(), context.volumeKey.size()),
                    string((const char*)context.volumeIv.data(), context.volumeIv.size()),
                    blockIv, context.aesCfbDec, context.aesCfbDecLock,
                    srcBlock, destBlock);
            }

            if (destBlock.size() < headerSize) {
                throw EncFSInvalidBlockException();
            }

            byte mac[8];
            mac64(context.volumeHmac, context.hmacLock,
                (const byte*)destBlock.data() + context.blockMACBytes,
                destBlock.size() - context.blockMACBytes,
                (char*)mac);

            byte reversedMac[8];
            for (size_t i = 0; i < static_cast<size_t>(context.blockMACBytes); ++i) {
                reversedMac[i] = mac[7 - i];
            }

            if (!VerifyBufsEqual((const byte*)destBlock.data(), reversedMac, context.blockMACBytes)) {
                throw EncFSInvalidBlockException();
            }

            destBlock.assign(destBlock.data() + headerSize, destBlock.size() - headerSize);
        }
    }

    int64_t ToDecodedLength(const FileCodecContext& context, int64_t encodedLength) {
        int64_t size = encodedLength;
        const int64_t headerSize = getHeaderSize(context);
        if (size < (context.uniqueIV ? (int64_t)HEADER_SIZE : 0) + headerSize) {
            return 0;
        }
        if (context.uniqueIV) {
            size -= HEADER_SIZE;
        }
        if (headerSize > 0 && size > 0) {
            int64_t numBlocks = ((size - 1) / context.blockSize) + 1;
            size -= numBlocks * headerSize;
        }
        return max((int64_t)0, size);
    }

    int64_t ToEncodedLength(const FileCodecContext& context, int64_t decodedLength) {
        int64_t size = decodedLength;
        if (size > 0) {
            const int64_t headerSize = getHeaderSize(context);
            const int64_t dataBlockSize = context.blockSize - headerSize;
            if (headerSize > 0 && dataBlockSize > 0) {
                int64_t numBlocks = ((size - 1) / dataBlockSize) + 1;
                size += numBlocks * headerSize;
            }
            if (context.uniqueIV) {
                size += HEADER_SIZE;
            }
        }
        return size;
    }

    void EncodeFileIv(const FileCodecContext& context, const string& plainFilePath, int64_t fileIv, string& encodedFileHeader) {
        if (!context.uniqueIV) {
            encodedFileHeader.assign(HEADER_SIZE, (char)0);
            return;
        }

        char initIvBytes[CHAIN_IV_SIZE];
        computeExternalChainIv(context, plainFilePath, initIvBytes);
        string initIv(initIvBytes, CHAIN_IV_SIZE);
        string decodedFileIv;
        longToBytesByBE(decodedFileIv, fileIv);

        streamEncrypt(
            context.volumeHmac, context.hmacLock,
            string((const char*)context.volumeKey.data(), context.volumeKey.size()),
            string((const char*)context.volumeIv.data(), context.volumeIv.size()),
            initIv, context.aesCfbEnc, context.aesCfbEncLock,
            decodedFileIv, encodedFileHeader);
    }

    int64_t DecodeFileIv(const FileCodecContext& context, const string& plainFilePath, const string& encodedFileHeader) {
        if (!context.uniqueIV) {
            return 0;
        }

        char initIvBytes[CHAIN_IV_SIZE];
        computeExternalChainIv(context, plainFilePath, initIvBytes);
        string initIv(initIvBytes, CHAIN_IV_SIZE);
        string decodedFileIv;

        streamDecrypt(
            context.volumeHmac, context.hmacLock,
            string((const char*)context.volumeKey.data(), context.volumeKey.size()),
            string((const char*)context.volumeIv.data(), context.volumeIv.size()),
            initIv, context.aesCfbDec, context.aesCfbDecLock,
            encodedFileHeader, decodedFileIv);

        return bytesToLongByBE(decodedFileIv);
    }

    void EncodeBlock(const FileCodecContext& context, int64_t fileIv, int64_t blockNum, const string& plainBlock, string& encodedBlock) {
        codeBlock(context, fileIv, blockNum, true, plainBlock, encodedBlock);
    }

    void DecodeBlock(const FileCodecContext& context, int64_t fileIv, int64_t blockNum, const string& encodedBlock, string& plainBlock) {
        codeBlock(context, fileIv, blockNum, false, encodedBlock, plainBlock);
    }
}
