
#include "EncFSFile.h"

using namespace std;

static AutoSeededX917RNG<CryptoPP::AES> random;

namespace EncFS {
	int64_t EncFSFile::counter = 0;

	EncFSGetFileIVResult EncFSFile::getFileIV(const LPCWSTR FileName, int64_t *fileIv, bool create) {
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
			DWORD writtenLen;
			if (!WriteFile(this->handle, fileHeader.data(), (DWORD)fileHeader.size(), &writtenLen, NULL)) {
				return READ_ERROR;
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

			//string cFileName = this->strConv.to_bytes(wstring(FileName));
			//printf("read %s %d %d %d %d\n", cFileName.c_str(), fileIv, this->lastBlockNum, off, len);

			// Calculate block position.
			const size_t blockSize = encfs.getBlockSize();
			const size_t blockHeaderSize = encfs.getHeaderSize();
			const size_t blockDataSize = blockSize - blockHeaderSize;
			size_t shift = off % blockDataSize;
			size_t blockNum = off / blockDataSize;
			const size_t lastBlockNum = (off + len - 1) / blockDataSize;

			int32_t copiedLen = 0;
			// Copy from buffer.
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

				// Read encrypted data.
				DWORD readLen;
				this->blockBuffer.resize(blocksLength);
				if (!ReadFile(this->handle, &this->blockBuffer[0], (DWORD)blocksLength, &readLen, NULL)) {
					this->clearBlockBuffer();
					return -1;
				}

				//printf("read2 %d %d %d %d %d\n", shift, blockNum, lastBlockNum, blocksLength, readLen);

				if (readLen > blockHeaderSize + shift) {
					for (size_t i = 0; i < readLen && len > 0; i += encfs.getBlockSize()) {
						size_t blockLen = (readLen - i) > encfs.getBlockSize() ? encfs.getBlockSize() : (readLen - i);

						this->encodeBuffer.assign((const char*)&this->blockBuffer[i], blockLen);
						this->decodeBuffer.clear();
						encfs.decodeBlock(fileIv, this->lastBlockNum = blockNum, this->encodeBuffer, this->decodeBuffer);

						blockLen = this->decodeBuffer.size() - shift;
						if (blockLen > len) {
							blockLen = len;
						}
						memcpy(buff + copiedLen, this->decodeBuffer.data() + shift, blockLen);
						shift = 0;
						len -= (DWORD)blockLen;
						copiedLen += (int32_t)blockLen;
						blockNum++;
					}
				}
				this->clearBlockBuffer();
			}
			//printf("readEnd %d\n", copiedLen);
			return copiedLen;
		}
		catch (const EncFSInvalidBlockException &ex) {
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
			const size_t blocksLength = (lastBlockNum + 1) * blockSize - blocksOffset;
			if (encfs.isUniqueIV()) {
				blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
			}
			// wprintf(L"Write %s off=%ld len=%ld blockDataSize=%ld shift=%ld blockNum=%ld lastBlockNum=%ld blocksOffset=%ld blocksLength=%ld\n",
			//	FileName, off, len, blockDataSize, shift, blockNum, lastBlockNum, blocksOffset, blocksLength);

			// Seek for write,
			LARGE_INTEGER distanceToMove;
			distanceToMove.QuadPart = blocksOffset;
			if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
				return -1;
			}

			if (shift != 0) {
				// Write to a part of block.
				//printf("write2 %d %d %d %d\n", blockNum, this->lastBlockNum, shift, this->decodeBuffer.size());
				if (blockNum != this->lastBlockNum) {
					DWORD readLen;
					this->encodeBuffer.resize(encfs.getBlockSize());
					if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
						return -1;
					}
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
			for (size_t i = 0; i < len; i += blockDataLen) {
				blockDataLen = (len - i) > blockDataSize - shift ? blockDataSize - shift : (len - i);
				if (shift != 0) {
					if (this->decodeBuffer.size() < shift + blockDataLen) {
						this->decodeBuffer.resize(shift + blockDataLen);
					}
					memcpy(&this->decodeBuffer[shift], buff, blockDataLen);
					//printf("A %d\n", this->decodeBuffer.size());
				}
				else if (blockDataLen == blockDataSize || off + i + blockDataLen >= fileSize) {
					this->decodeBuffer.assign(buff + i, blockDataLen);
				}
				else {
					DWORD readLen;
					this->encodeBuffer.resize(encfs.getBlockSize());
					if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
						return -1;
					}
					distanceToMove.QuadPart = -(LONGLONG)readLen;
					if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_CURRENT)) {
						return -1;
					}
					this->encodeBuffer.resize(readLen);
					this->decodeBuffer.clear();
					encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
					//printf("B %d %d\n", this->decodeBuffer.size(), readLen);
					memcpy(&this->decodeBuffer[0], buff + i, blockDataLen);
				}
				this->encodeBuffer.clear();
				encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);
				DWORD writtenLen;
				if (!WriteFile(this->handle, this->encodeBuffer.data(), (DWORD)this->encodeBuffer.size(), &writtenLen, NULL)) {
					return -1;
				}
				//printf("%d %d %d\n", blockNum, blockDataLen, writtenLen);
				blockNum++;
				shift = 0;
			}
			//printf("written %d\n", len);
			return len;
		}
		catch (const EncFSInvalidBlockException &ex) {
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

		int64_t fileIv = 0; // Cannot use fileIv on reverse mode.

		// Calculate position.
		// File header and block header sizes are zero on reverse mode.
		int32_t blockSize = encfs.getBlockSize();
		int32_t shift = off % blockSize;
		int64_t blockNum = off / blockSize;
		int64_t lastBlockNum = (off + len - 1) / blockSize;

		int64_t blocksOffset = blockNum * blockSize;
		int64_t blocksLength = (lastBlockNum + 1) * blockSize - blocksOffset;

		int32_t i = 0;
		int32_t blockDataLen;

		if (blockNum == this->lastBlockNum) {
			// Copy from buffer.
			blockDataLen = len > this->encodeBuffer.size() - shift ? this->encodeBuffer.size() - shift : len;
			memcpy(buff, &this->encodeBuffer[shift], blockDataLen);
			blocksOffset = blockNum++ * blockSize;
			shift = 0;
			i += blockDataLen;
			if (i >= len || this->encodeBuffer.size() < blockSize) {
				return i;
			}
		}

		// Seek for read.
		LARGE_INTEGER distanceToMove;
		distanceToMove.QuadPart = blocksOffset;
		if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
			return -1;
		}

		// Read plain data.
		DWORD readLen;
		this->blockBuffer.resize(blocksLength);
		if (!ReadFile(this->handle, &this->blockBuffer[0], (DWORD)blocksLength, &readLen, NULL)) {
			this->clearBlockBuffer(); 
			return -1;
		}
		len = readLen;
		if (i >= len) {
			this->clearBlockBuffer();
			return i;
		}

		int64_t bufferPos = 0;
		for (; i < len; i += blockDataLen) {
			blockDataLen = (len - i) > blockSize - shift ? blockSize - shift : (len - i);

			this->decodeBuffer.resize(blockDataLen + shift);
			memcpy(&this->decodeBuffer[0], &this->blockBuffer[bufferPos * blockSize], this->decodeBuffer.size());

			this->encodeBuffer.clear();
			encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);

			memcpy(buff + i, &this->encodeBuffer[shift], blockDataLen);
			// printf("encode %d %d\n", shift, blockDataLen);

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
		//printf("setLength %ld\n", length);

		if (length == 0) {
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
		LARGE_INTEGER distanceToMove;
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
			// 境界部分をデコード
			blocksOffset = blockNum * encfs.getBlockSize();
			if (encfs.isUniqueIV()) {
				blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
			}
			DWORD readLen;
			this->encodeBuffer.resize(encfs.getBlockSize());
			distanceToMove.QuadPart = blocksOffset;
			if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
				return false;
			}
			if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
				return false;
			}
			this->encodeBuffer.resize(readLen);
			this->decodeBuffer.clear();
			encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
		}

		size_t encodedLength = encfs.toEncodedLength(length);
		LARGE_INTEGER offset;
		offset.QuadPart = encodedLength;
		if (!SetFilePointerEx(this->handle, offset, NULL, FILE_BEGIN)) {
			return false;
		}
		if (!SetEndOfFile(this->handle)) {
			return false;
		}
		this->lastBlockNum = -1;

		if (shift != 0) {
			// 境界部分をエンコード
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
			if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
				return false;
			}
			DWORD writtenLen;
			if (!WriteFile(this->handle, this->encodeBuffer.data(), (DWORD)this->encodeBuffer.size(), &writtenLen, NULL)) {
				return false;
			}
		}

		// 拡大した末尾をエンコード
		if (length > fileSize) {
			shift = length % blockDataSize;
			if (shift != 0 && (fileSize == 0 || blockNum != length / blockDataSize)) {
				blockNum = length / blockDataSize;
				this->decodeBuffer.assign(shift, (char)0);
				this->encodeBuffer.clear();
				encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);
				distanceToMove.QuadPart = -(int64_t)shift - (int64_t)blockHeaderSize;
				if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_END)) {
					return false;
				}
				DWORD writtenLen;
				if (!WriteFile(this->handle, this->encodeBuffer.data(), (DWORD)this->encodeBuffer.size(), &writtenLen, NULL)) {
					return false;
				}
			}
		}
		return true;
	}

	bool EncFSFile::changeFileIV(const LPCWSTR FileName, const LPCWSTR NewFileName) {
		int64_t fileIv;
		//printf("changeFileIV\n");
		EncFSGetFileIVResult ivResult = this->getFileIV(FileName, &fileIv, false);
		if (ivResult == EMPTY) {
			return true;
		}
		if (ivResult == READ_ERROR) {
			return false;
		}
		//printf("changeFileIV A %d\n", fileIv);
		string cNewFileName = strConv.to_bytes(wstring(NewFileName));
		string encodedFileHeader;
		encfs.encodeFileIv(cNewFileName, fileIv, encodedFileHeader);
		LARGE_INTEGER distanceToMove;
		distanceToMove.QuadPart = 0;
		if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
			return false;
		}
		//printf("changeFileIV B %d\n", fileIv);
		DWORD writtenLen;
		if (!WriteFile(this->handle, encodedFileHeader.data(), (DWORD)encodedFileHeader.size(), &writtenLen, NULL)) {
			return false;
		}
		//printf("changeFileIV C %d\n", fileIv);
		return true;
	}

	void EncFSFile::clearBlockBuffer() {
		if (this->blockBuffer.capacity() > encfs.getBlockSize()) {
			this->blockBuffer.clear();
			this->blockBuffer.shrink_to_fit();
		}
	}
}