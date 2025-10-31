#include "EngineSetting.h"
#include "ReflectionYml.h"
#include "PakHelper.h"


bool EngineSetting::Initialize()
{

#ifndef BUILD_FLAG
	bool isSuccess = LoadSettings();
	char* vcInstallDir = nullptr;
	size_t len = 0;

	std::string output = ExecuteVsWhere();

	if (output.empty())
	{
		std::cout << "Visual Studio not found.\n";
		return false;
	}

	output.erase(std::remove(output.begin(), output.end(), '\r'), output.end());
	output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());

	std::cout << "VS Install Path: " << output << std::endl;

	if (output.find("Preview") != std::string::npos)
	{
		m_msvcVersion = MSVCVersion::Comunity2022Preview;
	}
	else if (output.find("2022") != std::string::npos)
	{
		m_msvcVersion = MSVCVersion::Comunity2022;
	}
	else if (output.find("18") != std::string::npos)
	{
		m_msvcVersion = MSVCVersion::Comunity2026Insideration;
	}
	else
	{
		m_msvcVersion = MSVCVersion::None;
		std::cout << "Unsupported Visual Studio version.\n";
		return false;
	}

	isSuccess = EngineSetting::LoadSettings();
#else
	UnpackageGameAssets();
	bool isSuccess = EngineSetting::LoadSettings();
#endif

	return isSuccess;
}

bool EngineSetting::SaveSettings()
{
	// Implement saving logic here
	file::path engineSettingsPath = PathFinder::ProjectSettingPath("EngineSettings.asset");

	std::ofstream settingsFile(engineSettingsPath);
	MetaYml::Node rootNode;

	rootNode["lastWindowSize"]["x"] = m_lastWindowSize.x;
	rootNode["lastWindowSize"]["y"] = m_lastWindowSize.y;
	rootNode["msvcVersion"] = static_cast<int>(m_msvcVersion);
	rootNode["renderPassSettings"] = Meta::Serialize(&m_renderPassSettings);
	rootNode["m_contentsBrowserStyle"] = (int)m_contentsBrowserStyle;
	file::path buildGameProjectName = m_buildGameName;
	file::path startupSceneName = m_startupSceneName;
	rootNode["buildGameName"] = buildGameProjectName.string();
	rootNode["startupSceneName"] = startupSceneName.string();
	rootNode["imguiScale"] = m_imguiScale;

	settingsFile << rootNode;

	settingsFile.close();

	return true;
}

bool EngineSetting::LoadSettings()
{
	bool isSuccess = true;
	// Implement loading logic here
	file::path engineSettingsPath = PathFinder::ProjectSettingPath("EngineSettings.asset");

	if (!file::exists(engineSettingsPath))
	{
		//initialize default settings
		isSuccess = SaveSettings();
	}

	MetaYml::Node rootNode = MetaYml::LoadFile(engineSettingsPath.string());

	m_lastWindowSize =
	{
		rootNode["lastWindowSize"]["x"].as<float>(),
		rootNode["lastWindowSize"]["y"].as<float>()
	};

	if (rootNode["renderPassSettings"])
		Meta::Deserialize(&m_renderPassSettings, rootNode["renderPassSettings"]);

	if (rootNode["m_contentsBrowserStyle"])
	{
		m_contentsBrowserStyle = static_cast<ContentsBrowserStyle>(rootNode["m_contentsBrowserStyle"].as<int>());
	}
	else
	{
		m_contentsBrowserStyle = ContentsBrowserStyle::Tile; // Default style if not set
	}
	file::path buildGameProjectName = rootNode["buildGameName"].as<std::string>("Train Your Asis");
	file::path startupSceneName = rootNode["startupSceneName"].as<std::string>("SampleScene");
	m_buildGameName = buildGameProjectName.wstring();
	m_startupSceneName = startupSceneName.wstring();

	if(rootNode["imguiScale"])
	{
		m_imguiScale = rootNode["imguiScale"].as<float>();
	}

	return isSuccess;
}