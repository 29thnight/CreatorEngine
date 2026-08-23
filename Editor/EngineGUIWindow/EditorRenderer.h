#pragma once

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
//   · ImGuiRegister 창 펌프
//
// 소속도 계보와 다르다: RenderEngine이 아니라 Academy_4Q(Editor 필터)다.
// 에디터 코드는 에디터 exe에 산다 — Player는 이 파일을 링크하지 않는다.
class EditorRenderer
{
public:
    /// windowHandle은 Win32 HWND. 호스트 가동 + 에디터 폰트·스타일까지 세운다.
    explicit EditorRenderer(void* windowHandle);
    ~EditorRenderer();

    void BeginRender();
    void Render();
    void EndRender();

private:
    void AddEditorFonts();
    void ApplyEditorScale(float newScale, bool rebuildFonts);
    void BuildInitialDockLayout(unsigned int dockspaceId, float width, float height,
        float posX, float posY);

    IImGuiHost* m_host{ nullptr };
    float m_lastAppliedScale{ 0.8f };
    float m_lastRequestedScale{ -1.f };
    bool m_firstLoop{ true };
};
