#include "EncFSFile.h"
#include <winioctl.h> 

using namespace std;

// 静的な RNG は競合リスクがあるため、
// マルチスレッド環境では排他制御かインスタンス分離を検討してください。
static thread_local AutoSeededX917RNG<CryptoPP::AES> random;

namespace EncFS {
    int64_t EncFSFile::counter = 0;

    EncFSGetFileIVResult EncFSFile::getFileIV(const LPCWSTR FileName, int64_t* fileIv, bool create) {
        if (this->fileIvAvailable) {
            *fileIv = this->fileIv;
            return EXISTS;
        }
        if (!encfs.isUniqueIV()) {
            this->fileIv = *fileIv = 0L;
            this->fileIvAvailable = true;
            return EXISTS;
        }
        // Read file header.
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = 0;
        if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
            return READ_ERROR;
        }
        string fileHeader;
        fileHeader.resize(EncFSVolume::HEADER_SIZE);
        DWORD ReadLength;
        if (!ReadFile(this->handle, &fileHeader[0], (DWORD)fileHeader.size(), (LPDWORD)&ReadLength, NULL)) {
            return READ_ERROR;
        }
        if (ReadLength != fileHeader.size()) {
            if (!create) {
                if (ReadLength == 0) {
                    return EMPTY;
                }
                SetLastError(ERROR_READ_FAULT);
                return READ_ERROR;
            }
            // Create file header.
            fileHeader.resize(EncFSVolume::HEADER_SIZE);
            random.GenerateBlock((byte*)&fileHeader[0], EncFSVolume::HEADER_SIZE);
            if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                return READ_ERROR;
            }
            // --- 部分書き込みに備えたループ化（小規模なので一回で済む場合がほとんどですが...）
            {
                size_t totalWritten = 0;
                size_t toWrite = fileHeader.size();
                const char* pData = fileHeader.data();
                while (totalWritten < toWrite) {
                    DWORD writtenLen = 0;
                    DWORD request = (DWORD)(toWrite - totalWritten);
                    if (!WriteFile(this->handle, pData + totalWritten, request, &writtenLen, NULL)) {
                        return READ_ERROR;
                    }
                    if (writtenLen == 0) {
                        SetLastError(ERROR_WRITE_FAULT);
                        return READ_ERROR;
                    }
                    totalWritten += writtenLen;
                }
            }
        }

        string cFileName = this->strConv.to_bytes(wstring(FileName));
        this->fileIv = *fileIv = encfs.decodeFileIv(cFileName, fileHeader);
        this->fileIvAvailable = true;
        return EXISTS;
    }

    int32_t EncFSFile::read(const LPCWSTR FileName, char* buff, size_t off, DWORD len) {
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);
        if (!this->canRead) {
            SetLastError(ERROR_READ_FAULT);
            return -1;
        }

        try {
            int64_t fileIv;
            EncFSGetFileIVResult ivResult = this->getFileIV(FileName, &fileIv, false);
            if (ivResult == READ_ERROR) {
                return -1;
            }
            if (ivResult == EMPTY) {
                return 0;
            }

            // Calculate block position.
            const size_t blockSize = encfs.getBlockSize();
            const size_t blockHeaderSize = encfs.getHeaderSize();
            const size_t blockDataSize = blockSize - blockHeaderSize;
            size_t shift = off % blockDataSize;
            size_t blockNum = off / blockDataSize;
            const size_t lastBlockNum = (off + len - 1) / blockDataSize;

            int32_t copiedLen = 0;
            // Copy from buffer if same block is cached.
            if (blockNum == this->lastBlockNum) {
                uint32_t blockLen = (uint32_t)(this->decodeBuffer.size() - shift);
                if (blockLen > len) {
                    blockLen = len;
                }
                memcpy(buff, this->decodeBuffer.data() + shift, blockLen);
                shift = 0;
                len -= blockLen;
                copiedLen += blockLen;
                ++blockNum;
                if (len <= 0) {
                    return copiedLen;
                }
            }

            // Compute how many blocks to read
            size_t blocksOffset = blockNum * blockSize;
            const size_t blocksLength = (lastBlockNum + 1) * blockSize - blocksOffset;
            if (encfs.isUniqueIV()) {
                blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
            }

            if (blocksLength) {
                // Seek for read.
                LARGE_INTEGER distanceToMove;
                distanceToMove.QuadPart = blocksOffset;
                if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                    return -1;
                }

                // --- 修正箇所: 読み込み前に resize する
                this->blockBuffer.resize(blocksLength);

                // Read encrypted data (partial read loop)
                DWORD totalRead = 0;
                while (totalRead < blocksLength) {
                    DWORD readLen = 0;
                    DWORD toRead = (DWORD)(blocksLength - totalRead);
                    if (!ReadFile(this->handle, &this->blockBuffer[0] + totalRead, toRead, &readLen, NULL)) {
                        return -1;
                    }
                    if (readLen == 0) {
                        // EOF
                        break;
                    }
                    totalRead += readLen;
                }

                // 復号処理
                // もし totalRead が非常に小さくてブロックの一部しか読めなかった場合は
                // ブロックとして成立しない可能性もありますが、ある程度は復号を試みる。
                // 下記では単純に "一度で読み切れた分" を復号しています。
                size_t readPos = 0;
                while (readPos < totalRead && len > 0) {
                    size_t remain = totalRead - readPos;
                    size_t blockLen = (remain > blockSize) ? blockSize : remain;

                    this->encodeBuffer.assign((const char*)&this->blockBuffer[readPos], blockLen);
                    this->decodeBuffer.clear();
                    encfs.decodeBlock(fileIv, this->lastBlockNum = blockNum, this->encodeBuffer, this->decodeBuffer);

                    size_t decLen = this->decodeBuffer.size() - shift;
                    if (decLen > len) {
                        decLen = len;
                    }
                    memcpy(buff + copiedLen, this->decodeBuffer.data() + shift, decLen);
                    shift = 0;
                    len -= (DWORD)decLen;
                    copiedLen += (int32_t)decLen;
                    blockNum++;
                    readPos += blockLen;
                }

                this->clearBlockBuffer();
            }
            return copiedLen;
        }
        catch (const EncFSInvalidBlockException&) {
            SetLastError(ERROR_FILE_CORRUPT);
            return -1;
        }
    }

    int32_t EncFSFile::write(const LPCWSTR FileName, size_t fileSize, const char* buff, size_t off, DWORD len) {
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);
        try {
            int64_t fileIv;
            if (this->getFileIV(FileName, &fileIv, true) == READ_ERROR) {
                SetLastError(ERROR_FILE_CORRUPT);
                return -1;
            }

            if (off > fileSize) {
                // Expand file.
                if (!this->_setLength(FileName, fileSize, off)) {
                    SetLastError(ERROR_FILE_CORRUPT);
                    return -1;
                }
            }

            // Calculate position.
            const size_t blockSize = encfs.getBlockSize();
            const size_t blockHeaderSize = encfs.getHeaderSize();
            const size_t blockDataSize = blockSize - blockHeaderSize;
            size_t shift = off % blockDataSize;
            size_t blockNum = off / blockDataSize;
            const size_t lastBlockNum = (off + len - 1) / blockDataSize;
            size_t blocksOffset = blockNum * blockSize;
            if (encfs.isUniqueIV()) {
                blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
            }

            // Seek for write
            LARGE_INTEGER distanceToMove;
            distanceToMove.QuadPart = blocksOffset;
            if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                return -1;
            }

            // 部分ブロックへの書き込み
            if (shift != 0) {
                if (blockNum != this->lastBlockNum) {
                    // まずは既存ブロックを読み込んで復号する
                    this->encodeBuffer.resize(encfs.getBlockSize());
                    DWORD readLen;
                    if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
                        return -1;
                    }
                    // ポインタ巻き戻し
                    if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                        return -1;
                    }
                    this->encodeBuffer.resize(readLen);
                    this->decodeBuffer.clear();
                    encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
                }
                if (this->decodeBuffer.size() < shift) {
                    this->decodeBuffer.append(shift - this->decodeBuffer.size(), (char)0);
                }
            }

            size_t blockDataLen = 0;
            size_t writtenTotal = 0; // 返り値用に合計書き込みバイト数を持つ
            for (size_t i = 0; i < len; i += blockDataLen) {
                blockDataLen = (len - i) > blockDataSize - shift ? (blockDataSize - shift) : (len - i);

                // decodeBuffer に書き込みデータを反映
                if (shift != 0) {
                    if (this->decodeBuffer.size() < shift + blockDataLen) {
                        this->decodeBuffer.resize(shift + blockDataLen);
                    }
                    memcpy(&this->decodeBuffer[shift], buff + i, blockDataLen);
                }
                else if (blockDataLen == blockDataSize || off + i + blockDataLen >= fileSize) {
                    // ブロックぴったり or EOF 近くで全書き込み
                    this->decodeBuffer.assign(buff + i, blockDataLen);
                }
                else {
                    // 途中ブロックなので既存データを読み込んで復号しておく
                    this->encodeBuffer.resize(encfs.getBlockSize());
                    DWORD readLen;
                    if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
                        return -1;
                    }
                    // カーソルを元に戻す
                    LARGE_INTEGER backMove;
                    backMove.QuadPart = -(LONGLONG)readLen;
                    if (!SetFilePointerEx(this->handle, backMove, NULL, FILE_CURRENT)) {
                        return -1;
                    }
                    this->encodeBuffer.resize(readLen);
                    this->decodeBuffer.clear();
                    encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);

                    if (this->decodeBuffer.size() < blockDataLen) {
                        this->decodeBuffer.resize(blockDataLen);
                    }
                    memcpy(&this->decodeBuffer[0], buff + i, blockDataLen);
                }

                // エンコードしてファイルに書き戻す
                this->encodeBuffer.clear();
                encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);

                // --- 修正箇所: partial write への対応
                {
                    size_t totalWritten = 0;
                    size_t toWrite = this->encodeBuffer.size();
                    const char* pData = this->encodeBuffer.data();
                    while (totalWritten < toWrite) {
                        DWORD writtenLen = 0;
                        DWORD request = (DWORD)(toWrite - totalWritten);
                        if (!WriteFile(this->handle, pData + totalWritten, request, &writtenLen, NULL)) {
                            return -1;
                        }
                        if (writtenLen == 0) {
                            SetLastError(ERROR_WRITE_FAULT);
                            return -1;
                        }
                        totalWritten += writtenLen;
                    }
                }

                blockNum++;
                shift = 0;
                writtenTotal += blockDataLen;
            }

            return (int32_t)writtenTotal;
        }
        catch (const EncFSInvalidBlockException&) {
            SetLastError(ERROR_FILE_CORRUPT);
            return -1;
        }
    }

    int32_t EncFSFile::reverseRead(const LPCWSTR FileName, char* buff, size_t off, DWORD len) {
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);
        if (!this->canRead) {
            SetLastError(ERROR_READ_FAULT);
            return -1;
        }

        int64_t fileIv = 0; // Cannot use fileIv in reverse mode.

        // Calculate position.
        int32_t blockSize = encfs.getBlockSize();
        int32_t shift = off % blockSize;
        int64_t blockNum = off / blockSize;
        int64_t lastBlockNum = (off + len - 1) / blockSize;

        int64_t blocksOffset = blockNum * blockSize;
        int64_t blocksLength = (lastBlockNum + 1) * blockSize - blocksOffset;

        int32_t i = 0;
        int32_t blockDataLen;

        if (blockNum == this->lastBlockNum) {
            // Copy from buffer
            blockDataLen = (int32_t)std::min<size_t>(len, this->encodeBuffer.size() - shift);
            memcpy(buff, &this->encodeBuffer[shift], blockDataLen);
            blocksOffset = blockNum++ * blockSize;
            shift = 0;
            i += blockDataLen;
            if (i >= (int32_t)len || this->encodeBuffer.size() < (size_t)blockSize) {
                return i;
            }
        }

        // Seek for read.
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = blocksOffset;
        if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
            return -1;
        }

        // --- 修正箇所: 読み込み前に resize
        this->blockBuffer.resize(blocksLength);

        // Partial read
        DWORD totalRead = 0;
        while ((int64_t)totalRead < blocksLength) {
            DWORD readLen = 0;
            DWORD toRead = (DWORD)(blocksLength - totalRead);
            if (!ReadFile(this->handle, &this->blockBuffer[0] + totalRead, toRead, &readLen, NULL)) {
                return -1;
            }
            if (readLen == 0) {
                break;
            }
            totalRead += readLen;
        }
        // 実際に読めた分を len として扱う
        if ((DWORD)len > totalRead) {
            len = totalRead;
        }
        if (i >= (int32_t)len) {
            this->clearBlockBuffer();
            return i;
        }

        // Encode the read data
        int64_t bufferPos = 0;
        for (; i < (int32_t)len; i += blockDataLen) {
            blockDataLen = (int32_t)std::min<size_t>(len - i, blockSize - shift);

            // Prepare decodeBuffer
            this->decodeBuffer.resize(blockDataLen + shift);
            memcpy(&this->decodeBuffer[0], &this->blockBuffer[bufferPos * blockSize], this->decodeBuffer.size());

            // Encode
            this->encodeBuffer.clear();
            encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);

            memcpy(buff + i, &this->encodeBuffer[shift], blockDataLen);

            blockNum++;
            bufferPos++;
            shift = 0;
        }
        this->clearBlockBuffer();
        return i;
    }

    bool EncFSFile::flush() {
        return FlushFileBuffers(this->handle);
    }

    bool EncFSFile::setLength(const LPCWSTR FileName, const size_t length) {
        lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);

        LARGE_INTEGER encodedFileSize;
        if (!GetFileSizeEx(this->handle, &encodedFileSize)) {
            return false;
        }
        size_t fileSize = encfs.toDecodedLength(encodedFileSize.QuadPart);
        if (fileSize == length) {
            return true;
        }

        return this->_setLength(FileName, fileSize, length);
    }

    bool EncFSFile::_setLength(const LPCWSTR FileName, const size_t fileSize, const size_t length) {
        if (length >= 100 * 1024 * 1024) {
			// 100MB 以上のファイルはスパースファイルにする
            DWORD temp;
		    DeviceIoControl(handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &temp, NULL);
		}

        if (length == 0) {
            // ファイルサイズをゼロにする場合
            LARGE_INTEGER offset;
            offset.QuadPart = 0;
            if (!SetFilePointerEx(this->handle, offset, NULL, FILE_BEGIN)) {
                return false;
            }
            if (!SetEndOfFile(this->handle)) {
                return false;
            }
            this->fileIvAvailable = false;
            return true;
        }

        // 境界部分をデコード
        size_t blockHeaderSize = encfs.getHeaderSize();
        size_t blockDataSize = encfs.getBlockSize() - blockHeaderSize;
        size_t shift;
        size_t blockNum;
        size_t blocksOffset;
        if (length < fileSize) {
            // 縮小
            shift = length % blockDataSize;
            blockNum = length / blockDataSize;
        }
        else {
            // 拡大
            shift = fileSize % blockDataSize;
            blockNum = fileSize / blockDataSize;
        }
        int64_t fileIv;
        if (this->getFileIV(FileName, &fileIv, true) == READ_ERROR) {
            return false;
        }
        if (shift != 0) {
            blocksOffset = blockNum * encfs.getBlockSize();
            if (encfs.isUniqueIV()) {
                blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
            }
            LARGE_INTEGER distanceToMove;
            distanceToMove.QuadPart = blocksOffset;
            if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                return false;
            }
            this->encodeBuffer.resize(encfs.getBlockSize());
            DWORD readLen;
            if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
                return false;
            }
            this->encodeBuffer.resize(readLen);
            this->decodeBuffer.clear();
            encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
        }

        size_t encodedLength = encfs.toEncodedLength(length);
        {
            LARGE_INTEGER offset;
            offset.QuadPart = encodedLength;
            if (!SetFilePointerEx(this->handle, offset, NULL, FILE_BEGIN)) {
                return false;
            }
            if (!SetEndOfFile(this->handle)) {
                return false;
            }
        }
        this->lastBlockNum = -1;

        if (shift != 0) {
            // 再エンコードして書き込み
            size_t blockDataLen = length - blockNum * blockDataSize;
            if (blockDataLen > blockDataSize) {
                blockDataLen = blockDataSize;
            }
            if (this->decodeBuffer.size() < blockDataLen) {
                this->decodeBuffer.append(blockDataLen - this->decodeBuffer.size(), (char)0);
            }
            else {
                this->decodeBuffer.resize(blockDataLen);
            }
            this->encodeBuffer.clear();
            encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);

            // 書き込み位置を戻す
            LARGE_INTEGER distanceToMove;
            distanceToMove.QuadPart = (LONGLONG)blocksOffset;
            if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
                return false;
            }

            // --- 部分書き込みに対応
            {
                size_t totalWritten = 0;
                size_t toWrite = this->encodeBuffer.size();
                const char* pData = this->encodeBuffer.data();
                while (totalWritten < toWrite) {
                    DWORD writtenLen = 0;
                    DWORD request = (DWORD)(toWrite - totalWritten);
                    if (!WriteFile(this->handle, pData + totalWritten, request, &writtenLen, NULL)) {
                        return false;
                    }
                    if (writtenLen == 0) {
                        SetLastError(ERROR_WRITE_FAULT);
                        return false;
                    }
                    totalWritten += writtenLen;
                }
            }
        }

        // 拡大した末尾をエンコード
        if (length > fileSize) {
            size_t s = length % blockDataSize;
            if (s != 0 && (fileSize == 0 || blockNum != length / blockDataSize)) {
                blockNum = length / blockDataSize;
                this->decodeBuffer.assign(s, (char)0);
                this->encodeBuffer.clear();
                encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);
                // ファイル末尾へ移動
                LARGE_INTEGER moveEnd;
                moveEnd.QuadPart = 0;
                if (!SetFilePointerEx(this->handle, moveEnd, NULL, FILE_END)) {
                    return false;
                }

                // 書き込み位置をさらに調整
                moveEnd.QuadPart = -(int64_t)s - (int64_t)blockHeaderSize;
                if (!SetFilePointerEx(this->handle, moveEnd, NULL, FILE_CURRENT)) {
                    return false;
                }

                // --- 部分書き込みに対応
                {
                    size_t totalWritten = 0;
                    size_t toWrite = this->encodeBuffer.size();
                    const char* pData = this->encodeBuffer.data();
                    while (totalWritten < toWrite) {
                        DWORD writtenLen = 0;
                        DWORD request = (DWORD)(toWrite - totalWritten);
                        if (!WriteFile(this->handle, pData + totalWritten, request, &writtenLen, NULL)) {
                            return false;
                        }
                        if (writtenLen == 0) {
                            SetLastError(ERROR_WRITE_FAULT);
                            return false;
                        }
                        totalWritten += writtenLen;
                    }
                }
            }
        }
        return true;
    }

    bool EncFSFile::changeFileIV(const LPCWSTR FileName, const LPCWSTR NewFileName) {
        int64_t fileIv;
        EncFSGetFileIVResult ivResult = this->getFileIV(FileName, &fileIv, false);
        if (ivResult == EMPTY) {
            return true;
        }
        if (ivResult == READ_ERROR) {
            return false;
        }
        string cNewFileName = strConv.to_bytes(wstring(NewFileName));
        string encodedFileHeader;
        encfs.encodeFileIv(cNewFileName, fileIv, encodedFileHeader);
        LARGE_INTEGER distanceToMove;
        distanceToMove.QuadPart = 0;
        if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
            return false;
        }
        // --- こちらも部分書き込みを考慮
        {
            size_t totalWritten = 0;
            size_t toWrite = encodedFileHeader.size();
            const char* pData = encodedFileHeader.data();
            while (totalWritten < toWrite) {
                DWORD writtenLen = 0;
                DWORD request = (DWORD)(toWrite - totalWritten);
                if (!WriteFile(this->handle, pData + totalWritten, request, &writtenLen, NULL)) {
                    return false;
                }
                if (writtenLen == 0) {
                    SetLastError(ERROR_WRITE_FAULT);
                    return false;
                }
                totalWritten += writtenLen;
            }
        }
        return true;
    }

    void EncFSFile::clearBlockBuffer() {
        // サイズのみクリア。capacity はそのまま保持して再利用。
        this->blockBuffer.clear();
    }

} // namespace EncFS
