#include "EncFSVolume.h"
#include "EncFSUtils.hpp"

#include "rapidxml.hpp"

#include <aes.h>

using namespace std;
using namespace rapidxml;
using namespace CryptoPP;

static thread_local AutoSeededX917RNG<CryptoPP::AES> random;

namespace EncFS {
    EncFSVolume::EncFSVolume() {
        Base64Decoder::InitializeDecodingLookupArray(this->base64Lookup, ALPHABET, 64, false);
    };

    /*
      設定ファイルを読み込み。
    */
    void EncFSVolume::load(const string& xml, bool reverse)
    {
        // xml_document<> doc; // ← 重複定義は削除

        try {
            // rapidxml は \0 終端を必要とするため、vector<char> に詰めて終端文字を push_back
            std::vector<char> buffer(xml.begin(), xml.end());
            buffer.push_back('\0');

            // ここで定義を統一
            xml_document<> doc;
            doc.parse<0>(buffer.data()); // buffer.data() は可変バッファ

            xml_node<>* boost_serialization = doc.first_node("boost_serialization");
            if (!boost_serialization) {
                throw EncFSBadConfigurationException("boost_serialization");
            }
            xml_node<>* cfg = boost_serialization->first_node("cfg");
            if (!cfg) {
                throw EncFSBadConfigurationException("cfg");
            }
            {
                xml_node<>* node = cfg->first_node("keySize");
                if (!node) {
                    throw EncFSBadConfigurationException("keySize");
                }
                // strtol のエラーチェックをするなら errno や範囲を確認する
                this->keySize = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("blockSize");
                if (!node) {
                    throw EncFSBadConfigurationException("blockSize");
                }
                this->blockSize = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("uniqueIV");
                if (!node) {
                    throw EncFSBadConfigurationException("uniqueIV");
                }
                this->uniqueIV = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("chainedNameIV");
                if (!node) {
                    throw EncFSBadConfigurationException("chainedNameIV");
                }
                this->chainedNameIV = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("externalIVChaining");
                if (!node) {
                    throw EncFSBadConfigurationException("externalIVChaining");
                }
                this->externalIVChaining = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("blockMACBytes");
                if (!node) {
                    throw EncFSBadConfigurationException("blockMACBytes");
                }
                this->blockMACBytes = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("blockMACRandBytes");
                if (!node) {
                    throw EncFSBadConfigurationException("blockMACRandBytes");
                }
                this->blockMACRandBytes = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("allowHoles");
                if (!node) {
                    throw EncFSBadConfigurationException("allowHoles");
                }
                this->allowHoles = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("encodedKeySize");
                if (!node) {
                    throw EncFSBadConfigurationException("encodedKeySize");
                }
                this->encodedKeySize = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("encodedKeyData");
                if (!node) {
                    throw EncFSBadConfigurationException("encodedKeyData");
                }
                this->encodedKeyData.assign(node->value());
                trim(this->encodedKeyData);
            }
            {
                xml_node<>* node = cfg->first_node("saltLen");
                if (!node) {
                    throw EncFSBadConfigurationException("saltLen");
                }
                this->saltLen = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("saltData");
                if (!node) {
                    throw EncFSBadConfigurationException("saltData");
                }
                this->saltData.assign(node->value());
                trim(this->saltData);
            }
            {
                xml_node<>* node = cfg->first_node("kdfIterations");
                if (!node) {
                    throw EncFSBadConfigurationException("kdfIterations");
                }
                this->kdfIterations = strtol(node->value(), nullptr, 10);
            }
            {
                xml_node<>* node = cfg->first_node("desiredKDFDuration");
                if (!node) {
                    throw EncFSBadConfigurationException("desiredKDFDuration");
                }
                this->desiredKDFDuration = strtol(node->value(), nullptr, 10);
            }

            // reverse をそのまま代入し、条件分岐
            this->reverse = reverse;
            if (this->reverse) {
                // Reverse mode constraints.
                this->uniqueIV = false;
                this->chainedNameIV = false;
                this->blockMACBytes = 0;
                this->blockMACRandBytes = 0;
            }
        }
        catch (const parse_error& ex) {
            // 参照で受け取る
            throw EncFSBadConfigurationException(string(ex.what()) + " : " + ex.where<char>());
        }
    }

    void EncFSVolume::create(char* password, EncFSMode mode, bool reverse)
    {
        this->blockSize = 1024;
        this->uniqueIV = true;
        this->blockMACBytes = 8;
        this->blockMACRandBytes = 0;
        this->allowHoles = true;
        switch (mode) {
        default:
            this->keySize = 192;
            this->chainedNameIV = false;
            this->externalIVChaining = false;
            break;
        case PARANOIA:
            this->keySize = 256;
            this->chainedNameIV = true;
            this->externalIVChaining = true;
            break;
        }

        this->saltLen = 20;
        string salt;
        salt.resize(this->saltLen);
        random.GenerateBlock((byte*)salt.data(), salt.size());
        {
            Base64Encoder encoder;
            encoder.Put((byte*)salt.data(), salt.size());
            encoder.MessageEnd();
            this->saltData.resize(encoder.MaxRetrievable());
            encoder.Get((byte*)this->saltData.data(), this->saltData.size());
            trim(this->saltData);
        }

        // 代入と分岐を分ける
        this->reverse = reverse;
        if (this->reverse) {
            // Reverse mode constraints.
            this->uniqueIV = false;
            this->chainedNameIV = false;
            this->blockMACBytes = 0;
            this->blockMACRandBytes = 0;
        }

        this->kdfIterations = 170203;
        this->desiredKDFDuration = 500;
        this->encodedKeySize = 44;

        // キーを生成
        string plainKey;
        plainKey.resize(this->encodedKeySize - 4);
        random.GenerateBlock((byte*)plainKey.data(), plainKey.size());

        string pbkdf2Key;
        this->deriveKey(password, pbkdf2Key);

        string passKey(pbkdf2Key.begin(), pbkdf2Key.begin() + this->keySize / 8);
        string passIv(pbkdf2Key.begin() + this->keySize / 8,
            pbkdf2Key.begin() + this->keySize / 8 + 16);

        HMAC<SHA1> passKeyHmac((const byte*)passKey.data(), passKey.size());
        char mac[4];
        mac32(passKeyHmac, this->hmacLock, plainKey, mac);
        string ivSeed(mac, 4);

        string encryptedKey;
        streamEncrypt(passKeyHmac, this->hmacLock, passKey, passIv, ivSeed,
            this->aesCfbEnc, this->aesCfbEncLock, plainKey, encryptedKey);
        encryptedKey.insert(0, ivSeed);

        {
            Base64Encoder encoder;
            encoder.Put((byte*)encryptedKey.data(), encryptedKey.size());
            encoder.MessageEnd();
            this->encodedKeyData.resize(encoder.MaxRetrievable());
            encoder.Get((byte*)this->encodedKeyData.data(), this->encodedKeyData.size());
            trim(this->encodedKeyData);
        }
    }

    void EncFSVolume::save(string& xml) {
        // temp がそこそこ大きいサイズなので、this->encodedKeyData が大きいときは要注意。
        // 必要に応じて動的配列や std::string::append で安全に処理するのが望ましい。
        const char temp[] =
            R"(<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<!DOCTYPE boost_serialization>
<boost_serialization signature="serialization::archive" version="13">
<cfg class_id="0" tracking_level="0" version="20">
	<version>20100713</version>
	<creator>EncFSy</creator>
	<cipherAlg class_id="1" tracking_level="0" version="0">
		<name>ssl/aes</name>
		<major>3</major>
		<minor>0</minor>
	</cipherAlg>
	<nameAlg>
		<name>nameio/block</name>
		<major>3</major>
		<minor>0</minor>
	</nameAlg>
	<keySize>%d</keySize>
	<blockSize>%d</blockSize>
	<uniqueIV>%d</uniqueIV>
	<chainedNameIV>%d</chainedNameIV>
	<externalIVChaining>%d</externalIVChaining>
	<blockMACBytes>%d</blockMACBytes>
	<blockMACRandBytes>%d</blockMACRandBytes>
	<allowHoles>%d</allowHoles>
	<encodedKeySize>%d</encodedKeySize>
	<encodedKeyData>
%s
	</encodedKeyData>
	<saltLen>%d</saltLen>
	<saltData>
%s
	</saltData>
	<kdfIterations>%d</kdfIterations>
	<desiredKDFDuration>%d</desiredKDFDuration>
</cfg>
</boost_serialization>
)";

        // もし encodedKeyData や saltData が大きいとオーバーフローの恐れがあるため、十分大きくとる。
        // 本来は動的に生成するのが望ましいが、簡易修正に留める。
        char s[sizeof(temp) + 2048];  // 以前より拡大
        // 安全性を高めるため _snprintf_s を使う方法もある。
        sprintf_s(
            s,
            sizeof(s),
            temp,
            this->keySize,
            this->blockSize,
            this->uniqueIV,
            this->chainedNameIV,
            this->externalIVChaining,
            this->blockMACBytes,
            this->blockMACRandBytes,
            this->allowHoles,
            this->encodedKeySize,
            this->encodedKeyData.c_str(),
            this->saltLen,
            this->saltData.c_str(),
            this->kdfIterations,
            this->desiredKDFDuration
        );
        xml.assign(s);
    }

    void EncFSVolume::deriveKey(char* password, string& pbkdf2Key) {
        // PBKDF2 Hmac SHA1
        pbkdf2Key.resize(this->keySize / 8 + 16);
        PKCS5_PBKDF2_HMAC<SHA1> pbkdf2;

        // saltData を base64 デコードしてバイナリを得る
        string salt;
        {
            Base64Decoder decoder;
            decoder.Put((byte*)this->saltData.data(), this->saltData.size());
            decoder.MessageEnd();
            if (decoder.MaxRetrievable() < this->saltLen) {
                throw EncFSUnlockFailedException();
            }
            salt.resize(this->saltLen);
            decoder.Get((byte*)salt.data(), salt.size());
        }

        size_t passLen = strlen(password);
        pbkdf2.DeriveKey(
            (byte*)pbkdf2Key.data(),
            pbkdf2Key.size(),
            0,
            (const byte*)password,
            passLen,
            (const byte*)salt.data(),
            this->saltLen,
            this->kdfIterations
        );

        // メモリ中のパスワードをクリア
        random.GenerateBlock((byte*)password, passLen);
    }

    void EncFSVolume::unlock(char* password) {
        // ボリュームキーを復号
        string pbkdf2Key;
        this->deriveKey(password, pbkdf2Key);

        string encodedKey;
        {
            Base64Decoder decoder;
            decoder.Put((byte*)this->encodedKeyData.data(), this->encodedKeyData.size());
            decoder.MessageEnd();
            if (decoder.MaxRetrievable() < this->encodedKeySize) {
                throw EncFSUnlockFailedException();
            }
            encodedKey.resize(this->encodedKeySize);
            decoder.Get((byte*)encodedKey.data(), encodedKey.size());
        }

        string encryptedKey(encodedKey.begin() + 4, encodedKey.end());
        string passKey(pbkdf2Key.begin(), pbkdf2Key.begin() + this->keySize / 8);
        string passIv(pbkdf2Key.begin() + this->keySize / 8,
            pbkdf2Key.begin() + this->keySize / 8 + 16);
        string ivSeed(encodedKey.begin(), encodedKey.begin() + 4);
        HMAC<SHA1> passKeyHmac((const byte*)passKey.data(), passKey.size());

        string plainKey;
        streamDecrypt(
            passKeyHmac, this->hmacLock,
            passKey, passIv, ivSeed,
            this->aesCfbDec, this->aesCfbDecLock,
            encryptedKey, plainKey
        );

        // チェックサムの実行
        char mac[4];
        mac32(passKeyHmac, this->hmacLock, plainKey, mac);
        for (size_t i = 0; i < sizeof(mac); ++i) {
            if (mac[i] != ivSeed[i]) {
                throw EncFSUnlockFailedException();
            }
        }

        // volumeKey, volumeIv を初期化
        this->volumeKey.insert(
            this->volumeKey.begin(),
            plainKey.begin(),
            plainKey.begin() + this->keySize / 8
        );
        this->volumeIv.insert(
            this->volumeIv.begin(),
            plainKey.begin() + this->keySize / 8,
            plainKey.end()
        );
        this->volumeHmac.SetKey(
            (const byte*)this->volumeKey.data(),
            this->volumeKey.size()
        );
    }

    void EncFSVolume::processFileName(
        SymmetricCipher& cipher,
        std::mutex& cipherLock,
        const string& fileIv,
        const string& binFileName,
        string& fileName
    ) {
        char ivSpec[16];
        generateIv(this->volumeHmac, this->hmacLock, this->volumeIv, fileIv, ivSpec);

        {
            lock_guard<decltype(cipherLock)> lock(cipherLock);
            cipher.SetKeyWithIV(
                (const byte*)this->volumeKey.data(),
                this->volumeKey.size(),
                (const byte*)ivSpec
            );
            StreamTransformationFilter filter(
                cipher,
                new StringSink(fileName),
                StreamTransformationFilter::ZEROS_PADDING
            );
            filter.Put((byte*)binFileName.data(), binFileName.size());
            filter.MessageEnd();
        }
    }

    void EncFSVolume::encodeFileName(
        const string& plainFileName,
        const string& plainDirPath,
        string& encodedFileName
    ) {
        // 暗号化する必要のないファイル名
        if (plainFileName == "." || plainFileName == "..") {
            encodedFileName.append(plainFileName);
            return;
        }

        // getPaddedDecFilename
        const size_t padBytesSize = 16;
        size_t padLen = padBytesSize - (plainFileName.size() % padBytesSize);
        if (padLen == 0) {
            padLen = padBytesSize;
        }
        char chainIv[8];
        if (this->chainedNameIV) {
            // ファイル名キーがディレクトリ名に依存
            computeChainIv(this->volumeHmac, this->hmacLock, plainDirPath, chainIv);
        }
        else {
            memset(chainIv, 0, sizeof(chainIv));
        }

        // パディング
        string paddedFileName(plainFileName);
        for (size_t i = 0; i < padLen; ++i) {
            paddedFileName += (char)padLen;
        }

        // 2バイトの iv(Checksum)
        char iv[2];
        if (this->chainedNameIV) {
            mac16withIv(this->volumeHmac, this->hmacLock, paddedFileName, chainIv, iv);
        }
        else {
            mac16(this->volumeHmac, this->hmacLock, paddedFileName, iv);
        }

        // fileIv の組み立て
        string fileIv;
        fileIv.resize(8);
        fileIv[0] = chainIv[0];
        fileIv[1] = chainIv[1];
        fileIv[2] = chainIv[2];
        fileIv[3] = chainIv[3];
        fileIv[4] = chainIv[4];
        fileIv[5] = chainIv[5];
        fileIv[6] = iv[0] ^ chainIv[6];
        fileIv[7] = iv[1] ^ chainIv[7];

        // CBC でファイル名を暗号化
        string binFileName;
        this->processFileName(
            this->aesCbcEnc,
            this->aesCbcEncLock,
            fileIv,
            paddedFileName,
            binFileName
        );
        binFileName.insert(0, iv, 2);  // 先頭に 2バイトの iv を付加

        // Base64
        encodeBase64FileName(binFileName, encodedFileName);
    }

    void EncFSVolume::decodeFileName(
        const string& encodedFileName,
        const string& plainDirPath,
        string& plainFileName
    ) {
        // 復号不要なファイル名
        if (encodedFileName == "." || encodedFileName == "..") {
            plainFileName.append(encodedFileName);
            return;
        }

        // Base64 デコード
        string binFileName;
        decodeBase64FileName(this->base64Lookup, encodedFileName, binFileName);
        if (binFileName.size() < 2 + this->aesCbcDec.MandatoryBlockSize()) {
            throw EncFSInvalidBlockException();
        }

        char chainIv[8];
        if (this->chainedNameIV) {
            computeChainIv(this->volumeHmac, this->hmacLock, plainDirPath, chainIv);
        }
        else {
            memset(chainIv, 0, sizeof(chainIv));
        }

        // fileIv 復元
        string fileIv(8, '\0');
        fileIv[0] = chainIv[0];
        fileIv[1] = chainIv[1];
        fileIv[2] = chainIv[2];
        fileIv[3] = chainIv[3];
        fileIv[4] = chainIv[4];
        fileIv[5] = chainIv[5];
        fileIv[6] = binFileName[0] ^ chainIv[6];
        fileIv[7] = binFileName[1] ^ chainIv[7];

        // 先頭2バイトを除去
        char iv1[2];
        iv1[0] = binFileName[0];
        iv1[1] = binFileName[1];
        binFileName.erase(0, 2);

        // 復号
        size_t pos = plainFileName.size();
        this->processFileName(
            this->aesCbcDec,
            this->aesCbcDecLock,
            fileIv,
            binFileName,
            plainFileName
        );

        // チェックサム検証 (iv1 vs iv2)
        char iv2[2];
        if (this->chainedNameIV) {
            mac16withIv(this->volumeHmac, this->hmacLock,
                plainFileName.substr(pos), chainIv, iv2);
        }
        else {
            mac16(this->volumeHmac, this->hmacLock,
                plainFileName.substr(pos), iv2);
        }
        for (size_t i = 0; i < 2; ++i) {
            if (iv1[i] != iv2[i]) {
                throw EncFSInvalidBlockException();
            }
        }

        // パディング除去
        const size_t padLen = (unsigned char)plainFileName[plainFileName.size() - 1];
        for (size_t i = 0; i < padLen; ++i) {
            if ((unsigned char)plainFileName[plainFileName.size() - padLen + i] != padLen) {
                throw EncFSInvalidBlockException();
            }
        }
        plainFileName.erase(plainFileName.size() - padLen, padLen);
    }

    void EncFSVolume::codeFilePath(
        const string& srcFilePath,
        string& destFilePath,
        bool encode
    ) {
        string dirPath;
        string::size_type pos1 = 0;
        string::size_type pos2;

        do {
            bool alt = false;
            pos2 = srcFilePath.find(g_pathSeparator, pos1);
            if (pos2 == string::npos) {
                if (this->altStream) {
                    // ストリームセパレータを平文とみなす
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
                string destName = srcFilePath.substr(pos1, pos2 - pos1);

                destFilePath += g_pathSeparator;
                if (encode) {
                    this->encodeFileName(destName, dirPath, destFilePath);
                }
                else {
                    this->decodeFileName(destName, dirPath, destFilePath);
                }

                if (alt) {
                    string altName = srcFilePath.substr(pos2, srcFilePath.size() - pos2);
                    destFilePath += altName;
                    destName += altName;
                    pos2 = srcFilePath.size();
                }

                dirPath += g_pathSeparator;
                dirPath += destName;
            }
            pos1 = pos2 + 1;
        } while (pos2 != srcFilePath.size());

        if (destFilePath.empty()) {
            destFilePath += g_pathSeparator;
        }
    }

    void EncFSVolume::encodeFilePath(
        const string& plainFilePath,
        string& encodedFilePath
    ) {
        this->codeFilePath(plainFilePath, encodedFilePath, true);
    }

    void EncFSVolume::decodeFilePath(
        const string& encodedFilePath,
        string& plainFilePath
    ) {
        this->codeFilePath(encodedFilePath, plainFilePath, false);
    }

    int64_t EncFSVolume::toDecodedLength(const int64_t encodedLength) {
        int64_t size = encodedLength;
        const int64_t headerSize = this->getHeaderSize();
        if (size < (this->uniqueIV ? HEADER_SIZE : 0) + headerSize) {
            return 0;
        }
        if (this->uniqueIV) {
            size -= HEADER_SIZE;
        }
        if (headerSize > 0) {
            int64_t numBlocks = ((size - 1) / this->blockSize) + 1;
            size -= numBlocks * headerSize;
        }
        return size;
    }

    int64_t EncFSVolume::toEncodedLength(const int64_t decodedLength) {
        int64_t size = decodedLength;
        if (size != 0) {
            int64_t headerSize = this->getHeaderSize();
            if (headerSize > 0) {
                int64_t numBlocks = ((size - 1) / (this->blockSize - headerSize)) + 1;
                size += numBlocks * headerSize;
            }
            if (this->uniqueIV) {
                size += HEADER_SIZE;
            }
        }
        return size;
    }

    void EncFSVolume::encodeFileIv(
        const string& plainFilePath,
        const int64_t fileIv,
        string& encodedFileHeader
    ) {
        if (!this->uniqueIV) {
            encodedFileHeader.assign(8, (char)0);
            return;
        }

        string initIv;
        if (this->externalIVChaining) {
            initIv.resize(8);
            computeChainIv(this->volumeHmac, this->hmacLock, plainFilePath, &initIv[0]);
        }
        else {
            initIv.assign(8, (char)0);
        }
        string decodedFileIv;
        longToBytesByBE(decodedFileIv, fileIv);

        streamEncrypt(
            this->volumeHmac, this->hmacLock,
            this->volumeKey, this->volumeIv,
            initIv, this->aesCfbEnc, this->aesCfbEncLock,
            decodedFileIv, encodedFileHeader
        );
    }

    int64_t EncFSVolume::decodeFileIv(
        const string& plainFilePath,
        const string& encodedFileHeader
    ) {
        if (!this->uniqueIV) {
            return 0;
        }

        string initIv;
        if (this->externalIVChaining) {
            initIv.resize(8);
            computeChainIv(this->volumeHmac, this->hmacLock, plainFilePath, &initIv[0]);
        }
        else {
            initIv.assign(8, (char)0);
        }

        string decodedFileIv;
        streamDecrypt(
            this->volumeHmac, this->hmacLock,
            this->volumeKey, this->volumeIv,
            initIv, this->aesCfbDec, this->aesCfbDecLock,
            encodedFileHeader, decodedFileIv
        );

        return bytesToLongByBE(decodedFileIv);
    }

    void EncFSVolume::encodeBlock(
        const int64_t fileIv,
        const int64_t blockNum,
        const string& plainBlock,
        string& encodedBlock
    ) {
        this->codeBlock(fileIv, blockNum, true, plainBlock, encodedBlock);
    }

    void EncFSVolume::decodeBlock(
        const int64_t fileIv,
        const int64_t blockNum,
        const string& encodedBlock,
        string& plainBlock
    ) {
        this->codeBlock(fileIv, blockNum, false, encodedBlock, plainBlock);
    }

    void EncFSVolume::codeBlock(
        const int64_t fileIv,
        const int64_t blockNum,
        const bool encode,
        const string& srcBlock,
        string& destBlock
    ) {
        const int64_t iv = blockNum ^ fileIv;
        const size_t headerSize = this->getHeaderSize();

        if (encode) {
            // 暗号化
            if (this->allowHoles && srcBlock.size() + headerSize == this->blockSize) {
                bool zeroBlock = true;
                for (char c : srcBlock) {
                    if (c != 0) {
                        zeroBlock = false;
                        break;
                    }
                }
                if (zeroBlock) {
                    destBlock.append(this->blockSize, (char)0);
                    return;
                }
            }

            // ヘッダ + データをまとめたブロック
            string block;
            block.resize(headerSize);
            block.append(srcBlock);

            // チェックサム
            string mac;
            mac.resize(this->blockMACBytes);
            mac64(
                this->volumeHmac, this->hmacLock,
                (const byte*)srcBlock.data(), srcBlock.size(), &mac[0]
            );
            // MACを先頭に格納
            for (size_t i = 0; i < this->blockMACBytes; i++) {
                block[i] = mac[7 - i];
            }

            // IV
            string blockIv;
            longToBytesByBE(blockIv, iv);

            // CBC (サイズ固定) or CFB (可変サイズ)
            if (block.size() == this->blockSize) {
                blockCipher(
                    this->volumeHmac, this->hmacLock,
                    this->volumeKey, this->volumeIv,
                    blockIv, this->aesCbcEnc, this->aesCbcEncLock,
                    block, destBlock
                );
            }
            else {
                streamEncrypt(
                    this->volumeHmac, this->hmacLock,
                    this->volumeKey, this->volumeIv,
                    blockIv, this->aesCfbEnc, this->aesCfbEncLock,
                    block, destBlock
                );
            }
        }
        else {
            // 復号
            if (this->allowHoles && srcBlock.size() == this->blockSize) {
                bool zeroBlock = true;
                for (char c : srcBlock) {
                    if (c != 0) {
                        zeroBlock = false;
                        break;
                    }
                }
                if (zeroBlock) {
                    destBlock.append(this->blockSize - headerSize, (char)0);
                    return;
                }
            }

            string blockIv;
            longToBytesByBE(blockIv, iv);

            if (srcBlock.size() == this->blockSize) {
                blockCipher(
                    this->volumeHmac, this->hmacLock,
                    this->volumeKey, this->volumeIv,
                    blockIv, this->aesCbcDec, this->aesCbcDecLock,
                    srcBlock, destBlock
                );
            }
            else {
                streamDecrypt(
                    this->volumeHmac, this->hmacLock,
                    this->volumeKey, this->volumeIv,
                    blockIv, this->aesCfbDec, this->aesCfbDecLock,
                    srcBlock, destBlock
                );
            }

            // チェックサム検証
            if (destBlock.size() < headerSize) {
                throw EncFSInvalidBlockException();
            }
            bool valid = true;
            string mac;
            mac.resize(this->blockMACBytes);

            mac64(
                this->volumeHmac, this->hmacLock,
                (const byte*)destBlock.data() + this->blockMACBytes,
                destBlock.size() - this->blockMACBytes,
                &mac[0]
            );

            for (size_t i = 0; i < this->blockMACBytes; i++) {
                if (destBlock[i] != mac[7 - i]) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                throw EncFSInvalidBlockException();
            }

            // ヘッダ部分を除去
            destBlock.assign(
                destBlock.data() + headerSize,
                destBlock.size() - headerSize
            );
        }
    }

} // namespace EncFS
