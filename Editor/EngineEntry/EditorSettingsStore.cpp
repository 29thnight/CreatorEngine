#include "EditorSettingsStore.h"

#include "LogSystem.h"
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

    bool ReportSettingsError(const std::string& message) noexcept
    {
        std::fprintf(stderr, "[EditorSettings] %s\n", message.c_str());
        if (Log::IsAlive()) Debug->LogError(message);
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
    preferences.SetRenderBackend(RuntimeSettings::Get().GetRenderBackend());
    buildSettings.SetRenderBackend(RuntimeSettings::Get().GetRenderBackend());
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

			if (root["m_contentsBrowserStyle"])
			{
				const int style = root["m_contentsBrowserStyle"].As<int>();
                if (style < static_cast<int>(ContentsBrowserStyle::Tile) ||
                    style > static_cast<int>(ContentsBrowserStyle::Tree))
                {
                    return ReportSettingsError("m_contentsBrowserStyle is outside the supported range.");
                }
                preferences.SetContentsBrowserStyle(static_cast<ContentsBrowserStyle>(style));
            }

            if (root["imguiScale"])
            {
				const float scale = root["imguiScale"].As<float>();
                if (!std::isfinite(scale) || scale <= 0.0f)
                    return ReportSettingsError("imguiScale must be a positive finite value.");
                preferences.SetImGuiScale(scale);
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
                    return ReportSettingsError("build must be a map.");

				const Authoring::ReadNode buildRenderNode = buildNode["render"];
                if (buildRenderNode)
                {
                    if (!buildRenderNode.IsMap())
                        return ReportSettingsError("build.render must be a map.");

					const Authoring::ReadNode buildBackendNode = buildRenderNode["backend"];
                    if (buildBackendNode)
                    {
                        if (!buildBackendNode.IsScalar())
                            return ReportSettingsError(
                                "build.render.backend must be dx12 or vulkan.");
						const std::string backendName =
							buildBackendNode.AsString();
                        RenderBackend backend{};
                        if (!TryParseRenderBackend(backendName, backend))
                        {
                            return ReportSettingsError(
                                "Unsupported build.render.backend '" + backendName +
                                "' (expected dx12 or vulkan).");
                        }
                        buildSettings.SetRenderBackend(backend);
                    }
                }
            }
        }

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
		Meta::Typed::SerializeThunk<RenderPassSettings>(
			&renderPassSettings, renderPassDocument.Root());
		const Authoring::ReadNode serializedRenderPassSettings =
			renderPassDocument.Root().Read();
        const Authoring::WriteNode storedRenderPassSettings =
            root.Child("renderPassSettings");
        if (storedRenderPassSettings.Read().IsMap()
            && serializedRenderPassSettings.IsMap())
            OverlayKnownMap(storedRenderPassSettings, serializedRenderPassSettings);
        else
            storedRenderPassSettings.Assign(serializedRenderPassSettings);
        root.Child("m_contentsBrowserStyle").SetScalar(
            static_cast<int>(m_preferences.GetContentsBrowserStyle()));
        root.Child("startupSceneName").SetScalar(
            std::filesystem::path(
                m_buildSettings.GetStartupSceneName()).string());
        root.Child("imguiScale").SetScalar(m_preferences.GetImGuiScale());
        root.Child("render").Child("backend").SetScalar(
            RenderBackendName(m_preferences.GetRenderBackend()));
        root.Child("build").Child("render").Child("backend").SetScalar(
            RenderBackendName(m_buildSettings.GetRenderBackend()));
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
