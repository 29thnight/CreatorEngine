#pragma once

#include <cstdint>

// 편집 중 렌더 속도. 실행 중 게임과 배경 절전은 이 값 밖에서 정한다(App::ResolveLivePacing).
enum class EditorFrameRateMode : std::uint8_t
{
    Display,    // 모니터 주사율
    Unlimited,  // 성능 측정용
    Custom,     // customFrameRate
};

// Content Browser 표시 스타일(Tile/Tree)이 여기 있었다. 두 스타일은 본문
// 배치만이 아니라 **창의 성격까지** 갈랐다 — Tile 은 닫히는 하단 서랍,
// Tree 는 도킹된 패널이었고, 그 갈림이 도크 빌더·스냅샷 감사·메뉴 세 곳에
// 예외 분기를 심고 있었다. 하나로 접었다(도킹 패널 + 좌측 트리 + 자산 격자).
struct EditorPreferences
{
    static constexpr std::uint32_t kMinCustomFrameRate = 10;
    static constexpr std::uint32_t kMaxCustomFrameRate = 1000;
    static constexpr std::uint32_t kMaxBackgroundFrameRate = 60;

    EditorFrameRateMode GetFrameRateMode() const noexcept { return frameRateMode; }
    void SetFrameRateMode(EditorFrameRateMode value) noexcept { frameRateMode = value; }
    std::uint32_t GetCustomFrameRate() const noexcept { return customFrameRate; }
    void SetCustomFrameRate(std::uint32_t value) noexcept { customFrameRate = value; }
    // 0 이면 창이 뒤로 가도 낮추지 않는다.
    std::uint32_t GetBackgroundFrameRate() const noexcept { return backgroundFrameRate; }
    void SetBackgroundFrameRate(std::uint32_t value) noexcept { backgroundFrameRate = value; }

    float GetImGuiScale() const noexcept { return imguiScale; }
    void SetImGuiScale(float value) noexcept { imguiScale = value; }
    float GetContentTreeWidth() const noexcept { return contentTreeWidth; }
    void SetContentTreeWidth(float value) noexcept { contentTreeWidth = value; }

// 에디터 렌더 백엔드는 여기 없다. 에디터 호스트는 DX12 고정이고, 사람이 고르는
// 백엔드는 BuildSettings::renderBackend(=build.render.backend) 하나뿐이다.
private:
    float imguiScale{ 0.8f };
    float contentTreeWidth{ 220.f }; // logical pixels; resize clamping is transient
    EditorFrameRateMode frameRateMode{ EditorFrameRateMode::Display };
    std::uint32_t customFrameRate{ 120 };
    // 고도 에디터의 배경 기본값(초당 10)과 같다. 언리얼은 3.
    std::uint32_t backgroundFrameRate{ 10 };
};
