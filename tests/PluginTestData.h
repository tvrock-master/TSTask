#pragma once

#include <windows.h>

namespace PluginTest
{
	constexpr unsigned PacketSize = 188;
	constexpr WORD FirstService = 41;
	constexpr WORD SecondService = 42;
	constexpr WORD FirstTransportStream = 0x4010;
	constexpr WORD SecondTransportStream = 0x4011;
	constexpr WORD VideoPID = 0x101;

	inline bool HasData(LPCWSTR path)
	{
		WIN32_FILE_ATTRIBUTE_DATA info{};
		return GetFileAttributesExW(path, GetFileExInfoStandard, &info)
			&& (info.nFileSizeHigh != 0 || info.nFileSizeLow != 0);
	}
}
