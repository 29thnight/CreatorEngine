#pragma once

#include "RenderBackend.h"

#include <string>
#include <utility>

struct BuildSettings
{
    const std::wstring& GetStartupSceneName() const noexcept { return startupSceneName; }
    void SetStartupSceneName(std::wstring value) { startupSceneName = std::move(value); }

    RenderBackend GetRenderBackend() const noexcept { return renderBackend; }
    void SetRenderBackend(RenderBackend value) noexcept { renderBackend = value; }

private:
    std::wstring startupSceneName{ L"SampleScene" };
    RenderBackend renderBackend{ RenderBackend::DX12 };
};
