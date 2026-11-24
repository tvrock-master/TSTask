#include "PluginTestData.h"
#include <cwchar>

BOOL WINAPI TestGetDiskFreeSpaceEx(LPCWSTR directory, PULARGE_INTEGER available, PULARGE_INTEGER, PULARGE_INTEGER)
{
	WCHAR backup[MAX_PATH]{};
	const DWORD length = GetFullPathNameW(L"backup\\", _countof(backup), backup, nullptr);
	if (length == 0 || length >= _countof(backup))
		return FALSE;

	// Wait for data in the first file, then simulate a low-space disk and a usable backup.
	const bool enoughSpace = _wcsicmp(directory, backup) == 0 || !PluginTest::HasData(L"primary.ts");
	available->QuadPart = enoughSpace ? (1024ULL << 20) : 0;
	return TRUE;
}

// Compile the real SDK sample with a deterministic free-space query.
#undef GetDiskFreeSpaceEx
#define GetDiskFreeSpaceEx TestGetDiskFreeSpaceEx
#include "DiskRelay.cpp"
