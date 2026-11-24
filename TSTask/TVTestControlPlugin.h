#pragma once

#include <memory>

namespace TSTask
{
	class CTSTaskAppCore;

	class CTVTestControlPlugin
	{
	public:
		static bool IsTvRockPlugin(LPCWSTR Name);
		static bool IsDiskRelayPlugin(LPCWSTR Name);

		explicit CTVTestControlPlugin(CTSTaskAppCore &App);
		~CTVTestControlPlugin();
		bool Load(LPCWSTR Name);
		void Finalize();
		bool IsTvRockLoaded() const { return m_TvRock != nullptr; }
		void DispatchEvent(UINT Event, LPARAM Value);
		static const UINT WindowMessage = WM_APP + 2;

	private:
		class Session;
		CTSTaskAppCore &m_App;
		std::unique_ptr<Session> m_TvRock;
		std::unique_ptr<Session> m_DiskRelay;
	};
}
