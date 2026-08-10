#include "EngineSetting.h"
#include "ReflectionYml.h"
#include "PakHelper.h"
#include "EngineMode.h"


bool EngineSetting::Initialize()
{
	// ── 모드 분기 (B0-1: BUILD_FLAG → 런타임 EngineMode) ──
	//
	// 플레이어는 설정을 읽기 전에 pak을 %TEMP%에 풀어야 한다 —
	// LoadSettings가 읽는 ProjectSetting 자체가 pak 안에 있다.
	// vswhere/MSVC 판별은 에디터의 게임 빌드 버튼용이라 플레이어와 무관하다
	// (그 기계 전체의 은퇴는 B2 몫).
	if (EngineMode::IsPlayer())
	{
		// 지난 실행의 언팩 잔재를 여기서 걷는다 — 종료 시점 정리는 엔진
		// 해체(DataSystem::Destroy)가 그 파일들을 아직 밟는 동안 지우는
		// 꼴이라 첫 스모크에서 종료 크래시로 나타났다(B0-4). 부팅 정리는
		// 크래시로 죽은 실행의 잔재까지 함께 치운다.
		CleanupUnpackedGameAssets();
		UnpackageGameAssets();
		return EngineSetting::LoadSettings();
	}

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
	rootNode["renderBackendDx12"] = m_useDx12Backend;
	rootNode["imguiBackendDx12"] = m_useDx12ImGuiShell;

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

	// 구 설정의 false는 더 이상 백엔드 선택 의미가 없다.
	m_useDx12Backend = true;

	if (rootNode["imguiBackendDx12"])
		m_useDx12ImGuiShell = rootNode["imguiBackendDx12"].as<bool>();

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
