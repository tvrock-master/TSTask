#pragma once

#include "../BonTsEngine/MediaDecoder.h"
#include <memory>
#include <string>
#include <vector>

namespace TSTask
{
	class CSettings;
	struct TVTestPluginSettings
	{
		std::wstring Name;
		std::wstring ModuleFileName;
		std::wstring DeviceName;
		std::wstring FilterName;
	};
	std::vector<TVTestPluginSettings> ReadTVTestPlugins(CSettings &Settings);
}

// Hosts compatible TVTest TS postprocessors before recording and streaming.
class CTVTestTSProcessor : public CMediaDecoder
{
public:
	CTVTestTSProcessor();
	~CTVTestTSProcessor() override;

	void SetPlugins(const std::vector<TSTask::TVTestPluginSettings> &Plugins);
	void Start();
	void Stop();
	void Finalize() override;
	void Reset() override;
	const bool InputMedia(CMediaData *pData, DWORD InputIndex = 0) override;

private:
	class Session;
	struct Plugin
	{
		TSTask::TVTestPluginSettings Settings;
		std::unique_ptr<Session> Instance;
	};
	std::vector<Plugin> m_Plugins;
};
