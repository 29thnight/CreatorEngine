#pragma once

#include "RenderBackend.h"
#include "RenderPassSettings.h"

#include <mutex>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

struct AnimationBudgetSettings final
{
    // S1's 100-actor Walk baseline is about 1.2 ms of aggregate pose work.
    // Leave room for richer recipes and occasional worker scheduling stalls.
    double cpuBudgetMs{ 4.0 };
    std::uint16_t promotionGraceFrames{ 8 };
    double hysteresis{ .05 };
};

// Core-owned settings for the running process. Editor preferences and product-build
// choices are deliberately absent; the packaging pipeline projects those choices into
// the same runtime schema consumed by Player.
class RuntimeSettings final
{
public:
    static bool Initialize() noexcept;
    static void Shutdown() noexcept;
    static RuntimeSettings& Get() noexcept;
    static RuntimeSettings* TryGet() noexcept;

    RuntimeSettings(const RuntimeSettings&) = delete;
    RuntimeSettings& operator=(const RuntimeSettings&) = delete;

    RenderBackend GetRenderBackend() const noexcept { return m_renderBackend; }
    const std::wstring& GetStartupSceneName() const noexcept { return m_startupSceneName; }
    AnimationBudgetSettings GetAnimationBudgetSettings() const noexcept
    {
        const std::scoped_lock lock(m_animationBudgetMutex);
        return m_animationBudgetSettings;
    }
    void SetAnimationBudgetSettings(AnimationBudgetSettings settings) noexcept
    {
        if (!std::isfinite(settings.cpuBudgetMs) || settings.cpuBudgetMs <= 0.
            || !std::isfinite(settings.hysteresis) || settings.hysteresis < 0.
            || settings.hysteresis >= .5) return;
        const std::scoped_lock lock(m_animationBudgetMutex);
        m_animationBudgetSettings = settings;
    }

    void SetRenderPassSettings(const RenderPassSettings& settings)
    {
        const std::scoped_lock lock(m_renderPassSettingsMutex);
        m_renderPassSettings = settings;
    }
    RenderPassSettings GetRenderPassSettings() const
    {
        const std::scoped_lock lock(m_renderPassSettingsMutex);
        return m_renderPassSettings;
    }
    void SetSkyboxTextureName(std::string value)
    {
        const std::scoped_lock lock(m_renderPassSettingsMutex);
        m_renderPassSettings.skyboxTextureName = std::move(value);
    }

private:
    RuntimeSettings() = default;
    bool Load() noexcept;

    RenderBackend m_renderBackend{ RenderBackend::DX12 };
    mutable std::mutex m_renderPassSettingsMutex;
    RenderPassSettings m_renderPassSettings{};
    std::wstring m_startupSceneName{ L"SampleScene" };
    AnimationBudgetSettings m_animationBudgetSettings{};
    mutable std::mutex m_animationBudgetMutex;
};
