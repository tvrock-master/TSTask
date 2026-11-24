#include "PluginTestData.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <iterator>

#define CreateBonDriver ImportedCreateBonDriver
#include "../BonDriver_TSTask/IBonDriver2.h"
#undef CreateBonDriver

namespace
{
	using namespace PluginTest;
	using Stream = std::array<BYTE, PacketSize * 3 * 16>;

	void WriteSection(BYTE *packet, WORD pid, BYTE counter, std::initializer_list<BYTE> section)
	{
		packet[0] = 0x47;
		packet[1] = BYTE(0x40 | (pid >> 8));
		packet[2] = BYTE(pid);
		packet[3] = BYTE(0x10 | counter);
		packet[4] = 0;
		auto end = std::copy(section.begin(), section.end(), packet + 5);

		DWORD crc = 0xffffffff;
		for (BYTE value : section) {
			crc ^= DWORD(value) << 24;
			for (int bit = 0; bit < 8; ++bit)
				crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
		}
		for (int shift = 24; shift >= 0; shift -= 8)
			*end++ = BYTE(crc >> shift);
	}

	Stream MakeStream(WORD tsid, BYTE sid)
	{
		Stream stream;
		stream.fill(0xff);
		for (BYTE counter = 0; counter < 16; ++counter) {
			BYTE *packet = stream.data() + counter * 3 * PacketSize;
			WriteSection(packet, 0, counter,
				{0, 0xb0, 13, BYTE(tsid >> 8), BYTE(tsid), 0xc1, 0, 0, 0, sid, 0xe1, 0});
			WriteSection(packet + PacketSize, 0x100, counter,
				{2, 0xb0, 18, 0, sid, 0xc1, 0, 0, 0xe1, 1, 0xf0, 0, 2, 0xe1, 1, 0xf0, 0});

			// The video PID carries a channel marker so the test can identify the recorded TS.
			const BYTE video[] = {0x47, BYTE(VideoPID >> 8), BYTE(VideoPID), BYTE(0x10 | counter), sid};
			std::copy(std::begin(video), std::end(video), packet + 2 * PacketSize);
		}
		return stream;
	}

	class MockBonDriver final : public IBonDriver2
	{
	public:
		const BOOL OpenTuner() override { return TRUE; }
		void CloseTuner() override { m_Channel = MAXDWORD; }
		const BOOL SetChannel(BYTE channel) override { return SetChannel(0, channel); }
		const float GetSignalLevel() override { return 30.0f; }
		const DWORD WaitTsStream(DWORD) override { return WAIT_OBJECT_0; }
		const DWORD GetReadyCount() override { return m_Channel < m_Streams.size() ? 1 : 0; }
		const BOOL GetTsStream(BYTE *data, DWORD *size, DWORD *remain) override
		{
			BYTE *source;
			if (!GetTsStream(&source, size, remain))
				return FALSE;
			std::memcpy(data, source, *size);
			return TRUE;
		}
		const BOOL GetTsStream(BYTE **data, DWORD *size, DWORD *remain) override
		{
			const DWORD channel = m_Channel;
			if (channel >= m_Streams.size())
				return FALSE;
			*data = m_Streams[channel].data();
			*size = DWORD(m_Streams[channel].size());
			*remain = 0;
			return TRUE;
		}
		void PurgeTsStream() override {}
		void Release() override { delete this; }
		LPCTSTR GetTunerName() override { return L"CI tuner"; }
		const BOOL IsTunerOpening() override { return TRUE; }
		LPCTSTR EnumTuningSpace(DWORD space) override { return space == 0 ? L"Terrestrial" : nullptr; }
		LPCTSTR EnumChannelName(DWORD space, DWORD channel) override
		{
			const LPCWSTR names[] = {L"First", L"Second"};
			return space == 0 && channel < std::size(names) ? names[channel] : nullptr;
		}
		const BOOL SetChannel(DWORD space, DWORD channel) override
		{
			if (space != 0 || channel >= m_Streams.size())
				return FALSE;
			m_Channel = channel;
			return TRUE;
		}
		const DWORD GetCurSpace() override { return 0; }
		const DWORD GetCurChannel() override { return m_Channel; }

	private:
		std::atomic<DWORD> m_Channel{MAXDWORD};
		std::array<Stream, 2> m_Streams{
			MakeStream(FirstTransportStream, BYTE(FirstService)),
			MakeStream(SecondTransportStream, BYTE(SecondService))
		};
	};
}

extern "C" IBonDriver *CreateBonDriver() { return new MockBonDriver; }
