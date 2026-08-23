#pragma once

#include "RenderBackend.h"
#include "RenderPassSettings.h"

#include <mutex>
#include <string>
#include <utility>

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
};
