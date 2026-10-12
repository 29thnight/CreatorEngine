#include "RuntimeSettings.h"

#include "LogSystem.h"
#include "PathFinder.h"
#include "ReflectionTypedYml.h"
#include "AuthoringParsedDocument.h"
#include "TemporalProductSettingsIO.h"

#include <cstdio>
#include <cmath>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace
{
    std::unique_ptr<RuntimeSettings> g_runtimeSettings;
}

bool RuntimeSettings::Initialize(RuntimeRenderBackendPolicy backendPolicy) noexcept
{
    if (g_runtimeSettings)
    {
        return true;
    }

    std::unique_ptr<RuntimeSettings> candidate(new RuntimeSettings(backendPolicy));
    if (!candidate->Load())
    {
        return false;
    }
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
                Debug::PrintLog(spdlog::level::err, "Packaged EngineSettings.asset is missing.");
                return false;
            }

            Debug::PrintLog(spdlog::level::warn, "EngineSettings.asset is missing; using runtime defaults until the Editor saves it.");
            return true;
        }

        std::string parseError;
        const Authoring::ParsedDocument document =
            Authoring::ParsedDocument::ParseFile(settingsPath.string(), parseError);
        if (!document)
        {
            Debug::PrintLog(spdlog::level::err, "Unable to load EngineSettings.asset: " + parseError);
            return false;
        }
        const Authoring::ReadNode root = document.Root();
        RenderPassSettings renderPassSettings = m_renderPassSettings;
        if (root["renderPassSettings"])
        {
            Meta::Typed::DeserializeObjectFrom(renderPassSettings, root["renderPassSettings"]);
        }
        const auto featureNode = root[m_backendPolicy == RuntimeRenderBackendPolicy::FixedDX12
            ? "editorRenderFeatures" : "renderFeatures"];
        TemporalProductSettings temporalProductSettings;
        temporalProductSettings.fallbackAa = renderPassSettings.aa.isApply;
        if (!TemporalProductSettingsIO::Read(featureNode, temporalProductSettings))
        {
            Debug::PrintLog(spdlog::level::err, "Invalid or unsupported render feature settings schema.");
            return false;
        }
        if (featureNode)
        {
            renderPassSettings.aa.isApply = temporalProductSettings.fallbackAa;
        }

        RenderBackend renderBackend = RenderBackend::DX12;
        // Editor의 고정 정책에서는 render 맵과 레거시 선택자에 접근하지 않는다.
        // Player만 패키지에 투영된 백엔드 설정을 읽고 검증한다.
        if (m_backendPolicy == RuntimeRenderBackendPolicy::ProjectSettings)
        {
            bool hasCanonicalBackend = false;
            const Authoring::ReadNode renderNode = root["render"];
            if (renderNode)
            {
                if (!renderNode.IsMap())
                {
                    Debug::PrintLog(spdlog::level::err, "EngineSettings render must be a map.");
                    return false;
                }

                const Authoring::ReadNode backendNode = renderNode["backend"];
                if (backendNode)
                {
                    if (!backendNode.IsScalar())
                    {
                        Debug::PrintLog(spdlog::level::err, "EngineSettings render.backend must be dx12 or vulkan.");
                        return false;
                    }

                    const std::string backendName = backendNode.AsString();
                    if (!TryParseRenderBackend(backendName, renderBackend))
                    {
                        const std::string message = "Unsupported EngineSettings render.backend '" +
                            backendName + "' (expected dx12 or vulkan).";
                        std::fprintf(stderr, "[RenderBackend] %s\n", message.c_str());
                        Debug::PrintLog(spdlog::level::err, message);
                        return false;
                    }
                    hasCanonicalBackend = true;
                }
            }
            if (!hasCanonicalBackend)
            {
                bool legacyDx12 = true;
                if (root["renderBackendDx12"])
                {
                    legacyDx12 = root["renderBackendDx12"].As<bool>();
                }
                if (root["imguiBackendDx12"])
                {
                    legacyDx12 = legacyDx12 && root["imguiBackendDx12"].As<bool>();
                }
                renderBackend = legacyDx12 ? RenderBackend::DX12 : RenderBackend::Vulkan;
            }
        }

        const Authoring::ReadNode startupSceneNode = root["startupSceneName"];
        const std::filesystem::path startupScene = startupSceneNode
            ? startupSceneNode.AsString() : "SampleScene";

        AnimationBudgetSettings animationBudget{};
        const Authoring::ReadNode animationNode = root["animation"];
        if (animationNode)
        {
            if (!animationNode.IsMap())
            {
                Debug::PrintLog(spdlog::level::err, "EngineSettings animation must be a map.");
                return false;
            }
            if (const auto value = animationNode["cpuBudgetMs"])
                animationBudget.cpuBudgetMs = value.As<double>();
            if (const auto value = animationNode["promotionGraceFrames"])
                animationBudget.promotionGraceFrames = value.As<std::uint16_t>();
            if (const auto value = animationNode["hysteresis"])
                animationBudget.hysteresis = value.As<double>();
            if (!std::isfinite(animationBudget.cpuBudgetMs)
                || animationBudget.cpuBudgetMs <= 0.
                || !std::isfinite(animationBudget.hysteresis)
                || animationBudget.hysteresis < 0. || animationBudget.hysteresis >= .5)
            {
                Debug::PrintLog(spdlog::level::err, "Invalid EngineSettings animation budget.");
                return false;
            }
        }

        m_renderPassSettings = std::move(renderPassSettings);
        m_temporalProductSettings = std::move(temporalProductSettings);
        m_renderBackend = renderBackend;
        m_startupSceneName = startupScene.wstring();
        m_animationBudgetSettings = animationBudget;

        Debug::PrintLog(spdlog::level::debug, std::string("[RenderBackend] active=") +
            RenderBackendName(m_renderBackend) + " policy=" +
            (m_backendPolicy == RuntimeRenderBackendPolicy::FixedDX12 ? "host-fixed-dx12" : "project-settings"));
        return true;
    }
    catch (const std::exception& exception)
    {
        const std::string message = "Runtime settings initialization failed: " +
            std::string(exception.what());
        std::fprintf(stderr, "[RuntimeSettings] %s\n", message.c_str());
        Debug::PrintLog(spdlog::level::err, message);
        return false;
    }
    catch (...)
    {
        std::fputs("[RuntimeSettings] unknown initialization failure\n", stderr);
        Debug::PrintLog(spdlog::level::err, "Runtime settings initialization failed with an unknown error.");
        return false;
    }
}
