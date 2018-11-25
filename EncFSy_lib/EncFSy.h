#pragma once
#include <dokan.h>

#define DOKAN_MAX_PATH 32768

struct EncFSOptions {
	BOOL g_UseStdErr;
	BOOL g_DebugMode;
	BOOL g_HasSeSecurityPrivilege;
	BOOL g_ImpersonateCallerUser;
	USHORT ThreadCount;
	ULONG Timeout;
	ULONG AllocationUnitSize;
	ULONG DokanOptions;
	ULONG SectorSize;

	WCHAR RootDirectory[DOKAN_MAX_PATH];
	WCHAR MountPoint[DOKAN_MAX_PATH];
	WCHAR UNCName[DOKAN_MAX_PATH];
};

bool IsEncFSExists(LPCWSTR rootDir);

int CreateEncFS(LPCWSTR rootDir, char *password, bool paranoia);

int StartEncFS(EncFSOptions &options, char *password);
