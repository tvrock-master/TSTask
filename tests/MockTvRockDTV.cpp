#include "PluginTestData.h"
#include "TvRockDTV.h"
#include <atomic>
#include <cstring>
#include <thread>

namespace
{
	using namespace PluginTest;
	using namespace TvRock;

	void WriteResult(LPCWSTR path, const char *message)
	{
		HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			DWORD written;
			WriteFile(file, message, DWORD(std::strlen(message)), &written, nullptr);
			CloseHandle(file);
		}
	}

	class MockTvRockDTV final : public TvRockDTV
	{
	public:
		bool Initialize(DWORD device, RockRequest *request) override
		{
			if (device != 0 || !request)
				return false;
			if (GetEnvironmentVariableW(L"TSTASK_TEST_FAIL_INIT", nullptr, 0)) {
				WriteResult(L"tvrock-init-refused.txt", "OK");
				return false;
			}
			m_Worker = std::thread([this, request] { Run(*request); });
			return true;
		}
		void Dispose() override
		{
			m_Stopping = true;
			if (m_Worker.joinable())
				m_Worker.join();
			WriteResult(L"tvrock-disposed.txt", "OK");
		}
		bool ProcessTS(BYTE *data, DWORD size) override
		{
			for (DWORD offset = 0; offset + PacketSize <= size; offset += PacketSize) {
				const BYTE *packet = data + offset;
				const WORD pid = WORD(((packet[1] & 0x1f) << 8) | packet[2]);
				if (packet[0] == 0x47 && pid == VideoPID) {
					if (packet[4] == FirstService)
						m_ReceivedFirst = true;
					if (packet[4] == SecondService)
						m_ReceivedSecond = true;
				}
			}
			return true;
		}
		bool OnStreamChange(DWORD, DWORD service) override
		{
			m_Service = service;
			return true;
		}
		void GetBroadCastTime(SYSTEMTIME *) override {}
		RockProperty *GetProperty() override { return nullptr; }
		RockStatus *GetStatus() override { return nullptr; }
		void Verbose(BOOL) override {}
		void Trace(LPCWSTR, ...) override {}
		void Trace(LPCSTR, ...) override {}

	private:
		template<typename Predicate>
		bool WaitFor(Predicate ready) const
		{
			const ULONGLONG deadline = GetTickCount64() + 8000;
			while (!m_Stopping && GetTickCount64() < deadline) {
				if (ready())
					return true;
				Sleep(25);
			}
			return false;
		}

		const char *Exercise(RockRequest &request)
		{
			if (!WaitFor([this] { return m_ReceivedFirst && m_Service == FirstService; }))
				return "First service was not delivered to TvRockDTV";
			// TvRock's terrestrial channel command selects the service from the driver's .ch2.
			if (!request.ChannelChange(1, SecondService))
				return "ChannelChange failed";
			if (!WaitFor([this] { return m_ReceivedSecond && m_Service == SecondService; }))
				return "Second service was not delivered to TvRockDTV";
			WCHAR recording[] = L"primary.ts";
			if (!request.RecordStart(recording))
				return "RecordStart failed";
			if (!WaitFor([] { return HasData(L"backup\\primary.part2.ts"); }))
				return "Relay file did not receive TS data";
			if (!request.RecordStop())
				return "RecordStop failed";
			return "OK";
		}

		void Run(RockRequest &request)
		{
			WriteResult(L"tvrock-result.txt", Exercise(request));
			if (!m_Stopping)
				request.PowerOff();
		}

		std::atomic<bool> m_Stopping{false};
		std::atomic<bool> m_ReceivedFirst{false}, m_ReceivedSecond{false};
		std::atomic<DWORD> m_Service{0};
		std::thread m_Worker;
	};
}

extern "C" TvRock::TvRockDTV *CreateTvRockDTV(HINSTANCE) { return new MockTvRockDTV; }
