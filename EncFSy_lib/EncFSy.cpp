/*
Dokan : user - mode file system library for Windows
Copyright(C) 2015 - 2018 Adrien J. <liryna.stark@gmail.com> and Maxime C. <maxime@islog.com>
Copyright(C) 2007 - 2011 Hiroki Asakawa <info@dokan-dev.net>
http ://dokan-dev.github.io
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <dokan.h>
#include "EncFSy.h"

#include <fileinfo.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <winbase.h>

#include <string>
#include <codecvt>
#include <mutex>
#include <fstream>
#include <streambuf>

#include "EncFSFile.h"

using namespace std;

EncFS::EncFSVolume encfs;
EncFSOptions g_efo;

mutex dirMoveLock;

static void PrintF(LPCWSTR format, ...) {
	const WCHAR* outputString;
	WCHAR* buffer = NULL;
	size_t length;
	va_list argp;

	va_start(argp, format);
	length = _vscwprintf(format, argp) + 1;
	buffer = (WCHAR*)_malloca(length * sizeof(WCHAR));
	if (buffer) {
		vswprintf_s(buffer, length, format, argp);
		outputString = buffer;
	}
	else {
		outputString = format;
	}
	fputws(outputString, stderr);
	_freea(buffer);
	va_end(argp);
	fflush(stderr);
}

static void DbgPrint(LPCWSTR format, ...) {
	if (!g_efo.g_DebugMode) {
		return;
	}

	const WCHAR* outputString;
	WCHAR* buffer = NULL;
	size_t length;
	va_list argp;

	va_start(argp, format);
	length = _vscwprintf(format, argp) + 1;
	buffer = (WCHAR*)_malloca(length * sizeof(WCHAR));
	if (buffer) {
		vswprintf_s(buffer, length, format, argp);
		outputString = buffer;
	}
	else {
		outputString = format;
	}
	if (g_efo.g_UseStdErr) {
		fputws(L"EncFSy ", stderr);
		fputws(outputString, stderr);
	}
	else {
		OutputDebugStringW(outputString);
	}
	if (buffer)
		_freea(buffer);
	va_end(argp);
	if (g_efo.g_UseStdErr)
		fflush(stderr);
}

/**
 Convert virtual path to real path.
*/
static void GetFilePath(PWCHAR encodedFilePath, ULONG numberOfElements,
	LPCWSTR plainFilePath) {
	wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
	wstring wFilePath(plainFilePath);
	string cFilePath = strConv.to_bytes(wFilePath);
	string cEncodedFileName;
	if (encfs.isReverse()) {
		try {
			encfs.decodeFilePath(cFilePath, cEncodedFileName);
		}
		catch (const EncFS::EncFSInvalidBlockException &ex) {
			cEncodedFileName = cFilePath;
		}
	}
	else {
		encfs.encodeFilePath(cFilePath, cEncodedFileName);
	}
	wFilePath = strConv.from_bytes(cEncodedFileName);

	WCHAR filePath[DOKAN_MAX_PATH];
	wcscpy_s(filePath, wFilePath.c_str());

	wcsncpy_s(encodedFilePath, numberOfElements, L"\\\\?\\", 4);
	wcsncat_s(encodedFilePath, numberOfElements, g_efo.RootDirectory, wcslen(g_efo.RootDirectory));
	size_t unclen = wcslen(g_efo.UNCName);
	if (unclen > 0 && _wcsnicmp(filePath, g_efo.UNCName, unclen) == 0) {
		if (_wcsnicmp(filePath + unclen, L".", 1) != 0) {
			wcsncat_s(encodedFilePath, numberOfElements, filePath + unclen,
				wcslen(filePath) - unclen);
		}
	}
	else {
		wcsncat_s(encodedFilePath, numberOfElements, filePath, wcslen(filePath));
	}
}

static void PrintUserName(PDOKAN_FILE_INFO DokanFileInfo) {
	HANDLE handle;
	UCHAR buffer[1024];
	DWORD returnLength;
	WCHAR accountName[256];
	WCHAR domainName[256];
	DWORD accountLength = sizeof(accountName) / sizeof(WCHAR);
	DWORD domainLength = sizeof(domainName) / sizeof(WCHAR);
	PTOKEN_USER tokenUser;
	SID_NAME_USE snu;

	if (!g_efo.g_DebugMode)
		return;

	handle = DokanOpenRequestorToken(DokanFileInfo);
	if (handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"  DokanOpenRequestorToken failed\n");
		return;
	}

	if (!GetTokenInformation(handle, TokenUser, buffer, sizeof(buffer),
		&returnLength)) {
		DbgPrint(L"  GetTokenInformaiton failed: %d\n", GetLastError());
		CloseHandle(handle);
		return;
	}

	CloseHandle(handle);

	tokenUser = (PTOKEN_USER)buffer;
	if (!LookupAccountSid(NULL, tokenUser->User.Sid, accountName, &accountLength,
		domainName, &domainLength, &snu)) {
		DbgPrint(L"  LookupAccountSid failed: %d\n", GetLastError());
		return;
	}

	DbgPrint(L"  AccountName: %s, DomainName: %s\n", accountName, domainName);
}

#define EncFSCheckFlag(val, flag)                                             \
  if (val & flag) {                                                            \
    DbgPrint(L"\t" L#flag L"\n");                                              \
  }

static NTSTATUS DOKAN_CALLBACK
EncFSCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
	ACCESS_MASK DesiredAccess, ULONG FileAttributes,
	ULONG ShareAccess, ULONG CreateDisposition,
	ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo) {

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE handle;
	DWORD fileAttr;
	NTSTATUS status = STATUS_SUCCESS;
	DWORD creationDisposition;
	DWORD fileAttributesAndFlags;
	DWORD error = 0;
	SECURITY_ATTRIBUTES securityAttrib;
	ACCESS_MASK genericDesiredAccess;
	// userTokenHandle is for Impersonate Caller User Option
	HANDLE userTokenHandle = 0;

	securityAttrib.nLength = sizeof(securityAttrib);
	securityAttrib.lpSecurityDescriptor =
		SecurityContext->AccessState.SecurityDescriptor;
	securityAttrib.bInheritHandle = FALSE;

	DokanMapKernelToUserCreateFileFlags(
		DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
		&genericDesiredAccess, &fileAttributesAndFlags, &creationDisposition);

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"CreateFile : %s\n", filePath);
	//PrintF(L"CreateFile : %s\n%s\n", FileName, filePath);

	PrintUserName(DokanFileInfo);

	/*
	if (ShareMode == 0 && AccessMode & FILE_WRITE_DATA)
	ShareMode = FILE_SHARE_WRITE;
	else if (ShareMode == 0)
	ShareMode = FILE_SHARE_READ;
	*/

	DbgPrint(L"\tShareMode = 0x%x\n", ShareAccess);

	EncFSCheckFlag(ShareAccess, FILE_SHARE_READ);
	EncFSCheckFlag(ShareAccess, FILE_SHARE_WRITE);
	EncFSCheckFlag(ShareAccess, FILE_SHARE_DELETE);

	DbgPrint(L"\tDesiredAccess = 0x%x\n", DesiredAccess);

	EncFSCheckFlag(DesiredAccess, GENERIC_READ);
	EncFSCheckFlag(DesiredAccess, GENERIC_WRITE);
	EncFSCheckFlag(DesiredAccess, GENERIC_EXECUTE);

	EncFSCheckFlag(DesiredAccess, DELETE);
	EncFSCheckFlag(DesiredAccess, FILE_READ_DATA);
	EncFSCheckFlag(DesiredAccess, FILE_READ_ATTRIBUTES);
	EncFSCheckFlag(DesiredAccess, FILE_READ_EA);
	EncFSCheckFlag(DesiredAccess, READ_CONTROL);
	EncFSCheckFlag(DesiredAccess, FILE_WRITE_DATA);
	EncFSCheckFlag(DesiredAccess, FILE_WRITE_ATTRIBUTES);
	EncFSCheckFlag(DesiredAccess, FILE_WRITE_EA);
	EncFSCheckFlag(DesiredAccess, FILE_APPEND_DATA);
	EncFSCheckFlag(DesiredAccess, WRITE_DAC);
	EncFSCheckFlag(DesiredAccess, WRITE_OWNER);
	EncFSCheckFlag(DesiredAccess, SYNCHRONIZE);
	EncFSCheckFlag(DesiredAccess, FILE_EXECUTE);
	EncFSCheckFlag(DesiredAccess, STANDARD_RIGHTS_READ);
	EncFSCheckFlag(DesiredAccess, STANDARD_RIGHTS_WRITE);
	EncFSCheckFlag(DesiredAccess, STANDARD_RIGHTS_EXECUTE);

	// When filePath is a directory, needs to change the flag so that the file can
	// be opened.
	fileAttr = GetFileAttributesW(filePath);

	if (fileAttr != INVALID_FILE_ATTRIBUTES) {
		if (fileAttr & FILE_ATTRIBUTE_DIRECTORY) {
			if (!(CreateOptions & FILE_NON_DIRECTORY_FILE)) {
				DokanFileInfo->IsDirectory = TRUE;
				// Needed by FindFirstFile to list files in it
				// TODO: use ReOpenFile in EncFSFindFiles to set share read temporary
				ShareAccess |= FILE_SHARE_READ;
			}
			else { // FILE_NON_DIRECTORY_FILE - Cannot open a dir as a file
				DbgPrint(L"\tCannot open a dir as a file\n");
				return STATUS_FILE_IS_A_DIRECTORY;
			}
		}
	}

	DbgPrint(L"\tFlagsAndAttributes = 0x%x\n", fileAttributesAndFlags);

	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_ARCHIVE);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_COMPRESSED);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_DEVICE);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_DIRECTORY);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_ENCRYPTED);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_HIDDEN);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_INTEGRITY_STREAM);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NORMAL);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NO_SCRUB_DATA);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_OFFLINE);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_READONLY);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_REPARSE_POINT);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_SPARSE_FILE);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_SYSTEM);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_TEMPORARY);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_VIRTUAL);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_WRITE_THROUGH);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_OVERLAPPED);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_NO_BUFFERING);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_RANDOM_ACCESS);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_SEQUENTIAL_SCAN);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_DELETE_ON_CLOSE);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_BACKUP_SEMANTICS);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_POSIX_SEMANTICS);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_OPEN_REPARSE_POINT);
	EncFSCheckFlag(fileAttributesAndFlags, FILE_FLAG_OPEN_NO_RECALL);
	EncFSCheckFlag(fileAttributesAndFlags, SECURITY_ANONYMOUS);
	EncFSCheckFlag(fileAttributesAndFlags, SECURITY_IDENTIFICATION);
	EncFSCheckFlag(fileAttributesAndFlags, SECURITY_IMPERSONATION);
	EncFSCheckFlag(fileAttributesAndFlags, SECURITY_DELEGATION);
	EncFSCheckFlag(fileAttributesAndFlags, SECURITY_CONTEXT_TRACKING);
	EncFSCheckFlag(fileAttributesAndFlags, SECURITY_EFFECTIVE_ONLY);
	EncFSCheckFlag(fileAttributesAndFlags, SECURITY_SQOS_PRESENT);

	if (creationDisposition == CREATE_NEW) {
		DbgPrint(L"\tCREATE_NEW\n");
	}
	else if (creationDisposition == OPEN_ALWAYS) {
		DbgPrint(L"\tOPEN_ALWAYS\n");
	}
	else if (creationDisposition == CREATE_ALWAYS) {
		DbgPrint(L"\tCREATE_ALWAYS\n");
	}
	else if (creationDisposition == OPEN_EXISTING) {
		DbgPrint(L"\tOPEN_EXISTING\n");
	}
	else if (creationDisposition == TRUNCATE_EXISTING) {
		DbgPrint(L"\tTRUNCATE_EXISTING\n");
	}
	else {
		DbgPrint(L"\tUNKNOWN creationDisposition!\n");
	}

	if (g_efo.g_ImpersonateCallerUser) {
		userTokenHandle = DokanOpenRequestorToken(DokanFileInfo);

		if (userTokenHandle == INVALID_HANDLE_VALUE) {
			DbgPrint(L"  DokanOpenRequestorToken failed\n");
			// Should we return some error?
		}
	}

	if (DokanFileInfo->IsDirectory) {
		// It is a create directory request
		if (creationDisposition == CREATE_NEW ||
			creationDisposition == OPEN_ALWAYS) {

			if (g_efo.g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
				// if g_ImpersonateCallerUser option is on, call the ImpersonateLoggedOnUser function.
				if (!ImpersonateLoggedOnUser(userTokenHandle)) {
					// handle the error if failed to impersonate
					DbgPrint(L"\tImpersonateLoggedOnUser failed.\n");
				}
			}

			//We create folder
			if (!CreateDirectory(filePath, &securityAttrib)) {
				error = GetLastError();
				// Fail to create folder for OPEN_ALWAYS is not an error
				if (error != ERROR_ALREADY_EXISTS ||
					creationDisposition == CREATE_NEW) {
					DbgPrint(L"\terror code = %d\n\n", error);
					status = DokanNtStatusFromWin32(error);
				}
			}

			if (g_efo.g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
				// Clean Up operation for impersonate
				DWORD lastError = GetLastError();
				if (status != STATUS_SUCCESS) //Keep the handle open for CreateFile
					CloseHandle(userTokenHandle);
				RevertToSelf();
				SetLastError(lastError);
			}
		}

		if (status == STATUS_SUCCESS) {
			//Check first if we're trying to open a file as a directory.
			if (fileAttr != INVALID_FILE_ATTRIBUTES &&
				!(fileAttr & FILE_ATTRIBUTE_DIRECTORY) &&
				(CreateOptions & FILE_DIRECTORY_FILE)) {
				return STATUS_NOT_A_DIRECTORY;
			}

			if (g_efo.g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
				// if g_ImpersonateCallerUser option is on, call the ImpersonateLoggedOnUser function.
				if (!ImpersonateLoggedOnUser(userTokenHandle)) {
					// handle the error if failed to impersonate
					DbgPrint(L"\tImpersonateLoggedOnUser failed.\n");
				}
			}

			// FILE_FLAG_BACKUP_SEMANTICS is required for opening directory handles
			handle =
				CreateFileW(filePath, genericDesiredAccess, ShareAccess,
					&securityAttrib, OPEN_EXISTING,
					fileAttributesAndFlags | FILE_FLAG_BACKUP_SEMANTICS, NULL);

			if (g_efo.g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
				// Clean Up operation for impersonate
				DWORD lastError = GetLastError();
				CloseHandle(userTokenHandle);
				RevertToSelf();
				SetLastError(lastError);
			}
			if (handle == INVALID_HANDLE_VALUE) {
				error = GetLastError();
				DbgPrint(L"\terror code = %d\n\n", error);

				status = DokanNtStatusFromWin32(error);
			}
			else {
				DokanFileInfo->Context =
					(ULONG64)new EncFS::EncFSFile(handle, false); // save the file handle in Context
																  // Open succeed but we need to inform the driver
																  // that the dir open and not created by returning STATUS_OBJECT_NAME_COLLISION
				if (creationDisposition == OPEN_ALWAYS &&
					fileAttr != INVALID_FILE_ATTRIBUTES)
					return STATUS_OBJECT_NAME_COLLISION;
			}
		}
	}
	else {
		// It is a create file request

		// Cannot overwrite a hidden or system file if flag not set
		if (fileAttr != INVALID_FILE_ATTRIBUTES &&
			((!(fileAttributesAndFlags & FILE_ATTRIBUTE_HIDDEN) &&
			(fileAttr & FILE_ATTRIBUTE_HIDDEN)) ||
				(!(fileAttributesAndFlags & FILE_ATTRIBUTE_SYSTEM) &&
				(fileAttr & FILE_ATTRIBUTE_SYSTEM))) &&
					(creationDisposition == TRUNCATE_EXISTING ||
						creationDisposition == CREATE_ALWAYS))
			return STATUS_ACCESS_DENIED;

		// Cannot delete a read only file
		if ((fileAttr != INVALID_FILE_ATTRIBUTES &&
			(fileAttr & FILE_ATTRIBUTE_READONLY) ||
			(fileAttributesAndFlags & FILE_ATTRIBUTE_READONLY)) &&
			(fileAttributesAndFlags & FILE_FLAG_DELETE_ON_CLOSE))
			return STATUS_CANNOT_DELETE;

		// Truncate should always be used with write access
		if (creationDisposition == TRUNCATE_EXISTING)
			genericDesiredAccess |= GENERIC_WRITE;

		if (g_efo.g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
			// if g_ImpersonateCallerUser option is on, call the ImpersonateLoggedOnUser function.
			if (!ImpersonateLoggedOnUser(userTokenHandle)) {
				// handle the error if failed to impersonate
				DbgPrint(L"\tImpersonateLoggedOnUser failed.\n");
			}
		}

		// ファイルヘッダを見るため、書込み可能なファイルは全て読める
		if (genericDesiredAccess & GENERIC_WRITE) {
			genericDesiredAccess |= GENERIC_READ;
		}

		// ヘッダの修正のため削除・移動可能なファイルは全て読み書きできる
		if (genericDesiredAccess & DELETE) {
			genericDesiredAccess |= GENERIC_READ | GENERIC_WRITE;
		}

		// FILE_ATTRIBUTE_NORMALは単独のみ有効
		if (fileAttributesAndFlags & FILE_ATTRIBUTE_NORMAL) {
			fileAttributesAndFlags = FILE_ATTRIBUTE_NORMAL;
		}

		handle = CreateFileW(
			filePath,
			genericDesiredAccess, // GENERIC_READ|GENERIC_WRITE|GENERIC_EXECUTE,
			ShareAccess,
			&securityAttrib, // security attribute
			creationDisposition,
			fileAttributesAndFlags, // |FILE_FLAG_NO_BUFFERING,
			NULL);                  // template file handle

		if (g_efo.g_ImpersonateCallerUser && userTokenHandle != INVALID_HANDLE_VALUE) {
			// Clean Up operation for impersonate
			DWORD lastError = GetLastError();
			CloseHandle(userTokenHandle);
			RevertToSelf();
			SetLastError(lastError);
		}

		if (handle == INVALID_HANDLE_VALUE) {
			error = GetLastError();
			DbgPrint(L"\terror code = %d\n\n", error);

			status = DokanNtStatusFromWin32(error);
		}
		else {

			//Need to update FileAttributes with previous when Overwrite file
			if (fileAttr != INVALID_FILE_ATTRIBUTES &&
				creationDisposition == TRUNCATE_EXISTING) {
				SetFileAttributesW(filePath, fileAttributesAndFlags | fileAttr);
			}

			DokanFileInfo->Context =
				(ULONG64)new EncFS::EncFSFile(handle, true); // save the file handle in Context

			if (creationDisposition == OPEN_ALWAYS ||
				creationDisposition == CREATE_ALWAYS) {
				error = GetLastError();
				if (error == ERROR_ALREADY_EXISTS) {
					DbgPrint(L"\tOpen an already existing file\n");
					// Open succeed but we need to inform the driver
					// that the file open and not created by returning STATUS_OBJECT_NAME_COLLISION
					status = STATUS_OBJECT_NAME_COLLISION;
				}
			}
		}
	}

	//PrintF(L"CreateFileEnd %d\n", status);
	return status;
}

#pragma warning(push)
#pragma warning(disable : 4305)

static void DOKAN_CALLBACK EncFSCloseFile(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo) {
	lock_guard<decltype(dirMoveLock)> dlock(dirMoveLock);
	WCHAR filePath[DOKAN_MAX_PATH];
	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);
	if (DokanFileInfo->Context) {
		DbgPrint(L"CloseFile: %s %s\n", FileName, filePath);
		DbgPrint(L"\terror : not cleanuped file\n\n");
		EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
		// CleanupとCloseFileが同時に実行されるとDokanFileInfoが別々でDokanFileInfo->Contextが同じになってしまう現象が起こる HACK
		//printf("delA %x %x %x\n", DokanFileInfo->Context, DokanFileInfo->ProcessId, DokanFileInfo);
		DokanFileInfo->Context = 0;
		if (encfsFile->getHandle() != INVALID_HANDLE_VALUE)
			delete encfsFile;
		else
			DbgPrint(L"Close: encfs invalid handle%s %s\n", FileName, filePath);
	}
	else {
		DbgPrint(L"Close: %s\n\n", filePath);
	}
}

static void DOKAN_CALLBACK EncFSCleanup(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo) {
	lock_guard<decltype(dirMoveLock)> dlock(dirMoveLock);
	WCHAR filePath[DOKAN_MAX_PATH];
	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);
	if (DokanFileInfo->Context) {
		DbgPrint(L"Cleanup: %s %s\n", FileName, filePath);
		EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
		DokanFileInfo->Context = 0;
		//printf("delB %x %x %x\n", encfsFile, DokanFileInfo->ProcessId, DokanFileInfo);
		if (encfsFile->getHandle() != INVALID_HANDLE_VALUE)
			delete encfsFile;
		else
			DbgPrint(L"Cleanup: encfs invalid handle%s %s\n", FileName, filePath);
	}
	else {
		DbgPrint(L"Cleanup: %s\n\tinvalid handle\n\n", filePath);
	}

	if (DokanFileInfo->DeleteOnClose) {
		// Should already be deleted by CloseHandle
		// if open with FILE_FLAG_DELETE_ON_CLOSE
		DbgPrint(L"\tDeleteOnClose\n");
		if (DokanFileInfo->IsDirectory) {
			DbgPrint(L"  DeleteDirectory ");
			if (!RemoveDirectoryW(filePath)) {
				DbgPrint(L"error code = %d\n\n", GetLastError());
			}
			else {
				DbgPrint(L"success\n\n");
			}
		}
		else {
			DbgPrint(L"  DeleteFile ");
			if (DeleteFileW(filePath) == 0) {
				DbgPrint(L" error code = %d\n\n", GetLastError());
			}
			else {
				DbgPrint(L"success\n\n");
			}
		}
	}
}

static NTSTATUS DOKAN_CALLBACK EncFSReadFile(LPCWSTR FileName, LPVOID Buffer,
	DWORD BufferLength,
	LPDWORD ReadLength,
	LONGLONG Offset,
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];
	ULONG offset = (ULONG)Offset;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"ReadFile : %s\n", filePath);

	EncFS::EncFSFile* encfsFile;
	BOOL opened = FALSE;
	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle, cleanuped?\n");
		HANDLE handle = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, 0, NULL);
		if (handle == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			DbgPrint(L"\tCreateFile error : %d\n\n", error);
			return DokanNtStatusFromWin32(error);
		}
		encfsFile = new EncFS::EncFSFile(handle, true);
		opened = TRUE;
	}
	else {
		encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
	}

	int32_t readLen;
	if (encfs.isReverse()) {
		size_t len = wcslen(filePath);
		if (len >= 12 && wcscmp(filePath + len - 12, L"\\.encfs6.xml") == 0) {
			LARGE_INTEGER distanceToMove;
			distanceToMove.QuadPart = Offset;
			if (!SetFilePointerEx(encfsFile->getHandle(), distanceToMove, NULL, FILE_BEGIN)) {
				DWORD error = GetLastError();
				DbgPrint(L"\tseek error, offset = %d\n\n", offset);
				if (opened) {
					delete encfsFile;
				}
				return DokanNtStatusFromWin32(error);
			}
			else if (!ReadFile(encfsFile->getHandle(), Buffer, BufferLength, ReadLength, NULL)) {
				readLen = -1;
			}
			else {
				readLen = *ReadLength;
			}
		}
		else {
			readLen = encfsFile->reverseRead(FileName, (char*)Buffer, offset, BufferLength);
		}
	}
	else {
		readLen = encfsFile->read(FileName, (char*)Buffer, offset, BufferLength);
	}
	if (readLen == -1) {
		DWORD error = GetLastError();
		DbgPrint(L"\tread error = %u, buffer length = %d, read length = %d, offset = %d\n\n",
			error, BufferLength, readLen, offset);
		if (opened) {
			delete encfsFile;
		}
		return DokanNtStatusFromWin32(error);

	}
	*ReadLength = readLen;
	DbgPrint(L"\tByte to read: %d, Byte read %d, offset %d\n\n", BufferLength,
		*ReadLength, offset);

	if (opened) {
		delete encfsFile;
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSWriteFile(LPCWSTR FileName, LPCVOID Buffer,
	DWORD NumberOfBytesToWrite,
	LPDWORD NumberOfBytesWritten,
	LONGLONG Offset,
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"WriteFile : %s, offset %I64d, length %d\n", filePath, Offset,
		NumberOfBytesToWrite);

	// reopen the file
	UINT64 fileSize = 0;
	EncFS::EncFSFile* encfsFile;
	BOOL opened = FALSE;
	{
		if (!DokanFileInfo->Context) {
			DbgPrint(L"\tinvalid handle, cleanuped?\n");
			HANDLE handle = CreateFileW(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
				OPEN_EXISTING, 0, NULL);
			if (handle == INVALID_HANDLE_VALUE) {
				DWORD error = GetLastError();
				DbgPrint(L"\tCreateFile error : %d\n\n", error);
				return DokanNtStatusFromWin32(error);
			}
			encfsFile = new EncFS::EncFSFile(handle, false);
			opened = TRUE;
		}
		else {
			encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
		}

		LARGE_INTEGER li;
		if (GetFileSizeEx(encfsFile->getHandle(), &li)) {
			fileSize = li.QuadPart;
		}
		else {
			DWORD error = GetLastError();
			DbgPrint(L"\terror code = %d\n\n", error);
			return DokanNtStatusFromWin32(error);
		}
		fileSize = encfs.toDecodedLength(fileSize);
	}

	size_t off;
	if (DokanFileInfo->WriteToEndOfFile) {
		off = fileSize;
	}
	else {
		// Paging IO cannot write after allocate file size.
		if (DokanFileInfo->PagingIo) {
			if ((UINT64)Offset >= fileSize) {
				*NumberOfBytesWritten = 0;
				if (opened) {
					delete encfsFile;
				}
				return STATUS_SUCCESS;
			}

			if (((UINT64)Offset + NumberOfBytesToWrite) > fileSize) {
				UINT64 bytes = fileSize - Offset;
				if (bytes >> 32) {
					NumberOfBytesToWrite = (DWORD)(bytes & 0xFFFFFFFFUL);
				}
				else {
					NumberOfBytesToWrite = (DWORD)bytes;
				}
			}
		}

		off = Offset;
	}

	int32_t writtenLen = encfsFile->write(FileName, fileSize, (char*)Buffer, off, NumberOfBytesToWrite);
	if (writtenLen == -1) {
		DWORD error = GetLastError();
		DbgPrint(L"\twrite error = %u, buffer length = %d, write length = %d\n",
			error, NumberOfBytesToWrite, writtenLen);
		if (opened) {
			delete encfsFile;
		}
		return DokanNtStatusFromWin32(error);

	}
	else {
		DbgPrint(L"\twrite %d, offset %I64d\n\n", writtenLen, Offset);
	}
	*NumberOfBytesWritten = writtenLen;

	// close the file when it is reopened
	if (opened) {
		delete encfsFile;
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
EncFSFindFiles(LPCWSTR FileName,
	PFillFindData FillFindData, // function pointer
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];
	size_t fileLen;
	HANDLE hFind;
	WIN32_FIND_DATAW findData;
	DWORD error;
	int count = 0;


	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"FindFiles : %s\n", filePath);

	fileLen = wcslen(filePath);
	if (filePath[fileLen - 1] != L'\\') {
		filePath[fileLen++] = L'\\';
	}
	if (fileLen + 1 >= DOKAN_MAX_PATH)
		return STATUS_BUFFER_OVERFLOW;
	filePath[fileLen] = L'*';
	filePath[fileLen + 1] = L'\0';

	hFind = FindFirstFileW(filePath, &findData);

	if (hFind == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		DbgPrint(L"\tinvalid file handle. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	wstring wPath(FileName);
	wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
	string cPath = strConv.to_bytes(wPath);

	// Root folder does not have . and .. folder - we remove them
	BOOLEAN rootFolder = (wcscmp(FileName, L"\\") == 0);
	do {
		if (!rootFolder || (wcscmp(findData.cFileName, L".") != 0 &&
			wcscmp(findData.cFileName, L"..") != 0)) {

			// Decrypt file name
			wstring wcFileName(findData.cFileName);
			string ccFileName = strConv.to_bytes(wcFileName);
			string cPlainFileName;
			try {
				if (encfs.isReverse()) {
					// Encrypt when reverse mode.
					if (wcscmp(findData.cFileName, L".encfs6.xml") != 0) {
						encfs.encodeFileName(ccFileName, cPath, cPlainFileName);
					}
					else {
						cPlainFileName = ccFileName;
					}
				}
				else {
					encfs.decodeFileName(ccFileName, cPath, cPlainFileName);
				}
			}
			catch (const EncFS::EncFSInvalidBlockException &ex) {
				continue;
			}
			wstring wPlainFileName = strConv.from_bytes(cPlainFileName);
			wcscpy_s(findData.cFileName, wPlainFileName.c_str());
			findData.cAlternateFileName[0] = 0;

			// Calculate file size
			int64_t size = (findData.nFileSizeHigh * ((int64_t)MAXDWORD + 1)) + findData.nFileSizeLow;
			size = encfs.isReverse() ? encfs.toEncodedLength(size) : encfs.toDecodedLength(size);
			findData.nFileSizeLow = size & MAXDWORD;
			findData.nFileSizeHigh = (size >> 32) & MAXDWORD;

			FillFindData(&findData, DokanFileInfo);
		}
		count++;
	} while (FindNextFileW(hFind, &findData) != 0);

	error = GetLastError();
	FindClose(hFind);

	if (error != ERROR_NO_MORE_FILES) {
		DbgPrint(L"\tFindNextFile error. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tFindFiles return %d entries in %s\n\n", count, filePath);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
EncFSDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE hFind;
	WIN32_FIND_DATAW findData;
	size_t fileLen;

	ZeroMemory(filePath, sizeof(filePath));
	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"DeleteDirectory %s - %d\n", filePath,
		DokanFileInfo->DeleteOnClose);

	if (!DokanFileInfo->DeleteOnClose)
		//Dokan notify that the file is requested not to be deleted.
		return STATUS_SUCCESS;

	fileLen = wcslen(filePath);
	if (filePath[fileLen - 1] != L'\\') {
		filePath[fileLen++] = L'\\';
	}
	if (fileLen + 1 >= DOKAN_MAX_PATH)
		return STATUS_BUFFER_OVERFLOW;
	filePath[fileLen] = L'*';
	filePath[fileLen + 1] = L'\0';

	hFind = FindFirstFileW(filePath, &findData);

	if (hFind == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		DbgPrint(L"\tDeleteDirectory error code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	do {
		if (wcscmp(findData.cFileName, L"..") != 0 &&
			wcscmp(findData.cFileName, L".") != 0) {
			FindClose(hFind);
			DbgPrint(L"\tDirectory is not empty: %s\n", findData.cFileName);
			return STATUS_DIRECTORY_NOT_EMPTY;
		}
	} while (FindNextFileW(hFind, &findData) != 0);

	DWORD error = GetLastError();

	FindClose(hFind);

	if (error != ERROR_NO_MORE_FILES) {
		DbgPrint(L"\tDeleteDirectory error code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	return STATUS_SUCCESS;
}

static NTSTATUS changeIVRecursive(LPCWSTR newFilePath, const string cOldPlainDirPath, const string cNewPlainDirPath) {
	WCHAR findPath[DOKAN_MAX_PATH];
	wcscpy_s(findPath, newFilePath);
	wcscat_s(findPath, L"\\*.*");
	WCHAR oldPath[DOKAN_MAX_PATH];
	wcscpy_s(oldPath, newFilePath);
	wcscat_s(oldPath, L"\\");
	size_t oldPathLen = wcslen(oldPath);
	WCHAR newPath[DOKAN_MAX_PATH];
	wcscpy_s(newPath, oldPath);

	DbgPrint(L"ChangeIV: %s %s\n", cOldPlainDirPath, cNewPlainDirPath);

	WIN32_FIND_DATAW find;
	ZeroMemory(&find, sizeof(WIN32_FIND_DATAW));
	HANDLE findHandle = FindFirstFileW(findPath, &find);
	if (findHandle == INVALID_HANDLE_VALUE) {
		DWORD error = GetLastError();
		return DokanNtStatusFromWin32(error);
	}
	wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
	do {
		if (find.cFileName[0] == L'.') {
			continue;
		}
		wstring wOldName(find.cFileName);
		string cOldName = strConv.to_bytes(wOldName);
		string plainName;
		try {
			encfs.decodeFileName(cOldName, cOldPlainDirPath, plainName);
		}
		catch (const EncFS::EncFSInvalidBlockException &ex) {
			continue;
		}
		string cNewName;
		encfs.encodeFileName(plainName, cNewPlainDirPath, cNewName);
		wstring wNewName = strConv.from_bytes(cNewName);
		wcscpy_s((wchar_t*)&oldPath[oldPathLen], DOKAN_MAX_PATH - oldPathLen, find.cFileName);
		wcscpy_s((wchar_t*)&newPath[oldPathLen], DOKAN_MAX_PATH - oldPathLen, wNewName.c_str());
		//PrintF(L"A %s %s\n", oldPath, newPath);
		if (encfs.isChainedNameIV()) {
			if (!MoveFileW(oldPath, newPath)) {
				FindClose(findHandle);
				DWORD error = GetLastError();
				return DokanNtStatusFromWin32(error);
			}
		}
		string cPlainOldPath = cOldPlainDirPath + "\\" + plainName;
		wstring wPlainOldPath = strConv.from_bytes(cPlainOldPath);
		string cPlainNewPath = cNewPlainDirPath + "\\" + plainName;
		wstring wPlainNewPath = strConv.from_bytes(cPlainNewPath);
		//PrintF(L"B %s %s\n", wPlainOldPath.c_str(), wPlainNewPath.c_str());
		if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			// ディレクトリ
			NTSTATUS status = changeIVRecursive(newPath, cPlainOldPath, cPlainNewPath);
			if (status != STATUS_SUCCESS) {
				//PrintF(L"e %s %s %d\n", wPlainOldPath.c_str(), wPlainNewPath.c_str(), status);
				FindClose(findHandle);
				return status;
			}
		}
		else {
			// ファイル
			if (encfs.isExternalIVChaining()) {
				HANDLE handle2 = CreateFileW(newPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
					OPEN_EXISTING, 0, NULL);
				if (handle2 == INVALID_HANDLE_VALUE) {
					FindClose(findHandle);
					DWORD error = GetLastError();
					return DokanNtStatusFromWin32(error);
				}

				EncFS::EncFSFile encfsFile2(handle2, false);
				if (!encfsFile2.changeFileIV(wPlainOldPath.c_str(), wPlainNewPath.c_str())) {
					FindClose(findHandle);
					DWORD error = GetLastError();
					return DokanNtStatusFromWin32(error);
				}
			}
		}
	} while (FindNextFileW(findHandle, &find));
	FindClose(findHandle);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
EncFSMoveFile(LPCWSTR FileName, // existing file name
	LPCWSTR NewFileName, BOOL ReplaceIfExisting,
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];
	WCHAR newFilePath[DOKAN_MAX_PATH];
	DWORD bufferSize;
	BOOL result;
	size_t newFilePathLen;

	PFILE_RENAME_INFO renameInfo = NULL;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);
	GetFilePath(newFilePath, DOKAN_MAX_PATH, NewFileName);

	DbgPrint(L"MoveFile %s -> %s\n\n", filePath, newFilePath);
	//PrintF(L"MoveFile %s -> %s\n", filePath, newFilePath);

	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}
	EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
	if (encfs.isChainedNameIV() || encfs.isExternalIVChaining()) {
		if (DokanFileInfo->IsDirectory) {
			// ディレクトル内のすべてのファイルを奥に向かってIVを書き換える
			// ディレクトリの移動は時間がかかっても単一スレッドで行う
			lock_guard<decltype(dirMoveLock)> dlock(dirMoveLock);

			DokanFileInfo->Context = 0;
			delete encfsFile;
			// ディレクトリ内がロックされていなければここで移動に成功する
			if (!MoveFileW(filePath, newFilePath)) {
				DWORD error = GetLastError();
				return DokanNtStatusFromWin32(error);
			}

			wstring wOldPlainDirPath(FileName);
			wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
			string cOldPlainDirPath = strConv.to_bytes(wOldPlainDirPath);
			wstring wNewPlainDirPath(NewFileName);
			string cNewPlainDirPath = strConv.to_bytes(wNewPlainDirPath);
			NTSTATUS status = changeIVRecursive(newFilePath, cOldPlainDirPath, cNewPlainDirPath);
			//PrintF(L"MoveDirEnd\n");
			return status;
		}
		else {
			// ファイルのIVを書き換える
			if (encfs.isExternalIVChaining()) {
				if (!encfsFile->changeFileIV(FileName, NewFileName)) {
					DWORD error = GetLastError();
					return DokanNtStatusFromWin32(error);
				}
			}
		}
	}

	newFilePathLen = wcslen(newFilePath);

	// the PFILE_RENAME_INFO struct has space for one WCHAR for the name at
	// the end, so that
	// accounts for the null terminator

	bufferSize = (DWORD)(sizeof(FILE_RENAME_INFO) +
		newFilePathLen * sizeof(newFilePath[0]));

	renameInfo = (PFILE_RENAME_INFO)malloc(bufferSize);
	if (!renameInfo) {
		return STATUS_BUFFER_OVERFLOW;
	}
	ZeroMemory(renameInfo, bufferSize);

	renameInfo->ReplaceIfExists =
		ReplaceIfExisting
		? TRUE
		: FALSE; // some warning about converting BOOL to BOOLEAN
	renameInfo->RootDirectory = NULL; // hope it is never needed, shouldn't be
	renameInfo->FileNameLength =
		(DWORD)newFilePathLen *
		sizeof(newFilePath[0]); // they want length in bytes

	wcscpy_s(renameInfo->FileName, newFilePathLen + 1, newFilePath);

	result = SetFileInformationByHandle(encfsFile->getHandle(), FileRenameInfo, renameInfo,
		bufferSize);

	free(renameInfo);

	//PrintF(L"MoveEnd\n");
	if (result) {
		return STATUS_SUCCESS;
	}
	else {
		DWORD error = GetLastError();
		DbgPrint(L"\tMoveFile error = %u\n", error);
		return DokanNtStatusFromWin32(error);
	}
}

static NTSTATUS DOKAN_CALLBACK EncFSLockFile(LPCWSTR FileName,
	LONGLONG ByteOffset,
	LONGLONG Length,
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];
	LARGE_INTEGER offset;
	LARGE_INTEGER length;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"LockFile %s\n", filePath);
	
	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}
	EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;

	length.QuadPart = Length;
	offset.QuadPart = ByteOffset;

	if (!LockFile(encfsFile->getHandle(), offset.LowPart, offset.HighPart, length.LowPart,
		length.HighPart)) {
		DWORD error = GetLastError();
		DbgPrint(L"\terror code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tsuccess\n\n");
	return STATUS_SUCCESS;
}

// passthrough functions.

static BOOL AddSeSecurityNamePrivilege() {
	HANDLE token = 0;
	DbgPrint(
		L"## Attempting to add SE_SECURITY_NAME privilege to process token ##\n");
	DWORD err;
	LUID luid;
	if (!LookupPrivilegeValue(0, SE_SECURITY_NAME, &luid)) {
		err = GetLastError();
		if (err != ERROR_SUCCESS) {
			DbgPrint(L"  failed: Unable to lookup privilege value. error = %u\n",
				err);
			return FALSE;
		}
	}

	LUID_AND_ATTRIBUTES attr;
	attr.Attributes = SE_PRIVILEGE_ENABLED;
	attr.Luid = luid;

	TOKEN_PRIVILEGES priv;
	priv.PrivilegeCount = 1;
	priv.Privileges[0] = attr;

	if (!OpenProcessToken(GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
		err = GetLastError();
		if (err != ERROR_SUCCESS) {
			DbgPrint(L"  failed: Unable obtain process token. error = %u\n", err);
			return FALSE;
		}
	}

	TOKEN_PRIVILEGES oldPriv;
	DWORD retSize;
	AdjustTokenPrivileges(token, FALSE, &priv, sizeof(TOKEN_PRIVILEGES), &oldPriv,
		&retSize);
	err = GetLastError();
	if (err != ERROR_SUCCESS) {
		DbgPrint(L"  failed: Unable to adjust token privileges: %u\n", err);
		CloseHandle(token);
		return FALSE;
	}

	BOOL privAlreadyPresent = FALSE;
	for (unsigned int i = 0; i < oldPriv.PrivilegeCount; i++) {
		if (oldPriv.Privileges[i].Luid.HighPart == luid.HighPart &&
			oldPriv.Privileges[i].Luid.LowPart == luid.LowPart) {
			privAlreadyPresent = TRUE;
			break;
		}
	}
	DbgPrint(privAlreadyPresent ? L"  success: privilege already present\n"
		: L"  success: privilege added\n");
	if (token)
		CloseHandle(token);
	return TRUE;
}

static NTSTATUS DOKAN_CALLBACK
EncFSFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"FlushFileBuffers : %s\n", filePath);

	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_SUCCESS;
	}

	EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
	if (encfsFile->flush()) {
		return STATUS_SUCCESS;
	}
	else {
		DWORD error = GetLastError();
		DbgPrint(L"\tflush error code = %d\n", error);
		return DokanNtStatusFromWin32(error);
	}
}


static NTSTATUS DOKAN_CALLBACK EncFSSetEndOfFile(
	LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetEndOfFile %s, %I64d\n", filePath, ByteOffset);

	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
	if (!encfsFile->setLength(FileName, ByteOffset)) {
		DWORD error = GetLastError();
		DbgPrint(L"\tSetFilePointer error: %d, offset = %I64d\n\n", error,
			ByteOffset);
		return DokanNtStatusFromWin32(error);
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSGetFileInformation(
	LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"GetFileInfo : %s\n", filePath);

	EncFS::EncFSFile* encfsFile;
	BOOL opened = FALSE;
	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle, cleanuped?\n");
		HANDLE handle = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, 0, NULL);
		if (handle == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			DbgPrint(L"\tCreateFile error : %d\n\n", error);
			return DokanNtStatusFromWin32(error);
		}
		encfsFile = new EncFS::EncFSFile(handle, false);
		opened = TRUE;
	}
	else {
		encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
	}

	if (!GetFileInformationByHandle(encfsFile->getHandle(), HandleFileInformation)) {
		DbgPrint(L"\terror code = %d\n", GetLastError());

		// FileName is a root directory
		// in this case, FindFirstFile can't get directory information
		if (wcslen(FileName) == 1) {
			DbgPrint(L"  root dir\n");
			HandleFileInformation->dwFileAttributes = GetFileAttributesW(filePath);

		}
		else {
			WIN32_FIND_DATAW find;
			ZeroMemory(&find, sizeof(WIN32_FIND_DATAW));
			HANDLE findHandle = FindFirstFileW(filePath, &find);
			if (findHandle == INVALID_HANDLE_VALUE) {
				DWORD error = GetLastError();
				DbgPrint(L"\tFindFirstFile error code = %d\n\n", error);
				if (opened) {
					delete encfsFile;
				}
				return DokanNtStatusFromWin32(error);
			}
			HandleFileInformation->dwFileAttributes = find.dwFileAttributes;
			HandleFileInformation->ftCreationTime = find.ftCreationTime;
			HandleFileInformation->ftLastAccessTime = find.ftLastAccessTime;
			HandleFileInformation->ftLastWriteTime = find.ftLastWriteTime;
			HandleFileInformation->nFileSizeHigh = find.nFileSizeHigh;
			HandleFileInformation->nFileSizeLow = find.nFileSizeLow;
			DbgPrint(L"\tFindFiles OK, file size = %d\n", find.nFileSizeLow);
			FindClose(findHandle);
		}
	}
	else {
		DbgPrint(L"\tGetFileInformationByHandle success, file size = %d\n",
			HandleFileInformation->nFileSizeLow);
	}

	// Caluclate file size
	int64_t size = (HandleFileInformation->nFileSizeHigh * ((int64_t)MAXDWORD + 1)) + HandleFileInformation->nFileSizeLow;
	size = encfs.isReverse() ? encfs.toEncodedLength(size) : encfs.toDecodedLength(size);
	HandleFileInformation->nFileSizeLow = size & MAXDWORD;
	HandleFileInformation->nFileSizeHigh = (size >> 32) & MAXDWORD;
	DbgPrint(L"\tVirtualFileSize = %d\n",
		HandleFileInformation->nFileSizeLow);

	DbgPrint(L"FILE ATTRIBUTE  = %d\n", HandleFileInformation->dwFileAttributes);

	if (opened) {
		delete encfsFile;
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
EncFSDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);
	DbgPrint(L"DeleteFile %s - %d\n", filePath, DokanFileInfo->DeleteOnClose);

	DWORD dwAttrib = GetFileAttributesW(filePath);

	if (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
		return STATUS_ACCESS_DENIED;

	if (DokanFileInfo->Context) {
		FILE_DISPOSITION_INFO fdi;
		fdi.DeleteFile = DokanFileInfo->DeleteOnClose;
		EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
		if (!SetFileInformationByHandle(encfsFile->getHandle(), FileDispositionInfo, &fdi,
			sizeof(FILE_DISPOSITION_INFO)))
			return DokanNtStatusFromWin32(GetLastError());
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSSetAllocationSize(
	LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];
	LARGE_INTEGER fileSize;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetAllocationSize %s, %I64d\n", filePath, AllocSize);
	//printf("AllocSize %d\n", AllocSize);

	int64_t encodedLength = encfs.toEncodedLength(AllocSize);
	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;
	if (GetFileSizeEx(encfsFile->getHandle(), &fileSize)) {
		if (encodedLength < fileSize.QuadPart) {
			if (!encfsFile->setLength(FileName, AllocSize)) {
				DWORD error = GetLastError();
				DbgPrint(L"\tSetFilePointer error: %d, offset = %I64d\n\n", error,
					AllocSize);
				return DokanNtStatusFromWin32(error);
			}
		}
	}
	else {
		DWORD error = GetLastError();
		DbgPrint(L"\terror code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSSetFileAttributes(
	LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	WCHAR filePath[DOKAN_MAX_PATH];

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetFileAttributes %s 0x%x\n", filePath, FileAttributes);

	if (FileAttributes != 0) {
		if (!SetFileAttributesW(filePath, FileAttributes)) {
			DWORD error = GetLastError();
			DbgPrint(L"\terror code = %d\n\n", error);
			return DokanNtStatusFromWin32(error);
		}
	}
	else {
		// case FileAttributes == 0 :
		// MS-FSCC 2.6 File Attributes : There is no file attribute with the value 0x00000000
		// because a value of 0x00000000 in the FileAttributes field means that the file attributes for this file MUST NOT be changed when setting basic information for the file
		DbgPrint(L"Set 0 to FileAttributes means MUST NOT be changed. Didn't call "
			L"SetFileAttributes function. \n");
	}

	DbgPrint(L"\n");
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
EncFSSetFileTime(LPCWSTR FileName, CONST FILETIME *CreationTime,
	CONST FILETIME *LastAccessTime, CONST FILETIME *LastWriteTime,
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetFileTime %s\n", filePath);

	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}
	EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;

	if (!SetFileTime(encfsFile->getHandle(), CreationTime, LastAccessTime, LastWriteTime)) {
		DWORD error = GetLastError();
		DbgPrint(L"\terror code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\n");
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK
EncFSUnlockFile(LPCWSTR FileName, LONGLONG ByteOffset, LONGLONG Length,
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];
	LARGE_INTEGER length;
	LARGE_INTEGER offset;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"UnlockFile %s\n", filePath);

	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}

	EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;

	length.QuadPart = Length;
	offset.QuadPart = ByteOffset;

	if (!UnlockFile(encfsFile->getHandle(), offset.LowPart, offset.HighPart, length.LowPart,
		length.HighPart)) {
		DWORD error = GetLastError();
		DbgPrint(L"\terror code = %d\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tsuccess\n\n");
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSGetFileSecurity(
	LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
	PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
	PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];
	BOOLEAN requestingSaclInfo;

	UNREFERENCED_PARAMETER(DokanFileInfo);

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"GetFileSecurity %s\n", filePath);

	EncFSCheckFlag(*SecurityInformation, FILE_SHARE_READ);
	EncFSCheckFlag(*SecurityInformation, OWNER_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, GROUP_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, DACL_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, SACL_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, LABEL_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, ATTRIBUTE_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, SCOPE_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation,
		PROCESS_TRUST_LABEL_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, BACKUP_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, PROTECTED_DACL_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, PROTECTED_SACL_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, UNPROTECTED_DACL_SECURITY_INFORMATION);
	EncFSCheckFlag(*SecurityInformation, UNPROTECTED_SACL_SECURITY_INFORMATION);

	requestingSaclInfo = ((*SecurityInformation & SACL_SECURITY_INFORMATION) ||
		(*SecurityInformation & BACKUP_SECURITY_INFORMATION));

	if (!g_efo.g_HasSeSecurityPrivilege) {
		*SecurityInformation &= ~SACL_SECURITY_INFORMATION;
		*SecurityInformation &= ~BACKUP_SECURITY_INFORMATION;
	}

	DbgPrint(L"  Opening new handle with READ_CONTROL access\n");
	HANDLE handle = CreateFileW(
		filePath,
		READ_CONTROL | ((requestingSaclInfo && g_efo.g_HasSeSecurityPrivilege)
			? ACCESS_SYSTEM_SECURITY
			: 0),
		FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL, // security attribute
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, // |FILE_FLAG_NO_BUFFERING,
		NULL);

	if (!handle) {
		DbgPrint(L"\tinvalid handle\n\n");
		int error = GetLastError();
		return DokanNtStatusFromWin32(error);
	}

	if (!GetUserObjectSecurity(handle, SecurityInformation, SecurityDescriptor,
		BufferLength, LengthNeeded)) {
		int error = GetLastError();
		if (error == ERROR_INSUFFICIENT_BUFFER) {
			DbgPrint(L"  GetUserObjectSecurity error: ERROR_INSUFFICIENT_BUFFER\n");
			CloseHandle(handle);
			return STATUS_BUFFER_OVERFLOW;
		}
		else {
			DbgPrint(L"  GetUserObjectSecurity error: %d\n", error);
			CloseHandle(handle);
			return DokanNtStatusFromWin32(error);
		}
	}

	// Ensure the Security Descriptor Length is set
	DWORD securityDescriptorLength =
		GetSecurityDescriptorLength(SecurityDescriptor);
	DbgPrint(L"  GetUserObjectSecurity return true,  *LengthNeeded = "
		L"securityDescriptorLength \n");
	*LengthNeeded = securityDescriptorLength;

	CloseHandle(handle);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSSetFileSecurity(
	LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
	PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG SecurityDescriptorLength,
	PDOKAN_FILE_INFO DokanFileInfo) {
	WCHAR filePath[DOKAN_MAX_PATH];

	UNREFERENCED_PARAMETER(SecurityDescriptorLength);

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"SetFileSecurity %s\n", filePath);

	if (!DokanFileInfo->Context) {
		DbgPrint(L"\tinvalid handle\n\n");
		return STATUS_INVALID_HANDLE;
	}
	EncFS::EncFSFile* encfsFile = (EncFS::EncFSFile*)DokanFileInfo->Context;

	if (!SetUserObjectSecurity(encfsFile->getHandle(), SecurityInformation, SecurityDescriptor)) {
		int error = GetLastError();
		if (error == ERROR_INSUFFICIENT_BUFFER) {
			DbgPrint(L"  GetUserObjectSecurity error: ERROR_INSUFFICIENT_BUFFER\n");
			return STATUS_BUFFER_OVERFLOW;
		}
		else {
			DbgPrint(L"  SetUserObjectSecurity error: %d\n", error);
			return DokanNtStatusFromWin32(error);
		}
	}
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSGetVolumeInformation(
	LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
	LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
	LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
	PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	WCHAR volumeRoot[4];
	DWORD fsFlags = 0;

	wcscpy_s(VolumeNameBuffer, VolumeNameSize, L"EncFS");

	if (VolumeSerialNumber)
		*VolumeSerialNumber = 0x19831116;
	if (MaximumComponentLength)
		*MaximumComponentLength = 255;
	if (FileSystemFlags)
		*FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES |
		FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK |
		FILE_PERSISTENT_ACLS | FILE_NAMED_STREAMS;

	volumeRoot[0] = g_efo.RootDirectory[0];
	volumeRoot[1] = ':';
	volumeRoot[2] = '\\';
	volumeRoot[3] = '\0';

	if (GetVolumeInformationW(volumeRoot, NULL, 0, NULL, MaximumComponentLength,
		&fsFlags, FileSystemNameBuffer,
		FileSystemNameSize)) {

		if (FileSystemFlags)
			*FileSystemFlags &= fsFlags;

		if (MaximumComponentLength) {
			DbgPrint(L"GetVolumeInformation: max component length %u\n",
				*MaximumComponentLength);
		}
		if (FileSystemNameBuffer) {
			DbgPrint(L"GetVolumeInformation: file system name %s\n",
				FileSystemNameBuffer);
		}
		if (FileSystemFlags) {
			DbgPrint(L"GetVolumeInformation: got file system flags 0x%08x,"
				L" returning 0x%08x\n",
				fsFlags, *FileSystemFlags);
		}
	}
	else {

		DbgPrint(L"GetVolumeInformation: unable to query underlying fs,"
			L" using defaults.  Last error = %u\n",
			GetLastError());

		// File system name could be anything up to 10 characters.
		// But Windows check few feature availability based on file system name.
		// For this, it is recommended to set NTFS or FAT here.
		wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, L"NTFS");
	}

	return STATUS_SUCCESS;
}

//Uncomment for personalize disk space
static NTSTATUS DOKAN_CALLBACK EncFSDokanGetDiskFreeSpace(
	PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes,
	PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	WCHAR volumeRoot[4];
	volumeRoot[0] = g_efo.RootDirectory[0];
	volumeRoot[1] = ':';
	volumeRoot[2] = '\\';
	volumeRoot[3] = '\0';

	ULARGE_INTEGER lpFreeBytesAvailable;
	ULARGE_INTEGER lpTotalNumberOfBytes;
	ULARGE_INTEGER lpTotalNumberOfFreeBytes;
	GetDiskFreeSpaceExW(
		volumeRoot,
		&lpFreeBytesAvailable,
		&lpTotalNumberOfBytes,
		&lpTotalNumberOfFreeBytes);
	*FreeBytesAvailable = (ULONGLONG)lpFreeBytesAvailable.QuadPart;
	*TotalNumberOfBytes = (ULONGLONG)lpTotalNumberOfBytes.QuadPart;
	*TotalNumberOfFreeBytes = (ULONGLONG)lpTotalNumberOfFreeBytes.QuadPart;

	return STATUS_SUCCESS;
}

/**
* Avoid #include <winternl.h> which as conflict with FILE_INFORMATION_CLASS
* definition.
* This only for EncFSFindStreams. Link with ntdll.lib still required.
*
* Not needed if you're not using NtQueryInformationFile!
*
* BEGIN
*/
#pragma warning(push)
#pragma warning(disable : 4201)
typedef struct _IO_STATUS_BLOCK {
	union {
		NTSTATUS Status;
		PVOID Pointer;
	} DUMMYUNIONNAME;

	ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
#pragma warning(pop)

NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationFile(
	_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock,
	_Out_writes_bytes_(Length) PVOID FileInformation, _In_ ULONG Length,
	_In_ FILE_INFORMATION_CLASS FileInformationClass);
/**
* END
*/

NTSTATUS DOKAN_CALLBACK
EncFSFindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
	PVOID FindStreamContext,
	PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE hFind;
	WIN32_FIND_STREAM_DATA findData;
	DWORD error;
	int count = 0;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"FindStreams :%s\n", filePath);

	hFind = FindFirstStreamW(filePath, FindStreamInfoStandard, &findData, 0);

	if (hFind == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		DbgPrint(L"\tinvalid file handle. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	BOOL bufferFull = FillFindStreamData(&findData, FindStreamContext);
	if (bufferFull) {
		count++;
		while (FindNextStreamW(hFind, &findData) != 0) {
			bufferFull = FillFindStreamData(&findData, FindStreamContext);
			if (!bufferFull)
				break;
			count++;
		}
	}

	error = GetLastError();
	FindClose(hFind);

	if (!bufferFull) {
		DbgPrint(L"\tFindStreams returned %d entries in %s with "
			L"STATUS_BUFFER_OVERFLOW\n\n",
			count, filePath);
		// https://msdn.microsoft.com/en-us/library/windows/hardware/ff540364(v=vs.85).aspx
		return STATUS_BUFFER_OVERFLOW;
	}

	if (error != ERROR_HANDLE_EOF) {
		DbgPrint(L"\tFindNextStreamW error. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tFindStreams return %d entries in %s\n\n", count, filePath);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSMounted(LPCWSTR MountPoint,
	PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	DbgPrint(L"Mounted as %s\n", MountPoint);
	ShellExecute(NULL, L"open", MountPoint, NULL, NULL, SW_SHOWDEFAULT);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK EncFSUnmounted(PDOKAN_FILE_INFO DokanFileInfo) {
	UNREFERENCED_PARAMETER(DokanFileInfo);

	DbgPrint(L"Unmounted\n");
	return STATUS_SUCCESS;
}

#pragma warning(pop)

BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		SetConsoleCtrlHandler(CtrlHandler, FALSE);
		DokanRemoveMountPoint(g_efo.MountPoint);
		return TRUE;
	default:
		return FALSE;
	}
}

#define CONFIG_XML "\\.encfs6.xml"
bool IsEncFSExists(LPCWSTR rootDir) {
	const wstring wRootDir(rootDir);
	wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
	string cRootDir = strConv.to_bytes(wRootDir);
	string configFile = cRootDir + CONFIG_XML;

	ifstream in(configFile);
	return in.is_open();
}

int CreateEncFS(LPCWSTR rootDir, char *password, EncFSMode mode, bool reverse) {
	const wstring wRootDir(rootDir);
	wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
	string cRootDir = strConv.to_bytes(wRootDir);
	string configFile = cRootDir + CONFIG_XML;

	ifstream in(configFile);
	if (in.is_open()) {
		return EXIT_FAILURE;
	}

	encfs.create(password, (EncFS::EncFSMode)mode, reverse);
	string xml;
	encfs.save(xml);
	ofstream out(configFile);
	out << xml;
	out.close();
	return EXIT_SUCCESS;
}

int StartEncFS(EncFSOptions &efo, char *password) {
	DOKAN_OPERATIONS dokanOperations;
	DOKAN_OPTIONS dokanOptions;

	string configFile;
	wstring_convert<codecvt_utf8_utf16<wchar_t>> strConv;
	if (false && efo.ConfigFile) {
		const wstring wConfigFile(efo.ConfigFile);
		configFile = strConv.to_bytes(wConfigFile);
	}
	else {
		const wstring wRootDir(efo.RootDirectory);
		string cRootDir = strConv.to_bytes(wRootDir);
		configFile = cRootDir + CONFIG_XML;
	}

	try {
		ifstream in(configFile);
		if (in.is_open()) {
			string xml((istreambuf_iterator<char>(in)),
				istreambuf_iterator<char>());
			in.close();
			encfs.load(xml, efo.Reverse);
		}
		else {
			return EXIT_FAILURE;
		}
	}
	catch (const EncFS::EncFSBadConfigurationException &ex) {
		printf("%s\n", ex.what());
		return EXIT_FAILURE;
	}

	ZeroMemory(&dokanOptions, sizeof(DOKAN_OPTIONS));
	dokanOptions.Version = DOKAN_VERSION;
	dokanOptions.Timeout = efo.Timeout;
	dokanOptions.MountPoint = efo.MountPoint;
	dokanOptions.SingleThread = efo.SingleThread;
	dokanOptions.Options = efo.DokanOptions;
	dokanOptions.AllocationUnitSize = efo.AllocationUnitSize;
	dokanOptions.SectorSize = efo.SectorSize;

	if (efo.UNCName && wcscmp(efo.UNCName, L"") != 0 &&
		!(dokanOptions.Options & DOKAN_OPTION_NETWORK)) {
		fwprintf(
			stderr,
			L"  Warning: UNC provider name should be set on network drive only.\n");
		dokanOptions.UNCName = efo.UNCName;
	}
	else {
		dokanOptions.UNCName = L"";
	}

	if (dokanOptions.Options & DOKAN_OPTION_NETWORK &&
		dokanOptions.Options & DOKAN_OPTION_MOUNT_MANAGER) {
		fwprintf(stderr, L"Mount manager cannot be used on network drive.\n");
		return EXIT_FAILURE;
	}

	if (!(dokanOptions.Options & DOKAN_OPTION_MOUNT_MANAGER) &&
		wcscmp(efo.MountPoint, L"") == 0) {
		fwprintf(stderr, L"Mount Point required.\n");
		return EXIT_FAILURE;
	}

	if ((dokanOptions.Options & DOKAN_OPTION_MOUNT_MANAGER) &&
		(dokanOptions.Options & DOKAN_OPTION_CURRENT_SESSION)) {
		fwprintf(stderr,
			L"Mount Manager always mount the drive for all user sessions.\n");
		return EXIT_FAILURE;
	}

	if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
		fwprintf(stderr, L"Control Handler is not set.\n");
	}

	// Add security name privilege. Required here to handle GetFileSecurity
	// properly.
	/*
	g_HasSeSecurityPrivilege = AddSeSecurityNamePrivilege();
	if (!g_HasSeSecurityPrivilege) {
	fwprintf(stderr, L"Failed to add security privilege to process\n");
	fwprintf(stderr,
	L"\t=> GetFileSecurity/SetFileSecurity may not work properly\n");
	fwprintf(stderr, L"\t=> Please restart EncFS sample with administrator "
	L"rights to fix it\n");
	}
	*/

	if (efo.g_ImpersonateCallerUser && !efo.g_HasSeSecurityPrivilege) {
		fwprintf(stderr, L"Impersonate Caller User requires administrator right to "
			L"work properly\n");
		fwprintf(stderr, L"\t=> Other users may not use the drive properly\n");
		fwprintf(stderr, L"\t=> Please restart EncFS sample with administrator "
			L"rights to fix it\n");
	}

	if (efo.g_DebugMode) {
		dokanOptions.Options |= DOKAN_OPTION_DEBUG;
	}
	if (efo.g_UseStdErr) {
		dokanOptions.Options |= DOKAN_OPTION_STDERR;
	}
	if (efo.Reverse) {
		dokanOptions.Options |= DOKAN_OPTION_WRITE_PROTECT;
	}

	dokanOptions.Options |= DOKAN_OPTION_ALT_STREAM;

	ZeroMemory(&dokanOperations, sizeof(DOKAN_OPERATIONS));
	dokanOperations.ZwCreateFile = EncFSCreateFile;
	dokanOperations.Cleanup = EncFSCleanup;
	dokanOperations.CloseFile = EncFSCloseFile;
	dokanOperations.ReadFile = EncFSReadFile;
	dokanOperations.WriteFile = EncFSWriteFile;
	dokanOperations.FlushFileBuffers = EncFSFlushFileBuffers;
	dokanOperations.GetFileInformation = EncFSGetFileInformation;
	dokanOperations.FindFiles = EncFSFindFiles;
	dokanOperations.FindFilesWithPattern = NULL;
	dokanOperations.SetFileAttributes = EncFSSetFileAttributes;
	dokanOperations.SetFileTime = EncFSSetFileTime;
	dokanOperations.DeleteFile = EncFSDeleteFile;
	dokanOperations.DeleteDirectory = EncFSDeleteDirectory;
	dokanOperations.MoveFile = EncFSMoveFile;
	dokanOperations.SetEndOfFile = EncFSSetEndOfFile;
	dokanOperations.SetAllocationSize = EncFSSetAllocationSize;
	dokanOperations.LockFile = EncFSLockFile;
	dokanOperations.UnlockFile = EncFSUnlockFile;
	dokanOperations.GetFileSecurity = EncFSGetFileSecurity;
	dokanOperations.SetFileSecurity = EncFSSetFileSecurity;
	dokanOperations.GetDiskFreeSpace = EncFSDokanGetDiskFreeSpace;
	dokanOperations.GetVolumeInformation = EncFSGetVolumeInformation;
	dokanOperations.Unmounted = EncFSUnmounted;
	dokanOperations.FindStreams = EncFSFindStreams;
	dokanOperations.Mounted = EncFSMounted;

	// EncFS
	try {
		encfs.unlock(password);
	}
	catch (const EncFS::EncFSUnlockFailedException &ex) {
		printf("%s\n", ex.what());
		return EXIT_FAILURE;
	}

	g_efo = efo;
	DokanInit();
	int status = DokanMain(&dokanOptions, &dokanOperations);
	DokanShutdown();
	switch (status) {
	case DOKAN_SUCCESS:
		fprintf(stderr, "Success\n");
		break;
	case DOKAN_ERROR:
		fprintf(stderr, "Error\n");
		break;
	case DOKAN_DRIVE_LETTER_ERROR:
		fprintf(stderr, "Bad Drive letter\n");
		break;
	case DOKAN_DRIVER_INSTALL_ERROR:
		fprintf(stderr, "Can't install driver\n");
		break;
	case DOKAN_START_ERROR:
		fprintf(stderr, "Driver something wrong\n");
		break;
	case DOKAN_MOUNT_ERROR:
		fprintf(stderr, "Can't assign a drive letter\n");
		break;
	case DOKAN_MOUNT_POINT_ERROR:
		fprintf(stderr, "Mount point error\n");
		break;
	case DOKAN_VERSION_ERROR:
		fprintf(stderr, "Version error\n");
		break;
	default:
		fprintf(stderr, "Unknown error: %d\n", status);
		break;
	}
	return EXIT_SUCCESS;
}
