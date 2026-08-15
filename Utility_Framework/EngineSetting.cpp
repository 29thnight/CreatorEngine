#include "EngineSetting.h"
#include "ReflectionYml.h"
#include "PakHelper.h"
#include "EngineMode.h"

#include <cctype>
#include <cstdio>

namespace
{
	bool TryParseRenderBackend(const MetaYml::Node& node,
		RenderBackend& outBackend, std::string& outError)
	{
		if (!node || !node.IsScalar())
		{
			outError = "render backend 값은 dx12 또는 vulkan 문자열이어야 한다";
			return false;
		}

		std::string value;
		try
		{
			value = node.as<std::string>();
		}
		catch (const MetaYml::Exception& exception)
		{
			outError = "render backend 설정을 읽지 못했다: " +
				std::string(exception.what());
			return false;
		}

		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		if ("dx12" == value)
		{
			outBackend = RenderBackend::DX12;
			return true;
		}
		if ("vulkan" == value)
		{
			outBackend = RenderBackend::Vulkan;
			return true;
		}

		outError = "지원하지 않는 render backend '" + value +
			"' (허용: dx12, vulkan)";
		return false;
	}
}


bool EngineSetting::Initialize()
{
	m_isEditorMode = !EngineMode::IsPlayer();
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

	if (!LoadSettings()) return false;
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

	return true;
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
	rootNode["render"]["backend"] = RenderBackendName(m_editorRenderBackend);
	rootNode["build"]["render"]["backend"] = RenderBackendName(m_buildRenderBackend);

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

	// 새 계약의 단일 문자열을 우선한다. 구 bool 둘은 읽기 호환만 제공하고
	// SaveSettings에서는 다시 쓰지 않는다. 구 조합 중 하나라도 Vulkan이면
	// 과도기 startWithVulkan 규칙과 같은 결과로 이관한다.
	std::string backendError;
	const MetaYml::Node editorBackendNode = rootNode["render"]["backend"];
	if (editorBackendNode)
	{
		if (!TryParseRenderBackend(editorBackendNode, m_editorRenderBackend,
			backendError))
		{
			const std::string message =
				"EngineSettings render.backend 오류: " + backendError;
			std::fprintf(stderr, "[RenderBackend] %s\n", message.c_str());
			Debug->LogError(message);
			return false;
		}
	}
	else
	{
		bool legacyDx12 = true;
		if (rootNode["renderBackendDx12"])
			legacyDx12 = rootNode["renderBackendDx12"].as<bool>();
		if (rootNode["imguiBackendDx12"])
			legacyDx12 = legacyDx12 && rootNode["imguiBackendDx12"].as<bool>();
		m_editorRenderBackend = legacyDx12 ? RenderBackend::DX12 : RenderBackend::Vulkan;
	}

	m_buildRenderBackend = m_editorRenderBackend;
	const MetaYml::Node buildBackendNode = rootNode["build"]["render"]["backend"];
	if (buildBackendNode &&
		!TryParseRenderBackend(buildBackendNode, m_buildRenderBackend, backendError))
	{
		const std::string message =
			"EngineSettings build.render.backend 오류: " + backendError;
		std::fprintf(stderr, "[RenderBackend] %s\n", message.c_str());
		Debug->LogError(message);
		return false;
	}

	m_activeRenderBackend = EngineMode::IsPlayer()
		? m_buildRenderBackend : m_editorRenderBackend;
	Debug->LogDebug(std::string("[RenderBackend] editor=") +
		RenderBackendName(m_editorRenderBackend) + " build=" +
		RenderBackendName(m_buildRenderBackend) + " active=" +
		RenderBackendName(m_activeRenderBackend));

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
