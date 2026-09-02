#include "RuntimeSettings.h"

#include "LogSystem.h"
#include "PathFinder.h"
#include "ReflectionTypedYml.h"
#include "AuthoringParsedDocument.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace
{
    std::unique_ptr<RuntimeSettings> g_runtimeSettings;
}

bool RuntimeSettings::Initialize() noexcept
{
    if (g_runtimeSettings) return true;

    std::unique_ptr<RuntimeSettings> candidate(new RuntimeSettings());
    if (!candidate->Load()) return false;
    g_runtimeSettings = std::move(candidate);
    return true;
}

void RuntimeSettings::Shutdown() noexcept
{
    g_runtimeSettings.reset();
}

RuntimeSettings& RuntimeSettings::Get() noexcept
{
    if (!g_runtimeSettings) std::terminate();
    return *g_runtimeSettings;
}

RuntimeSettings* RuntimeSettings::TryGet() noexcept
{
    return g_runtimeSettings.get();
}

bool RuntimeSettings::Load() noexcept
{
    const std::filesystem::path settingsPath =
        PathFinder::ProjectSettingPath("EngineSettings.asset");

    try
    {
        if (!std::filesystem::exists(settingsPath))
        {
            if (!PathFinder::IsAssetAuthoringEnabled())
            {
                Debug->LogError("Packaged EngineSettings.asset is missing.");
                return false;
            }

            Debug->LogWarning("EngineSettings.asset is missing; using runtime defaults until the Editor saves it.");
            return true;
        }

        std::string parseError;
        const Authoring::ParsedDocument document =
            Authoring::ParsedDocument::ParseFile(settingsPath.string(), parseError);
        if (!document)
        {
            Debug->LogError("Unable to load EngineSettings.asset: " + parseError);
            return false;
        }
        const Authoring::ReadNode root = document.Root();
        RenderPassSettings renderPassSettings = m_renderPassSettings;
        if (root["renderPassSettings"])
        {
            Meta::Typed::DeserializeThunk<RenderPassSettings>(
                &renderPassSettings, root["renderPassSettings"]);
        }

        RenderBackend renderBackend = RenderBackend::DX12;
        bool hasCanonicalBackend = false;
        const Authoring::ReadNode renderNode = root["render"];
        if (renderNode)
        {
            if (!renderNode.IsMap())
            {
                Debug->LogError("EngineSettings render must be a map.");
                return false;
            }

            const Authoring::ReadNode backendNode = renderNode["backend"];
            if (backendNode)
            {
                if (!backendNode.IsScalar())
                {
                    Debug->LogError("EngineSettings render.backend must be dx12 or vulkan.");
                    return false;
                }

                const std::string backendName = backendNode.AsString();
                if (!TryParseRenderBackend(backendName, renderBackend))
                {
                    const std::string message = "Unsupported EngineSettings render.backend '" +
                        backendName + "' (expected dx12 or vulkan).";
                    std::fprintf(stderr, "[RenderBackend] %s\n", message.c_str());
                    Debug->LogError(message);
                    return false;
                }
                hasCanonicalBackend = true;
            }
        }
        if (!hasCanonicalBackend)
        {
            bool legacyDx12 = true;
            if (root["renderBackendDx12"])
                legacyDx12 = root["renderBackendDx12"].As<bool>();
            if (root["imguiBackendDx12"])
                legacyDx12 = legacyDx12 && root["imguiBackendDx12"].As<bool>();
            renderBackend = legacyDx12 ? RenderBackend::DX12 : RenderBackend::Vulkan;
        }

        const Authoring::ReadNode startupSceneNode = root["startupSceneName"];
        const std::filesystem::path startupScene = startupSceneNode
            ? startupSceneNode.AsString() : "SampleScene";

        m_renderPassSettings = std::move(renderPassSettings);
        m_renderBackend = renderBackend;
        m_startupSceneName = startupScene.wstring();

        Debug->LogDebug(std::string("[RenderBackend] runtime=") +
            RenderBackendName(m_renderBackend));
        return true;
    }
    catch (const std::exception& exception)
    {
        const std::string message = "Runtime settings initialization failed: " +
            std::string(exception.what());
        std::fprintf(stderr, "[RuntimeSettings] %s\n", message.c_str());
        Debug->LogError(message);
        return false;
    }
    catch (...)
    {
        std::fputs("[RuntimeSettings] unknown initialization failure\n", stderr);
        Debug->LogError("Runtime settings initialization failed with an unknown error.");
        return false;
    }
}
