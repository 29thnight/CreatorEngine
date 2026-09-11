#pragma once

// 그릴 창의 표를 생성자로 받으므로 타입을 안다(PHASE 21 W3).
#include "EditorWindowRegistry.h"

#include <atomic>
#include <chrono>

class IImGuiHost;

// 에디터 ImGui 오케스트레이터 (구 ImGuiRenderer 재작성, 2026-08-10).
//
// ── 계보 ──
//
// RenderEngine/ImGuiRenderer의 후계다. 그 클래스는 백엔드 가동과 에디터
// 오케스트레이션을 겸직했고, 겸직의 값은 Player가 치렀다 — 표시 경로가
// 필요할 뿐인데 에디터 독스페이스 빌더와 창 펌프까지 매 프레임 돌았다.
//
// 재작성에서 백엔드 몫은 IImGuiHost 경계 뒤(RHI/ImGuiHost —
// DX12DeviceResources 위에서 돈다)로 가라앉았고, 여기 남은 것은 에디터
// 몫뿐이다:
//   · 에디터 폰트(Verdana + FontAwesome 아이콘)와 스타일
//   · UI 스케일 추적(EditorPreferences) — 재빌드는 경계의 RebuildFontAtlas로
//   · 메인 독스페이스와 최초 도크 레이아웃(imgui.ini가 없을 때)
//   · 선언된 창의 프레임(editor::draw_declared_windows) — 옛 ImGuiRegister
//     펌프가 있던 자리다(M4 4단계에서 걷었다)
//
// 소속도 계보와 다르다: RenderEngine이 아니라 Academy_4Q(Editor 필터)다.
// 에디터 코드는 에디터 exe에 산다 — Player는 이 파일을 링크하지 않는다.
class EditorRenderer
{
public:
    /// windowHandle은 Win32 HWND. 호스트 가동 + 에디터 폰트·스타일까지 세운다.
    ///
    /// 그릴 창의 표를 **인자로 받는다**(PHASE 21 W3). 전에는 프레임 안에서
    /// 전역 접근자를 직접 집었고, 그래서 무엇을 그리는지가 서명에 없었다.
    /// 표를 받으면 다른 표 위에서 돌려 보는 것도 가능해진다.
    EditorRenderer(void* windowHandle, ::editor::window_table& windows);
    ~EditorRenderer();

    void BeginRender();
    void Render();
    void EndRender();

    /// 다음 BeginRender가 기본 도크 레이아웃을 다시 세운다. imgui.ini에는
    /// 재생성 경로가 없어서 한 번 어긋난 배치를 되돌릴 방법이 없었다.
    /// 어느 스레드에서 불러도 된다 — 적용은 표시 스레드가 한다.
    static void RequestDockLayoutReset() noexcept;

private:
    void AddEditorFonts();
    void ApplyEditorScale(float newScale, bool rebuildFonts);
    void BuildInitialDockLayout(unsigned int dockspaceId, float width, float height,
        float posX, float posY);

    IImGuiHost* m_host{ nullptr };
    ::editor::window_table* m_windows{ nullptr };
    float m_lastAppliedScale{ 0.8f };
    float m_lastRequestedScale{ -1.f };

    // 에디터 UI 한 프레임의 CPU 시간을 재는 시작점(PHASE 21 W0 후반).
    // `BeginRender` 에서 찍고 `EndRender` 에서 뺀다 — GPU 제출과 Present 는
    // `EndFrame` 안이라 빠진다.
    std::chrono::steady_clock::time_point m_uiFrameBegan{};
    bool m_firstLoop{ true };

    static std::atomic_bool s_dockLayoutResetRequested;
};
