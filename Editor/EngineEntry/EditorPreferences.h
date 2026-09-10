#pragma once

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

// 에디터 렌더 백엔드는 여기 없다. 에디터 호스트는 DX12 고정이고, 사람이 고르는
// 백엔드는 BuildSettings::renderBackend(=build.render.backend) 하나뿐이다.
private:
    ContentsBrowserStyle contentsBrowserStyle{ ContentsBrowserStyle::Tile };
    float imguiScale{ 0.8f };
};
