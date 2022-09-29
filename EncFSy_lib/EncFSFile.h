#pragma once
#include <dokan.h>
#include <winbase.h>

#include "EncFSVolume.h"

#include <string>
#include <codecvt>
#include <mutex>

extern EncFS::EncFSVolume encfs;

namespace EncFS
{
	enum EncFSGetFileIVResult {
		EXISTS,
		READ_ERROR,
		EMPTY
	};

	class EncFSFile {
	private:
		HANDLE handle;
		bool canRead;

		int64_t fileIv;
		bool fileIvAvailable;

		string blockBuffer;
		int64_t lastBlockNum;
		string encodeBuffer;
		string decodeBuffer;
		mutex mutexLock;
		wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;

	public:
		EncFSFile(HANDLE handle, bool canRead) {
			if (!handle || handle == INVALID_HANDLE_VALUE) {
				throw EncFSIllegalStateException();
			}
			this->handle = handle;
			this->canRead = canRead;
			this->fileIvAvailable = false;
			this->fileIv = 0L;
			this->lastBlockNum = -1;
		}

		~EncFSFile() {
			CloseHandle(this->handle);
			this->handle = INVALID_HANDLE_VALUE;
		}

		inline HANDLE getHandle() {
			return this->handle;
		}

		int32_t read(const LPCWSTR FileName, char* buff, size_t off, DWORD len);
		int32_t write(const LPCWSTR FileName, size_t fileSize, const char* buff, size_t off, DWORD len);
		int32_t reverseRead(const LPCWSTR FileName, char* buff, size_t off, DWORD len);
		bool flush();
		bool setLength(const LPCWSTR FileName, const size_t length);
		bool changeFileIV(const LPCWSTR FileName, const LPCWSTR NewFileName);

	private:
		EncFSGetFileIVResult getFileIV(const LPCWSTR FileName, int64_t *fileIv, bool create);
		bool _setLength(const LPCWSTR FileName, const size_t fileSize, const size_t length);
	};
}
