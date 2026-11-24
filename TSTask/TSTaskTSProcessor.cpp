#include "stdafx.h"
#include "TSTaskTSProcessor.h"
#include "../BonTsEngine/TsStream.h"
#include "../Common/TSTaskBase.h"
#include "../Common/Settings.h"
#include "TVTestControlPlugin.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <map>
#include <wrl/client.h>
#include "TVTestSDK/TVTestPlugin.h"
#include "TVTestSDK/TVTestInterface.h"

namespace
{
	using namespace TVTest;
	using namespace TVTest::Interface;
	using Microsoft::WRL::ComPtr;

	// As in TVTest, the input packet is reused and its data is borrowed for InputPacket.
	class Packet final : public ITSPacket
	{
	public:
		void SetData(const BYTE *pData)
		{
			memcpy(m_Data, pData, sizeof(m_Data));
			m_Modified = FALSE;
		}

		STDMETHODIMP QueryInterface(REFIID iid, void **ppObject) override
		{
			if (!ppObject)
				return E_POINTER;
			*ppObject = nullptr;
			if (iid != IID_IUnknown && iid != __uuidof(ITSPacket))
				return E_NOINTERFACE;
			*ppObject = static_cast<ITSPacket*>(this);
			AddRef();
			return S_OK;
		}
		STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_Refs); }
		STDMETHODIMP_(ULONG) Release() override
		{
			const ULONG refs = InterlockedDecrement(&m_Refs);
			if (!refs)
				delete this;
			return refs;
		}
		STDMETHODIMP GetData(BYTE **ppData) override
		{
			if (!ppData)
				return E_POINTER;
			*ppData = m_Data;
			return S_OK;
		}
		STDMETHODIMP GetSize(ULONG *pSize) override
		{
			if (!pSize)
				return E_POINTER;
			*pSize = sizeof(m_Data);
			return S_OK;
		}
		STDMETHODIMP SetModified(BOOL Modified) override
		{
			m_Modified = Modified;
			return S_OK;
		}
		STDMETHODIMP GetModified() override { return m_Modified ? S_OK : S_FALSE; }

	private:
		LONG m_Refs = 1;
		BOOL m_Modified = FALSE;
		BYTE m_Data[TS_PACKETSIZE];
	};
}

class CTVTestTSProcessor::Session
{
public:
	Session(CTVTestTSProcessor &Owner, const std::wstring &Name)
		: m_Owner(Owner), m_Name(Name)
	{
		m_Host.Attach(new Host(*this));
		m_Packet.Attach(new Packet);
	}

	~Session()
	{
		Stop();
		if (m_ProcessorInitialized)
			m_Processor->Finalize();
		m_FilterModule.Reset();
		m_Processor.Reset();
		if (m_PluginInitialized)
			m_PluginFinalize();
		if (m_Library)
			FreeLibrary(m_Library);
	}

	bool Load(LPCWSTR PluginPath, bool RequiresFilterModule)
	{
		m_Library = LoadLibraryExW(PluginPath, nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!m_Library) {
			m_Owner.Trace(CTracer::TYPE_WARNING,
				L"%s could not be loaded (Windows error %lu).", m_Name.c_str(), GetLastError());
			return false;
		}

		const auto GetVersion = reinterpret_cast<GetVersionFunc>(GetProcAddress(m_Library, "TVTGetVersion"));
		const auto GetInfo = reinterpret_cast<GetPluginInfoFunc>(GetProcAddress(m_Library, "TVTGetPluginInfo"));
		const auto Initialize = reinterpret_cast<InitializeFunc>(GetProcAddress(m_Library, "TVTInitialize"));
		m_PluginFinalize = reinterpret_cast<FinalizeFunc>(GetProcAddress(m_Library, "TVTFinalize"));
		if (!GetVersion || !GetInfo || !Initialize || !m_PluginFinalize)
			return false;

		const DWORD version = GetVersion();
		PluginInfo info = {};
		if (GetMajorVersion(version) != GetMajorVersion(TVTEST_PLUGIN_VERSION)
				|| GetMinorVersion(version) != GetMinorVersion(TVTEST_PLUGIN_VERSION)
				|| !GetInfo(&info))
			return false;

		m_Param.Callback = Callback;
		m_Param.pInternalData = this;
		m_Registering = true;
		m_PluginInitialized = Initialize(&m_Param) != FALSE;
		m_Registering = false;
		if (!m_PluginInitialized || !m_Processor || m_UnsupportedMessage)
			return false;
		if (RequiresFilterModule && FAILED(m_Processor.As(&m_FilterModule))) {
			m_Owner.Trace(CTracer::TYPE_WARNING, L"%s does not support filter modules.", m_Name.c_str());
			return false;
		}

		m_ProcessorInitialized = SUCCEEDED(m_Processor->Initialize(m_Host.Get()));
		return m_ProcessorInitialized;
	}

	bool Start(LPCWSTR ModulePath, const TSTask::TVTestPluginSettings &Settings)
	{
		if (m_Streaming)
			return true;
		if (ModulePath) {
			HRESULT result = m_FilterModule->LoadModule(ModulePath);
			if (FAILED(result)) {
				m_Owner.Trace(CTracer::TYPE_WARNING, L"%s: LoadModule failed (0x%08lX).",
					m_Name.c_str(), static_cast<unsigned long>(result));
				return false;
			}
			m_ModuleLoaded = true;
			int device = -1;
			if (Settings.DeviceName.empty())
				result = m_FilterModule->GetDefaultDevice(&device);
			else
				result = m_FilterModule->GetDeviceByName(Settings.DeviceName.c_str(), &device);
			if (FAILED(result) || device < 0) {
				m_Owner.Trace(CTracer::TYPE_WARNING, L"%s: filter device %s is unavailable (0x%08lX, index %d).",
					m_Name.c_str(), Settings.DeviceName.empty() ? L"(default)" : Settings.DeviceName.c_str(),
					static_cast<unsigned long>(result), device);
				Stop();
				return false;
			}
			const auto filterName = Settings.FilterName.empty() ? nullptr : Settings.FilterName.c_str();
			result = m_FilterModule->OpenFilter(device, filterName, m_Filter.GetAddressOf());
			if (FAILED(result) || !m_Filter) {
				m_Owner.Trace(CTracer::TYPE_WARNING, L"%s: OpenFilter failed (device %d, filter %s, 0x%08lX).",
					m_Name.c_str(), device, filterName ? filterName : L"(default)",
					static_cast<unsigned long>(result));
				Stop();
				return false;
			}
		}
		if (FAILED(m_Processor->SetEnableProcessing(TRUE))
				|| FAILED(m_Processor->Reset())) {
			Stop();
			return false;
		}
		if (FAILED(m_Processor->StartStreaming(m_Host.Get()))) {
			m_Processor->StopStreaming();
			Stop();
			return false;
		}
		m_Streaming = true;
		m_PacketErrorLogged = false;
		return true;
	}

	void Stop()
	{
		if (m_Streaming) {
			m_Streaming = false;
			m_Processor->StopStreaming();
		}
		m_Filter.Reset();
		if (m_ModuleLoaded) {
			m_FilterModule->UnloadModule();
			m_ModuleLoaded = false;
		}
	}

	void Reset()
	{
		if (m_Streaming)
			m_Processor->Reset();
	}

	CMediaData *Process(CMediaData *pData)
	{
		if (!m_Streaming || pData->GetSize() != TS_PACKETSIZE)
			return pData;

		// Keep the input intact so a processor error can fall back to the original TS.
		m_Packet->SetData(pData->GetData());
		m_OutputReady = false;
		m_UnexpectedOutput = false;
		m_ReceivingThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
		const HRESULT result = m_Processor->InputPacket(m_Packet.Get());
		m_ReceivingThread.store(0, std::memory_order_relaxed);
		if (SUCCEEDED(result) && m_OutputReady && !m_UnexpectedOutput)
			return &m_OutputPacket;

		if (!m_PacketErrorLogged) {
			m_PacketErrorLogged = true;
			m_Owner.Trace(CTracer::TYPE_WARNING, L"%s did not return exactly one valid TS packet.", m_Name.c_str());
		}
		return pData;
	}

private:
	class Host final : public IStreamingClient, public ITSOutput
	{
	public:
		explicit Host(Session &Owner) : m_Session(Owner) {}

		STDMETHODIMP QueryInterface(REFIID iid, void **ppObject) override
		{
			if (!ppObject)
				return E_POINTER;
			*ppObject = nullptr;
			if (iid == IID_IUnknown || iid == __uuidof(IStreamingClient))
				*ppObject = static_cast<IStreamingClient*>(this);
			else if (iid == __uuidof(ITSOutput))
				*ppObject = static_cast<ITSOutput*>(this);
			else
				return E_NOINTERFACE;
			AddRef();
			return S_OK;
		}
		STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_Refs); }
		STDMETHODIMP_(ULONG) Release() override
		{
			const ULONG refs = InterlockedDecrement(&m_Refs);
			if (!refs)
				delete this;
			return refs;
		}
		STDMETHODIMP OnError(const ErrorInfo *pInfo) override
		{
			return m_Session.WriteLog(CTracer::TYPE_ERROR, pInfo ? pInfo->pszText : nullptr);
		}
		STDMETHODIMP OutLog(Interface::LogType Type, LPCWSTR Text) override
		{
			const auto level = Type == LOG_ERROR ? CTracer::TYPE_ERROR
				: Type == LOG_WARNING ? CTracer::TYPE_WARNING : CTracer::TYPE_INFORMATION;
			return m_Session.WriteLog(level, Text);
		}
		STDMETHODIMP Notify(NotifyType Type, LPCWSTR Text) override
		{
			const auto level = Type == NOTIFY_ERROR ? CTracer::TYPE_ERROR
				: Type == NOTIFY_WARNING ? CTracer::TYPE_WARNING : CTracer::TYPE_INFORMATION;
			return m_Session.WriteLog(level, Text);
		}
		STDMETHODIMP OutputPacket(ITSPacket *pPacket) override
		{
			return m_Session.ReceiveOutput(pPacket);
		}

	private:
		Session &m_Session;
		LONG m_Refs = 1;
	};

	HRESULT WriteLog(CTracer::TraceType Level, LPCWSTR Text)
	{
		if (!Text)
			return E_POINTER;
		m_Owner.Trace(Level, L"%s: %s", m_Name.c_str(), Text);
		return S_OK;
	}

	HRESULT ReceiveOutput(ITSPacket *pPacket)
	{
		// The supported processors emit one packet on the input thread.
		if (m_ReceivingThread.load(std::memory_order_relaxed) != GetCurrentThreadId())
			return E_UNEXPECTED;
		if (m_OutputReady) {
			m_UnexpectedOutput = true;
			return E_UNEXPECTED;
		}
		BYTE *pBytes = nullptr;
		ULONG size = 0;
		if (!pPacket || FAILED(pPacket->GetSize(&size)) || size != TS_PACKETSIZE
				|| FAILED(pPacket->GetData(&pBytes)) || !pBytes)
			return E_INVALIDARG;

		if (m_OutputPacket.SetData(pBytes, size) != size)
			return E_OUTOFMEMORY;
		// Parse the returned header so downstream scramble counts reflect plugin output.
		m_OutputReady = m_OutputPacket.ParsePacket() == CTsPacket::EC_VALID;
		return m_OutputReady ? S_OK : E_FAIL;
	}

	static LRESULT CALLBACK Callback(PluginParam *pParam, MessageCode Message, LPARAM Param1, LPARAM Param2)
	{
		auto &session = *static_cast<Session*>(pParam->pInternalData);
		switch (Message) {
		case MESSAGE_GETVERSION:
			return MakeVersion(0, 10, 0);
		case MESSAGE_QUERYMESSAGE:
			return Param1 == MESSAGE_GETVERSION || Param1 == MESSAGE_QUERYMESSAGE
				|| Param1 == MESSAGE_ADDLOG || Param1 == MESSAGE_REGISTERTSPROCESSOR;
		case MESSAGE_ADDLOG:
			{
				const auto level = Param2 == LOG_TYPE_ERROR ? CTracer::TYPE_ERROR
					: Param2 == LOG_TYPE_WARNING ? CTracer::TYPE_WARNING : CTracer::TYPE_INFORMATION;
				return SUCCEEDED(session.WriteLog(level, reinterpret_cast<LPCWSTR>(Param1)));
			}
		case MESSAGE_REGISTERTSPROCESSOR:
			{
				const auto *pInfo = reinterpret_cast<const TSProcessorInfo*>(Param1);
				if (!session.m_Registering || session.m_Processor || !pInfo
						|| pInfo->Size != sizeof(*pInfo) || pInfo->Flags != TS_PROCESSOR_FLAG_NONE
						|| !pInfo->pTSProcessor || pInfo->ConnectPosition != TS_PROCESSOR_CONNECT_POSITION_POSTPROCESSING)
					return FALSE;
				session.m_Processor = pInfo->pTSProcessor;
				return TRUE;
			}
		default:
			if (session.m_Registering)
				session.m_UnsupportedMessage = true;
			return FALSE;
		}
	}

	CTVTestTSProcessor &m_Owner;
	const std::wstring m_Name;
	PluginParam m_Param = {};
	HMODULE m_Library = nullptr;
	FinalizeFunc m_PluginFinalize = nullptr;
	ComPtr<Host> m_Host;
	ComPtr<Packet> m_Packet;
	ComPtr<ITSProcessor> m_Processor;
	ComPtr<IFilterModule> m_FilterModule;
	ComPtr<IFilter> m_Filter;
	CTsPacket m_OutputPacket;
	bool m_Registering = false;
	bool m_UnsupportedMessage = false;
	bool m_PluginInitialized = false;
	bool m_ProcessorInitialized = false;
	bool m_ModuleLoaded = false;
	bool m_Streaming = false;
	std::atomic<DWORD> m_ReceivingThread{0};
	bool m_OutputReady = false;
	bool m_UnexpectedOutput = false;
	bool m_PacketErrorLogged = false;
};

CTVTestTSProcessor::CTVTestTSProcessor() = default;
CTVTestTSProcessor::~CTVTestTSProcessor() = default;

std::vector<TSTask::TVTestPluginSettings> TSTask::ReadTVTestPlugins(TSTask::CSettings &Settings)
{
	TSTask::CSettings::EntryList entries;
	if (!Settings.GetSectionEntries(L"Plugins", &entries))
		return {};

	std::map<unsigned long, const std::wstring*> names, modules, devices, filters;
	for (const auto &entry : entries) {
		if (entry.Name.size() <= 6 || _wcsnicmp(entry.Name.c_str(), L"Plugin", 6) != 0)
			continue;
		const wchar_t *number = entry.Name.c_str() + 6;
		if (*number < L'1' || *number > L'9')
			continue;
		wchar_t *end;
		errno = 0;
		const unsigned long index = wcstoul(number, &end, 10);
		if (errno == ERANGE)
			continue;
		if (*end == L'\0')
			names.emplace(index, &entry.Value);
		else if (_wcsicmp(end, L".Module") == 0)
			modules.emplace(index, &entry.Value);
		else if (_wcsicmp(end, L".Device") == 0)
			devices.emplace(index, &entry.Value);
		else if (_wcsicmp(end, L".Filter") == 0)
			filters.emplace(index, &entry.Value);
	}

	const auto findSetting = [](const auto &settings, unsigned long index) {
		const auto value = settings.find(index);
		return value != settings.end() ? *value->second : std::wstring();
	};

	std::vector<TVTestPluginSettings> result;
	for (const auto &entry : names) {
		const auto &name = *entry.second;
		if (name.empty())
			continue;
		if (name.size() <= 5 || _wcsicmp(name.c_str() + name.size() - 5, L".tvtp") != 0
				|| name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
			OutLog(LOG_WARNING, L"Invalid plugin file name in [Plugins]: %s", name.c_str());
			continue;
		}
		if (std::any_of(result.begin(), result.end(), [&](const TVTestPluginSettings &plugin) {
				return _wcsicmp(plugin.Name.c_str(), name.c_str()) == 0;
			}))
			continue;
		const auto module = findSetting(modules, entry.first);
		if (module == L"." || module == L".." || module.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
			OutLog(LOG_WARNING, L"Invalid filter module file name for %s in [Plugins]: %s", name.c_str(), module.c_str());
			continue;
		}
		result.push_back({name, module, findSetting(devices, entry.first), findSetting(filters, entry.first)});
	}
	return result;
}

void CTVTestTSProcessor::SetPlugins(const std::vector<TSTask::TVTestPluginSettings> &Plugins)
{
	m_Plugins.clear();
	for (const auto &plugin : Plugins) {
		if (!TSTask::CTVTestControlPlugin::IsTvRockPlugin(plugin.Name.c_str())
				&& !TSTask::CTVTestControlPlugin::IsDiskRelayPlugin(plugin.Name.c_str()))
			m_Plugins.push_back({plugin, nullptr});
	}
}

void CTVTestTSProcessor::Start()
{
	if (m_Plugins.empty())
		return;
	TSTask::String directory;
	if (!TSTask::GetModuleDirectory(nullptr, &directory))
		return;

	const auto pluginDirectory = directory + L"\\Plugins\\";
	for (auto &plugin : m_Plugins) {
		const auto &settings = plugin.Settings;
		const bool needsModule = !settings.ModuleFileName.empty();
		const auto modulePath = needsModule ? directory + L"\\" + settings.ModuleFileName : std::wstring();
		if (needsModule && !TSTask::PathUtility::IsFileExists(modulePath)) {
			Trace(CTracer::TYPE_WARNING, L"%s: filter module file not found (%s).", settings.Name.c_str(), modulePath.c_str());
			continue;
		}

		if (!plugin.Instance) {
			const auto pluginPath = pluginDirectory + settings.Name;
			std::unique_ptr<Session> session(new Session(*this, settings.Name));
			if (!session->Load(pluginPath.c_str(), needsModule)) {
				Trace(CTracer::TYPE_WARNING, L"%s could not be initialized as a TS postprocessor.", settings.Name.c_str());
				continue;
			}
			plugin.Instance = std::move(session);
		}
		if (!plugin.Instance->Start(needsModule ? modulePath.c_str() : nullptr, settings))
			Trace(CTracer::TYPE_WARNING, L"%s could not start processing TS.", settings.Name.c_str());
	}
}

void CTVTestTSProcessor::Stop()
{
	for (auto it = m_Plugins.rbegin(); it != m_Plugins.rend(); ++it) {
		if (it->Instance)
			it->Instance->Stop();
	}
}

void CTVTestTSProcessor::Finalize()
{
	for (auto it = m_Plugins.rbegin(); it != m_Plugins.rend(); ++it)
		it->Instance.reset();
}

void CTVTestTSProcessor::Reset()
{
	for (auto &plugin : m_Plugins) {
		if (plugin.Instance)
			plugin.Instance->Reset();
	}
}

const bool CTVTestTSProcessor::InputMedia(CMediaData *pData, DWORD)
{
	for (auto &plugin : m_Plugins) {
		if (plugin.Instance)
			pData = plugin.Instance->Process(pData);
	}
	return OutputMedia(pData);
}
