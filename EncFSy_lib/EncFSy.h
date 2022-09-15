#pragma once
#include <dokan.h>

#define DOKAN_MAX_PATH 32768

enum EncFSMode {
	STANDARD = 1,
	PARANOIA = 2
};

struct EncFSOptions {
	BOOLEAN g_UseStdErr;
	BOOLEAN g_DebugMode;
	BOOLEAN g_HasSeSecurityPrivilege;
	BOOLEAN g_ImpersonateCallerUser;

	BOOLEAN SingleThread;
	ULONG Timeout;
	ULONG AllocationUnitSize;
	ULONG DokanOptions;
	ULONG SectorSize;

	WCHAR RootDirectory[DOKAN_MAX_PATH];

	WCHAR MountPoint[DOKAN_MAX_PATH];
	WCHAR UNCName[DOKAN_MAX_PATH];

	BOOLEAN Reverse;
	PWCHAR ConfigFile;
};

bool IsEncFSExists(LPCWSTR rootDir);

int CreateEncFS(LPCWSTR rootDir, char *password, EncFSMode mode, bool paranoia);

int StartEncFS(EncFSOptions &options, char *password);
