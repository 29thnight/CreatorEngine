#pragma once

// Content Browser 표시 스타일(Tile/Tree)이 여기 있었다. 두 스타일은 본문
// 배치만이 아니라 **창의 성격까지** 갈랐다 — Tile 은 닫히는 하단 서랍,
// Tree 는 도킹된 패널이었고, 그 갈림이 도크 빌더·스냅샷 감사·메뉴 세 곳에
// 예외 분기를 심고 있었다. 하나로 접었다(도킹 패널 + 좌측 트리 + 자산 격자).
struct EditorPreferences
{
    float GetImGuiScale() const noexcept { return imguiScale; }
    void SetImGuiScale(float value) noexcept { imguiScale = value; }

// 에디터 렌더 백엔드는 여기 없다. 에디터 호스트는 DX12 고정이고, 사람이 고르는
// 백엔드는 BuildSettings::renderBackend(=build.render.backend) 하나뿐이다.
private:
    float imguiScale{ 0.8f };
};
