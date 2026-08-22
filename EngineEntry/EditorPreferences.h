#pragma once

#include "RenderBackend.h"

enum class ContentsBrowserStyle
{
    Tile,
    Tree,
};

struct EditorPreferences
{
    ContentsBrowserStyle GetContentsBrowserStyle() const noexcept
    {
        return contentsBrowserStyle;
    }
    void SetContentsBrowserStyle(ContentsBrowserStyle value) noexcept
    {
        contentsBrowserStyle = value;
    }

    float GetImGuiScale() const noexcept { return imguiScale; }
    void SetImGuiScale(float value) noexcept { imguiScale = value; }

    RenderBackend GetRenderBackend() const noexcept { return renderBackend; }
    void SetRenderBackend(RenderBackend value) noexcept { renderBackend = value; }
    bool IsRenderBackendRestartRequired(RenderBackend activeBackend) const noexcept
    {
        return renderBackend != activeBackend;
    }

private:
    ContentsBrowserStyle contentsBrowserStyle{ ContentsBrowserStyle::Tile };
    float imguiScale{ 0.8f };
    RenderBackend renderBackend{ RenderBackend::DX12 };
};
