#include "EditorSettingsStore.h"

#include "LogSystem.h"
#include <source_location>
#include "PathFinder.h"
#include "ReflectionTypedYml.h"
#include "RuntimeSettings.h"
#include "AuthoringParsedDocument.h"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace
{
    std::atomic_uint64_t g_settingsCandidateId{ 0 };

    void OverlayKnownMap(Authoring::WriteNode target,
        const Authoring::ReadNode& known)
    {
        target.SetMap();
        for (const auto field : known.Map())
        {
            const std::string key = field.key.AsStringChecked();
            const Authoring::ReadNode knownValue = field.value;
            const Authoring::WriteNode existingValue = target.Child(key);
            if (knownValue.IsMap() && existingValue.Read().IsMap())
            {
                OverlayKnownMap(existingValue, knownValue);
            }
            else
            {
                existingValue.Assign(knownValue);
            }
        }
    }

    // build.ps1의 (Split-Path -Leaf $projectRoot) 정화 규칙과 같은 문자 집합이다.
    // 패키징이 스테이지 폴더 이름을 그렇게 만들므로 두 곳이 갈리면 안 된다.
    std::string SanitizeProjectName(std::string value)
    {
        for (char& character : value)
        {
            const unsigned char raw = static_cast<unsigned char>(character);
            const bool forbidden = raw < 0x20 ||
                '<' == character || '>' == character || ':' == character ||
                '"' == character || '/' == character || '\\' == character ||
                '|' == character || '?' == character || '*' == character;
            if (forbidden) character = '_';
        }
        return value;
    }

    std::string ProjectNameFromProjectRoot()
    {
        const std::filesystem::path projectRoot = PathFinder::BaseProjectPath();
        if (projectRoot.empty()) return {};
        return SanitizeProjectName(projectRoot.filename().string());
    }

    // where 는 호출자에서 채워져 여기까지 온다. 생략하면 아래 PrintLog 의 기본
    // 인자가 이 줄에서 채워져 호출자 열여덟이 모두 이 한 줄을 가리킨다.
    bool ReportSettingsError(const std::string& message,
        std::source_location where = std::source_location::current()) noexcept
    {
        std::fprintf(stderr, "[EditorSettings] %s\n", message.c_str());
        if (Log::IsAlive()) Debug::PrintLog(spdlog::level::err, message, where);
        return false;
    }
}

EditorSettingsStore& EditorSettingsStore::Get() noexcept
{
    static EditorSettingsStore instance;
    return instance;
}

bool EditorSettingsStore::Initialize() noexcept
{
    EditorPreferences preferences{};
    BuildSettings buildSettings{};
    // Player의 빌드 설정은 Editor의 빌드 고정 DX12 정책과 독립적이다.
    buildSettings.SetStartupSceneName(RuntimeSettings::Get().GetStartupSceneName());

    const std::filesystem::path settingsPath =
        PathFinder::ProjectSettingPath("EngineSettings.asset");
    try
    {
		if (std::filesystem::exists(settingsPath))
		{
			std::string parseError;
			const Authoring::ParsedDocument document =
				Authoring::ParsedDocument::ParseFile(
					settingsPath.string(), parseError);
			if (!document)
				return ReportSettingsError(
					"Unable to load Editor settings: " + parseError);
			const Authoring::ReadNode root = document.Root();

			if (root["projectName"])
			{
				const Authoring::ReadNode projectNameNode = root["projectName"];
				if (!projectNameNode.IsScalar())
					return ReportSettingsError("projectName must be a scalar.");
				buildSettings.SetProjectName(
					SanitizeProjectName(projectNameNode.AsString()));
			}

            if (root["imguiScale"])
            {
				const float scale = root["imguiScale"].As<float>();
                if (!std::isfinite(scale) || scale <= 0.0f)
                    return ReportSettingsError("imguiScale must be a positive finite value.");
                preferences.SetImGuiScale(scale);
            }

            if (root["contentTreeWidth"])
            {
                const float width = root["contentTreeWidth"].As<float>();
                if (std::isfinite(width) && width >= 140.f && width <= 600.f)
                    preferences.SetContentTreeWidth(width);
            }

            if (root["startupSceneName"])
            {
				const std::filesystem::path startupScene =
					root["startupSceneName"].AsString();
                buildSettings.SetStartupSceneName(startupScene.wstring());
            }

			const Authoring::ReadNode buildNode = root["build"];
            if (buildNode)
            {
                if (!buildNode.IsMap())
                {
                    return ReportSettingsError("build must be a map.");
                }

                if (const Authoring::ReadNode developmentNode = buildNode["development"])
                {
                    if (!developmentNode.IsScalar())
                    {
                        return ReportSettingsError("build.development must be true or false.");
                    }
                    buildSettings.SetDevelopmentBuild(developmentNode.As<bool>());
                }

                // build.render.backend 는 Player 패키징 선택이다. 에디터 시작은 이 값에
                // 기대지 않으므로 검사하지 않는다. 알아볼 수 없으면 원문만 남기고
                // 패키징 때 거절한다(GameBuilderSystem::BuildGame).
				const Authoring::ReadNode buildRenderNode = buildNode["render"];
				const Authoring::ReadNode buildBackendNode = buildRenderNode && buildRenderNode.IsMap()
					? buildRenderNode["backend"] : Authoring::ReadNode{};
                if (buildRenderNode && !buildRenderNode.IsMap())
                {
                    buildSettings.SetUnrecognizedRenderBackend("(build.render is not a map)");
                }
                else if (buildBackendNode)
                {
                    RenderBackend backend{};
                    const std::string backendName = buildBackendNode.IsScalar()
                        ? buildBackendNode.AsString() : std::string("(not a scalar)");
                    if (TryParseRenderBackend(backendName, backend))
                        buildSettings.SetRenderBackend(backend);
                    else
                        buildSettings.SetUnrecognizedRenderBackend(backendName);
                }
            }
        }

        if (buildSettings.GetProjectName().empty())
            buildSettings.SetProjectName(ProjectNameFromProjectRoot());

        m_preferences = std::move(preferences);
        m_buildSettings = std::move(buildSettings);
        m_initialized = true;

        if (!std::filesystem::exists(settingsPath)) return Save();
        return true;
    }
	catch (const std::exception& exception)
    {
        return ReportSettingsError("Editor settings initialization failed: " +
            std::string(exception.what()));
    }
    catch (...)
    {
        return ReportSettingsError("Editor settings initialization failed with an unknown error.");
    }
}

bool EditorSettingsStore::Save() noexcept
{
    if (!m_initialized)
        return ReportSettingsError("Editor settings were saved before initialization.");
    if (!PathFinder::IsAssetAuthoringEnabled())
        return ReportSettingsError("Runtime host cannot write EngineSettings.asset.");

    const std::filesystem::path settingsPath =
        PathFinder::ProjectSettingPath("EngineSettings.asset");
    const std::filesystem::path candidatePath = settingsPath.parent_path() /
        (settingsPath.filename().wstring() + L".candidate." +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(g_settingsCandidateId.fetch_add(1, std::memory_order_relaxed)));

    const auto removeCandidate = [&candidatePath]() noexcept
    {
        std::error_code error{};
        std::filesystem::remove(candidatePath, error);
    };

    try
    {
        Authoring::WriteDocument rootDocument;
        if (std::filesystem::exists(settingsPath))
        {
            std::string parseError;
            auto parsed = Authoring::WriteDocument::ParseFile(
                settingsPath, &parseError);
            if (!parsed)
                return ReportSettingsError(
                    "Unable to load existing Editor settings for save: " +
                    parseError);
            rootDocument = std::move(*parsed);
        }
        const Authoring::WriteNode root = rootDocument.Root();
        if (!root.Read().IsMap()) root.SetMap();

        RenderPassSettings renderPassSettings =
            RuntimeSettings::Get().GetRenderPassSettings();
		Authoring::WriteDocument renderPassDocument;
		Meta::Typed::SerializeObjectInto(renderPassSettings, renderPassDocument.Root());
		const Authoring::ReadNode serializedRenderPassSettings =
			renderPassDocument.Root().Read();
        const Authoring::WriteNode storedRenderPassSettings =
            root.Child("renderPassSettings");
        if (storedRenderPassSettings.Read().IsMap()
            && serializedRenderPassSettings.IsMap())
            OverlayKnownMap(storedRenderPassSettings, serializedRenderPassSettings);
        else
            storedRenderPassSettings.Assign(serializedRenderPassSettings);
        root.Child("startupSceneName").SetScalar(
            std::filesystem::path(
                m_buildSettings.GetStartupSceneName()).string());
        root.Child("imguiScale").SetScalar(m_preferences.GetImGuiScale());
        // W3: read the legacy personal width for migration only; workspace owns future writes.
        root.Child("projectName").SetScalar(m_buildSettings.GetProjectName());
        // Editor 호스트는 백엔드 키를 읽거나 덮어쓰지 않는다.
        // 패키징이 build.render.backend를 Player의 런타임 설정에 투영한다.
        // 알아볼 수 없는 값은 사용자가 고를 때까지 원문 그대로 둔다.
        if (m_buildSettings.HasRecognizedRenderBackend())
            root.Child("build").Child("render").Child("backend").SetScalar(
                RenderBackendName(m_buildSettings.GetRenderBackend()));
        root.Child("build").Child("development").SetScalar(m_buildSettings.IsDevelopmentBuild());
        root.RemoveChild("renderBackendDx12");
        root.RemoveChild("imguiBackendDx12");

        std::ofstream output(candidatePath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
            return ReportSettingsError("Unable to open the Editor settings candidate file.");
        output << rootDocument.Dump();
        output.flush();
        if (!output.good())
        {
            output.close();
            removeCandidate();
            return ReportSettingsError("Unable to flush the Editor settings candidate file.");
        }
        output.close();
        if (output.fail())
        {
            removeCandidate();
            return ReportSettingsError("Unable to close the Editor settings candidate file.");
        }

        if (!MoveFileExW(candidatePath.c_str(), settingsPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();
            removeCandidate();
            return ReportSettingsError("Unable to atomically replace EngineSettings.asset (Win32 " +
                std::to_string(error) + ").");
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        removeCandidate();
        return ReportSettingsError("Unable to save Editor settings: " +
            std::string(exception.what()));
    }
    catch (...)
    {
        removeCandidate();
        return ReportSettingsError("Unable to save Editor settings due to an unknown error.");
    }
}
