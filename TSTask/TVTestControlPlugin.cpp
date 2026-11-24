#include "stdafx.h"
#include "TSTask.h"
#include "TSTaskAppCore.h"
#include "TVTestControlPlugin.h"
#include "TVTestSDK/TVTestPlugin.h"

namespace TSTask
{
namespace
{
	using namespace TVTest;

	bool SupportsMessage(MessageCode Message, bool DiskRelay)
	{
		switch (Message) {
		case MESSAGE_GETVERSION:
		case MESSAGE_QUERYMESSAGE:
		case MESSAGE_GETHOSTINFO:
		case MESSAGE_SETEVENTCALLBACK:
		case MESSAGE_ADDLOG:
			return true;
		case MESSAGE_ISPLUGINENABLED:
		case MESSAGE_GETRECORDSTATUS:
		case MESSAGE_RELAYRECORD:
			return DiskRelay;
		case MESSAGE_SETSTREAMCALLBACK:
		case MESSAGE_GETCHANNELINFO:
		case MESSAGE_GETCURRENTCHANNELINFO:
		case MESSAGE_GETTUNINGSPACE:
		case MESSAGE_GETDRIVERNAME:
		case MESSAGE_GETSERVICE:
		case MESSAGE_GETSERVICEINFO:
		case MESSAGE_SETCHANNEL:
		case MESSAGE_SETSERVICE:
		case MESSAGE_GETSTATUS:
		case MESSAGE_STARTRECORD:
		case MESSAGE_STOPRECORD:
		case MESSAGE_CLOSE:
			return !DiskRelay;
		default:
			return false;
		}
	}

	bool StoreChannel(TVTest::ChannelInfo *pInfo, TVTest::ChannelInfo &Info)
	{
		if (!pInfo || (pInfo->Size != sizeof(Info) && pInfo->Size != CHANNELINFO_SIZE_V2))
			return false;
		Info.Size = pInfo->Size;
		memcpy(pInfo, &Info, Info.Size);
		return true;
	}

	DWORD GetDiskRelayFolderLength(const String &Path)
	{
		String ini = Path;
		PathUtility::RenameExtension(&ini, L".ini");
		DWORD length = 0;
		for (int i = 0; i < 3; i++) {
			WCHAR key[] = L"Folder0", folder[MAX_PATH];
			key[6] = static_cast<WCHAR>(L'0' + i);
			length = std::max<DWORD>(length, GetPrivateProfileStringW(L"Settings", key, nullptr, folder, _countof(folder), ini.c_str()));
		}
		if (!length)
			OutLog(LOG_WARNING, L"Configure Folder0, Folder1 or Folder2 in [Settings] in %s to enable DiskRelay.", ini.c_str());
		return length;
	}
}

class CTVTestControlPlugin::Session final : public CEventHandler, public CTSTaskCore::ITsGrabber
{
public:
	Session(CTSTaskAppCore &App, bool DiskRelay)
		: m_App(App), m_Core(App.GetCore()), m_DiskRelay(DiskRelay) {}

	~Session()
	{
		Unload();
	}

	bool Unload()
	{
		if (m_GrabberAttached) {
			m_Core.RemoveTsGrabber(this);
			m_GrabberAttached = false;
		}
		if (m_EventsAttached)
			m_EventsAttached = !m_Core.RemoveEventHandler(this);
		if (m_Enabled) {
			DispatchEvent(EVENT_PLUGINENABLE, FALSE);
			m_Enabled = false;
		}
		if (m_Initialized) {
			m_Finalize();
			m_Initialized = false;
		}
		m_EventCallback = nullptr;
		if (m_Module) {
			FreeLibrary(m_Module);
			m_Module = nullptr;
		}
		return !m_EventsAttached;
	}

	bool Load(const String &Path)
	{
		if (m_DiskRelay) {
			m_RelayFolderLength = GetDiskRelayFolderLength(Path);
			if (!m_RelayFolderLength)
				return false;
		}
		m_Module = LoadLibraryExW(Path.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!m_Module) {
			OutSystemErrorLog(GetLastError(), L"Could not load the control plugin (%s).", Path.c_str());
			return false;
		}
		const auto getVersion = reinterpret_cast<GetVersionFunc>(GetProcAddress(m_Module, "TVTGetVersion"));
		const auto getInfo = reinterpret_cast<GetPluginInfoFunc>(GetProcAddress(m_Module, "TVTGetPluginInfo"));
		const auto initialize = reinterpret_cast<InitializeFunc>(GetProcAddress(m_Module, "TVTInitialize"));
		m_Finalize = reinterpret_cast<FinalizeFunc>(GetProcAddress(m_Module, "TVTFinalize"));
		if (!getVersion || !getInfo || !initialize || !m_Finalize)
			return false;
		const DWORD version = getVersion();
		PluginInfo info = {};
		const LPCWSTR expectedName = m_DiskRelay ? L"\u4e88\u5099\u306e\u9332\u753b\u5148" : L"TvRock DTV Target";
		if (version < TVTEST_PLUGIN_VERSION_(0, 0, 10) || version > TVTEST_PLUGIN_VERSION
				|| !getInfo(&info) || !info.pszPluginName
				|| lstrcmpW(info.pszPluginName, expectedName) != 0)
			return false;

		m_Param.Callback = Callback;
		m_Param.hwndApp = m_App.GetWindowHandle();
		m_Param.pInternalData = this;
		m_Initialized = initialize(&m_Param) != FALSE;
		// TvRockOnTVTest can report successful TVTInitialize even if its DLL setup failed.
		if (!m_Initialized || (!m_DiskRelay && !m_Stream.Callback) || !m_EventCallback)
			return false;

		if (m_DiskRelay) {
			m_Enabled = DispatchEvent(EVENT_PLUGINENABLE, TRUE) != FALSE;
			if (!m_Enabled)
				return false;
		} else {
			m_GrabberAttached = m_Core.AddTsGrabber(this);
			if (!m_GrabberAttached)
				return false;
		}
		m_EventsAttached = m_Core.AddEventHandler(this);
		return m_EventsAttached;
	}

	LRESULT DispatchEvent(UINT Event, LPARAM Value)
	{
		return m_EventCallback ? m_EventCallback(Event, Value, 0, m_EventData) : FALSE;
	}

private:
	static LRESULT CALLBACK Callback(PluginParam *pParam, MessageCode Message, LPARAM First, LPARAM Second)
	{
		auto &session = *static_cast<Session*>(pParam->pInternalData);
		return session.HandleMessage(Message, First, Second);
	}

	LRESULT HandleMessage(MessageCode Message, LPARAM First, LPARAM Second)
	{
		if (!SupportsMessage(Message, m_DiskRelay))
			return FALSE;
		switch (Message) {
		case MESSAGE_GETVERSION:
			return MakeVersion(APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_BUILD);
		case MESSAGE_QUERYMESSAGE:
			return SupportsMessage(static_cast<MessageCode>(First), m_DiskRelay);
		case MESSAGE_GETHOSTINFO:
			{
				auto *pInfo = reinterpret_cast<HostInfo*>(First);
				if (!pInfo || pInfo->Size != sizeof(*pInfo))
					return FALSE;
				pInfo->pszAppName = APP_NAME_W;
				pInfo->Version = {APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_BUILD};
				pInfo->pszVersionText = APP_VERSION_TEXT_W;
				pInfo->SupportedPluginVersion = TVTEST_PLUGIN_VERSION_(0, 0, 10);
				return TRUE;
			}
		case MESSAGE_SETEVENTCALLBACK:
			m_EventCallback = reinterpret_cast<EventCallbackFunc>(First);
			m_EventData = reinterpret_cast<void*>(Second);
			return TRUE;
		case MESSAGE_SETSTREAMCALLBACK:
			return SetStreamCallback(reinterpret_cast<const StreamCallbackInfo*>(First));
		case MESSAGE_ISPLUGINENABLED:
			return m_Enabled;
		case MESSAGE_GETRECORDSTATUS:
			return GetRecordStatus(reinterpret_cast<RecordStatusInfo*>(First), static_cast<RecordStatusFlag>(Second));
		case MESSAGE_RELAYRECORD:
			{
				const auto *fileName = reinterpret_cast<LPCWSTR>(First);
				return !IsStringEmpty(fileName) && !m_RecordingFile.empty() && !PathUtility::IsRelative(fileName)
					&& m_Core.ChangeRecordingFile(fileName, m_RecordingFile.c_str(), m_RecordingStartTick);
			}
		case MESSAGE_GETCHANNELINFO:
			return GetChannelInfo(LOWORD(Second), HIWORD(Second), reinterpret_cast<TVTest::ChannelInfo*>(First));
		case MESSAGE_GETCURRENTCHANNELINFO:
			return GetCurrentChannelInfo(reinterpret_cast<TVTest::ChannelInfo*>(First));
		case MESSAGE_GETTUNINGSPACE:
			{
				if (auto *pCount = reinterpret_cast<int*>(First))
					*pCount = m_Core.GetScannedTuningSpaceCount();
				TSTask::ChannelInfo info;
				return m_Core.GetChannel(&info) ? info.Space : -1;
			}
		case MESSAGE_GETDRIVERNAME:
			{
				String path, name;
				m_Core.GetBonDriverFilePath(&path);
				PathUtility::GetFileName(path, &name);
				if (First && Second > 0 && Second <= INT_MAX)
					lstrcpynW(reinterpret_cast<LPWSTR>(First), name.c_str(), static_cast<int>(Second));
				return static_cast<LRESULT>(name.size());
			}
		case MESSAGE_GETSERVICE:
			{
				ServiceList services;
				m_Core.GetServiceList(&services);
				if (auto *pCount = reinterpret_cast<int*>(First))
					*pCount = static_cast<int>(services.size());
				TSTask::ServiceInfo current;
				if (m_Core.GetServiceInfo(&current)) {
					for (size_t i = 0; i < services.size(); i++) {
						if (services[i].ServiceID == current.ServiceID)
							return static_cast<LRESULT>(i);
					}
				}
				return -1;
			}
		case MESSAGE_GETSERVICEINFO:
			return GetServiceInfo(First, reinterpret_cast<TVTest::ServiceInfo*>(Second));
		case MESSAGE_SETCHANNEL:
			return m_Core.SetChannelByScanned(static_cast<int>(First),
				static_cast<SHORT>(LOWORD(Second)), HIWORD(Second));
		case MESSAGE_SETSERVICE:
			{
				if (Second)
					return First > 0 && First < SERVICE_ID_INVALID && m_Core.SetService(static_cast<WORD>(First));
				ServiceList services;
				return First >= 0 && m_Core.GetServiceList(&services)
					&& static_cast<size_t>(First) < services.size()
					&& m_Core.SetService(services[First].ServiceID);
			}
		case MESSAGE_GETSTATUS:
			return GetStatus(reinterpret_cast<StatusInfo*>(First));
		case MESSAGE_STARTRECORD:
			{
				const auto *pInfo = reinterpret_cast<const RecordInfo*>(First);
				if (!pInfo || pInfo->Size != sizeof(*pInfo) || pInfo->Mask != RECORD_MASK_FILENAME) {
					OutLog(LOG_ERROR, L"TvRockOnTVTest sent an unsupported recording request.");
					return FALSE;
				}
				return m_App.StartTvRockRecording(pInfo->pszFileName);
			}
		case MESSAGE_STOPRECORD:
			return m_Core.StopRecording();
		case MESSAGE_CLOSE:
			return m_App.Quit();
		case MESSAGE_ADDLOG:
			if (!First)
				return FALSE;
			OutLog(Second == LOG_TYPE_ERROR ? LOG_ERROR : Second == LOG_TYPE_WARNING ? LOG_WARNING : LOG_INFO,
				L"%s", reinterpret_cast<LPCWSTR>(First));
			return TRUE;
		default:
			return FALSE;
		}
	}

	bool GetChannelInfo(int Space, int Index, TVTest::ChannelInfo *pInfo)
	{
		CChannelInfo channel;
		if (!m_Core.GetScannedChannelInfo(Space, Index, &channel))
			return false;
		TVTest::ChannelInfo info = {};
		info.Space = channel.GetSpace();
		info.Channel = channel.GetChannel();
		info.NetworkID = channel.GetNetworkID();
		info.TransportStreamID = channel.GetTransportStreamID();
		info.ServiceID = channel.GetServiceID();
		info.RemoteControlKeyID = channel.GetRemoteControlKeyID();
		lstrcpynW(info.szChannelName, channel.GetName(), _countof(info.szChannelName));
		return StoreChannel(pInfo, info);
	}

	bool GetCurrentChannelInfo(TVTest::ChannelInfo *pInfo)
	{
		TSTask::ChannelInfo channel;
		if (!m_Core.GetChannel(&channel) || channel.Space < 0 || channel.Channel < 0)
			return false;
		TVTest::ChannelInfo info = {};
		info.Space = channel.Space;
		info.Channel = channel.Channel;
		info.NetworkID = channel.NetworkID;
		info.TransportStreamID = channel.TransportStreamID;
		info.ServiceID = channel.ServiceID;
		info.RemoteControlKeyID = channel.RemoteControlKeyID;
		ChannelStreamIDs ids;
		if (m_Core.GetStreamIDs(&ids) && ids.TransportStreamID && ids.TransportStreamID != 0xFFFF)
			info.TransportStreamID = ids.TransportStreamID;
		lstrcpynW(info.szChannelName, channel.ChannelName.c_str(), _countof(info.szChannelName));
		return StoreChannel(pInfo, info);
	}

	bool GetServiceInfo(LPARAM Index, TVTest::ServiceInfo *pInfo)
	{
		ServiceList services;
		if (!pInfo || (pInfo->Size != sizeof(*pInfo) && pInfo->Size != SERVICEINFO_SIZE_V1)
				|| Index < 0 || !m_Core.GetServiceList(&services)
				|| static_cast<size_t>(Index) >= services.size())
			return false;
		const auto &service = services[Index];
		TVTest::ServiceInfo info = {};
		info.Size = pInfo->Size;
		info.ServiceID = service.ServiceID;
		lstrcpynW(info.szServiceName, service.ServiceName.c_str(), _countof(info.szServiceName));
		memcpy(pInfo, &info, info.Size);
		return true;
	}

	bool GetStatus(StatusInfo *pInfo)
	{
		StreamStatistics statistics;
		if (!pInfo || (pInfo->Size != sizeof(*pInfo) && pInfo->Size != STATUSINFO_SIZE_V1)
				|| !m_Core.GetStreamStatistics(&statistics))
			return false;
		StatusInfo info = {};
		info.Size = pInfo->Size;
		info.SignalLevel = statistics.SignalLevel;
		info.BitRate = statistics.BitRate;
		info.DropPacketCount = static_cast<DWORD>(statistics.DiscontinuityCount);
		info.ErrorPacketCount = static_cast<DWORD>(statistics.ErrorPacketCount + statistics.DiscontinuityCount);
		info.ScramblePacketCount = static_cast<DWORD>(statistics.ScramblePacketCount);
		memcpy(pInfo, &info, info.Size);
		return true;
	}

	bool GetRecordStatus(RecordStatusInfo *pInfo, RecordStatusFlag Flags)
	{
		RecordingInfo recording;
		if (!pInfo || pInfo->Size != sizeof(*pInfo) || !m_Core.GetRecordingInfo(&recording))
			return false;

		pInfo->Status = RECORD_STATUS_NOTRECORDING;
		pInfo->StartTime = {};
		pInfo->RecordTime = 0;
		pInfo->PauseTime = 0;
		pInfo->StopTimeSpec = RECORD_STOP_NOTSPECIFIED;
		pInfo->StopTime.Duration = 0;
		m_RecordingFile.clear();
		if (pInfo->pszFileName && pInfo->MaxFileName > 0)
			pInfo->pszFileName[0] = L'\0';
		if (recording.State == RECORDING_STATE_NOT_RECORDING || recording.FilePaths.empty())
			return true;

		if (pInfo->pszFileName && pInfo->MaxFileName > 0) {
			WCHAR fullPath[MAX_PATH];
			LPWSTR fileName = nullptr;
			const auto length = GetFullPathNameW(recording.FilePaths.back().c_str(), _countof(fullPath), fullPath, &fileName);
			// DiskRelay appends the base name and a .partN suffix to a MAX_PATH buffer.
			if (!length || length >= _countof(fullPath) || length >= static_cast<DWORD>(pInfo->MaxFileName)
					|| !fileName || m_RelayFolderLength + 1 + lstrlenW(fileName) + 6 >= MAX_PATH)
				return false;
			lstrcpynW(pInfo->pszFileName, fullPath, pInfo->MaxFileName);
		}

		pInfo->Status = RECORD_STATUS_RECORDING;
		pInfo->StartTime = recording.StartTime;
		if (!(Flags & RECORD_STATUS_FLAG_UTC))
			FileTimeToLocalFileTime(&recording.StartTime, &pInfo->StartTime);
		pInfo->RecordTime = static_cast<DWORD>(std::min<ULONGLONG>(GetTickCount64() - recording.StartTickCount, MAXDWORD));
		m_RecordingFile = recording.FilePaths.back();
		m_RecordingStartTick = recording.StartTickCount;
		return true;
	}

	bool SetStreamCallback(const StreamCallbackInfo *pInfo)
	{
		if (!pInfo || pInfo->Size != sizeof(*pInfo) || !pInfo->Callback)
			return false;
		CBlockLock lock(m_StreamLock);
		if (pInfo->Flags == STREAM_CALLBACK_REMOVE && m_Stream.Callback == pInfo->Callback) {
			m_Stream = {};
			return true;
		}
		if (pInfo->Flags != 0 || m_Stream.Callback)
			return false;
		m_Stream = *pInfo;
		return true;
	}

	bool OnPacket(const ::CTsPacket *pPacket) override
	{
		CBlockLock lock(m_StreamLock);
		if (m_Stream.Callback && pPacket->GetSize() == TS_PACKETSIZE) {
			BYTE packet[TS_PACKETSIZE];
			memcpy(packet, pPacket->GetData(), sizeof(packet));
			m_Stream.Callback(packet, m_Stream.pClientData);
		}
		return true;
	}

	void OnReset() override {}

	// Core callbacks may run with engine locks held. The plugin queries the core
	// and resets its own stream queue when handling these events on the UI thread.
	void QueueEvent(UINT Event, LPARAM Value = 0)
	{
		if (m_DiskRelay == (Event == EVENT_RECORDSTATUSCHANGE))
			PostMessageW(m_Param.hwndApp, WindowMessage, Event, Value);
	}
	void OnBonDriverLoaded(LPCWSTR) override { QueueEvent(EVENT_DRIVERCHANGE); }
	void OnBonDriverUnloaded() override { QueueEvent(EVENT_DRIVERCHANGE); }
	void OnTunerOpened() override { QueueEvent(EVENT_DRIVERCHANGE); }
	void OnTunerClosed() override { QueueEvent(EVENT_DRIVERCHANGE); }
	void OnChannelChanged(DWORD, DWORD, WORD) override { QueueEvent(EVENT_CHANNELCHANGE); }
	void OnServiceChanged(WORD) override { QueueEvent(EVENT_SERVICECHANGE); }
	void OnStreamChanged() override { QueueEvent(EVENT_SERVICEUPDATE); }
	void OnRecordingStarted(const RecordingInfo &) override { QueueEvent(EVENT_RECORDSTATUSCHANGE, RECORD_STATUS_RECORDING); }
	void OnRecordingStopped() override { QueueEvent(EVENT_RECORDSTATUSCHANGE, RECORD_STATUS_NOTRECORDING); }

	CTSTaskAppCore &m_App;
	CTSTaskCore &m_Core;
	const bool m_DiskRelay;
	bool m_Enabled = false;
	DWORD m_RelayFolderLength = 0;
	String m_RecordingFile;
	ULONGLONG m_RecordingStartTick = 0;
	HMODULE m_Module = nullptr;
	PluginParam m_Param = {};
	FinalizeFunc m_Finalize = nullptr;
	bool m_Initialized = false;
	bool m_EventsAttached = false;
	bool m_GrabberAttached = false;
	EventCallbackFunc m_EventCallback = nullptr;
	void *m_EventData = nullptr;
	CLocalLock m_StreamLock;
	StreamCallbackInfo m_Stream = {};
};

bool CTVTestControlPlugin::IsTvRockPlugin(LPCWSTR Name)
{
	return lstrcmpiW(Name, L"TvRockOnTVTest.tvtp") == 0
		|| lstrcmpiW(Name, L"TvRockOnTVTest.x64.tvtp") == 0;
}

bool CTVTestControlPlugin::IsDiskRelayPlugin(LPCWSTR Name)
{
	return lstrcmpiW(Name, L"DiskRelay.tvtp") == 0
		|| lstrcmpiW(Name, L"DiskRelay.x64.tvtp") == 0;
}

CTVTestControlPlugin::CTVTestControlPlugin(CTSTaskAppCore &App) : m_App(App) {}
CTVTestControlPlugin::~CTVTestControlPlugin() = default;

bool CTVTestControlPlugin::Load(LPCWSTR Name)
{
	const bool diskRelay = IsDiskRelayPlugin(Name);
	auto &plugin = diskRelay ? m_DiskRelay : m_TvRock;
	if (plugin || (!diskRelay && !IsTvRockPlugin(Name)))
		return false;
	String directory;
	if (!GetModuleDirectory(nullptr, &directory))
		return false;
	PathUtility::Append(&directory, L"Plugins");
	PathUtility::Append(&directory, Name);
	auto session = std::unique_ptr<Session>(new Session(m_App, diskRelay));
	if (!session->Load(directory)) {
		OutLog(LOG_ERROR, L"%s could not initialize.", Name);
		return false;
	}
	plugin = std::move(session);
	OutLog(LOG_INFO, diskRelay ? L"%s is monitoring recording free space." : L"%s is handling TvRockDTV commands.", Name);
	return true;
}

void CTVTestControlPlugin::Finalize()
{
	// If the core is busy, keep the detached session alive until core shutdown
	// clears its event list. It no longer calls into the unloaded plugin.
	if (m_DiskRelay && m_DiskRelay->Unload())
		m_DiskRelay.reset();
	if (m_TvRock && m_TvRock->Unload())
		m_TvRock.reset();
}

void CTVTestControlPlugin::DispatchEvent(UINT Event, LPARAM Value)
{
	const auto &plugin = Event == EVENT_RECORDSTATUSCHANGE ? m_DiskRelay : m_TvRock;
	if (plugin)
		plugin->DispatchEvent(Event, Value);
}
}
