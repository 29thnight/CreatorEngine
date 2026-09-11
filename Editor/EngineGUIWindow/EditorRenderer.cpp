#include "EditorRenderer.h"
#include "EditorWindowHost.h"
#include "EditorWindowRegistry.h"
#include "EditorChromeProbe.h"
#include "RHI/IImGuiHost.h"
#include "EditorFontResources.h"
#include "EditorTheme.h"
#include "EditorAssetPresentation.h"
#include "EditorSettingsStore.h"
#include "PathFinder.h"
#include "EditorWindowNames.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <stdexcept>
#include <string>

EditorRenderer::EditorRenderer(void* windowHandle, ::editor::window_table& windows)
    : m_windows(&windows)
{
    m_host = &GetImGuiHost();

    // 명시한 backend가 서지 않으면 부팅 실패다. 여기서 다른 backend를 만들거나
    // UI 없는 실행으로 계속 가면 설정 검증이 거짓 양성이 된다(Slice 8-c).
    std::string hostError;
    if (!m_host->Initialize(windowHandle, hostError))
    {
        m_host->Shutdown();
        throw std::runtime_error("Editor ImGui backend 초기화 실패: " + hostError);
    }

    // 1.92 의 아틀라스는 동적이라 `Build()` 를 부르지 않는다.
    AddEditorFonts();

    ApplyEditorScale(EditorSettingsStore::Get().Preferences().GetImGuiScale(),
                     m_host->GetWindowDpiScale());
}

EditorRenderer::~EditorRenderer()
{
    m_host->Shutdown();
}

void EditorRenderer::AddEditorFonts()
{
    // 폰트 자원은 `editor::fonts` 한 자리가 든다(PHASE 21 W1). 경로를 손으로
    // 적던 넷을 거기로 모았고, 파일이 없을 때 죽던 자리도 거기서 막는다.
    // 왜 죽었는지는 `EditorFontResources.h` 머리에 실측과 함께 적었다 —
    // 아이콘 폰트의 `MergeMode` 가 빈 폰트 목록의 끝을 읽었다.
    //
    // FA6 범위 단정도 그 파일로 옮겼다. 여기에 두면 `fa.h` 와
    // `IconsFontAwesome6.h` 를 이 TU 가 계속 들어야 한다.
    ::editor::fonts::add_required_font(
        "body", ::editor::fonts::body_candidates(), ::editor::EditorThemeTokens::BodyFontSize);
    ::editor::fonts::merge_icon_font(::editor::EditorThemeTokens::IconFontSize,
        ::editor::EditorThemeTokens::IconBaselineOffset);
}

void EditorRenderer::ApplyEditorScale(float userScale, float dpiScale)
{
    ::editor::ApplyEditorTheme(ImGui::GetStyle(), userScale, dpiScale);
    m_lastRequestedScale = userScale;
    m_lastDpiScale = dpiScale;
}

void EditorRenderer::BuildInitialDockLayout(unsigned int dockspaceId, float width, float height,
    float posX, float posY)
{
    // s&box 배치. 오른쪽 열이 전체 높이를 쓰고(Hierarchy 위 / Inspector 아래),
    // 남은 왼쪽을 아래로 갈라 자산 브라우저 계열을 넣고, 가운데는 뷰포트가
    // 탭으로 공유한다. 예전 배치는 왼쪽 절반을 Scene과 Game이 위아래로
    // 나눠 써서 뷰포트가 화면의 4분의 1이었다.
    //
    // 어느 창이 어느 자리에 가는지는 **선언이 답한다.** 여기 이름 목록을
    // 따로 적으면 그 목록과 `Begin` 이름이 갈릴 수 있고, 실제로 갈렸다 —
    // 공백 하나가 달라 Content Browser가 도크되지 않은 적이 있다. 표를 훑으면
    // 두 이름이 같은 출처라서 갈릴 자리 자체가 없다(부록 B.3의 "고아 0").
    ImGuiID id = dockspaceId;
    const ImVec2 size{ width, height };
    const ImVec2 nodePos{ posX, posY };

    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id);
    ImGui::DockBuilderSetNodeSize(id, size);
    ImGui::DockBuilderSetNodePos(id, nodePos);

    // 오른쪽 열을 먼저 떼어 전체 높이를 차지하게 한다. 아래 패널을 먼저
    // 가르면 오른쪽 열이 그 위에서 잘린다.
    ImGuiID rightColumn = ImGui::DockBuilderSplitNode(
        id, ImGuiDir_Right, 0.22f, nullptr, &id);
    const ImGuiID inspectorNode = ImGui::DockBuilderSplitNode(
        rightColumn, ImGuiDir_Down, 0.45f, nullptr, &rightColumn);
    const ImGuiID bottomPanel = ImGui::DockBuilderSplitNode(
        id, ImGuiDir_Down, 0.28f, nullptr, &id);
    const ImGuiID centerViewport = id;

    // 자리 이름을 노드로 옮기는 표. `dock_slot` 열거자 순서와 같은 순서이고,
    // 완전성은 static_assert가 지킨다 — 자리를 하나 더 만들면 여기가 컴파일에서
    // 걸린다.
    using ::editor::dock_slot;
    const ImGuiID slotNode[]
    {
        centerViewport,   // center
        rightColumn,      // right_upper
        inspectorNode,    // right_lower
        bottomPanel,      // bottom
        0,                // floating — 도크하지 않는다
    };
    static_assert(std::size(slotNode) == static_cast<std::size_t>(dock_slot::count),
        "dock_slot 열거자가 늘었는데 도크 빌더의 노드 표가 따라오지 않았다");

    for (const ::editor::window_entry& entry : m_windows->entries)
    {
        if (dock_slot::floating == entry.dock) continue;

        // 예전에는 여기 예외가 하나 있었다 — Tile 스타일의 Content Browser는
        // 팝업 드로어라 도크할 자리가 없었다. 스타일 분기를 걷으면서 예외도
        // 사라졌고, 이제 이 순회에 **자리 없는 창이 없다.**

        // stable_id는 전부 문자열 리터럴에서 왔으므로 data()가 널로 끝난다.
        ImGui::DockBuilderDockWindow(entry.stable_id.data(),
            slotNode[static_cast<std::size_t>(entry.dock)]);
    }

    ImGui::DockBuilderFinish(id);
}

void EditorRenderer::BeginRender()
{
    m_uiFrameBegan = std::chrono::steady_clock::now();
    // NewFrame가 현재 폰트 크기를 계산하므로 글자/geometry 배율을 먼저 적용한다.
    const float targetScale = EditorSettingsStore::Get().Preferences().GetImGuiScale();
    const float dpiScale = m_host->GetWindowDpiScale();
    if (m_lastRequestedScale != targetScale || m_lastDpiScale != dpiScale)
        ApplyEditorScale(targetScale, dpiScale);
    m_host->BeginFrame();
    // ── 메인 독스페이스 ──
    // 구 ImGuiRenderer에서는 #ifndef BUILD_FLAG 안이었다. 지금은 이 파일
    // 자체가 에디터 exe에만 링크되므로 조건이 필요 없다 — 매크로가 하던
    // 구분을 프로젝트 경계가 한다.
    //
    // 뷰포트의 작업 영역을 그대로 쓴다. 예전에는 전체 크기에서 메뉴 행
    // 높이를 둘 뺀 값을 쓰면서 위치는 GetWorkCenter에서 받아 두 좌표계를
    // 섞었고, 무엇보다 '행이 둘'이 코드에 박혀 있었다. 이제 제목표시줄과
    // 툴바와 상태 행이 셋이라 그 상수는 곧 어긋날 값이었다.
    // BeginViewportSideBar가 이미 작업 영역을 줄여 주므로 셀 필요가 없다.
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    ImGuiID id = ImGui::GetID("MainWindowGroup");
    const ImVec2 size = mainViewport->WorkSize;
    const ImVec2 nodePos = mainViewport->WorkPos;

    ImGui::SetNextWindowPos(nodePos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::Begin("Main DockSpace Window", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus);

    ImGui::DockSpace(id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::PopStyleVar(); // 반드시 Begin 이후에 Pop!

    ImGui::End();

    // 최초 실행(imgui.ini 없음)에만 기본 도크 레이아웃을 세운다.
    // ini에는 재생성 경로가 없었다 — 한 번 어긋나면 파일을 손으로 지우는
    // 것이 유일한 복구여서 Window > Reset Layout이 같은 빌더를 다시
    // 부를 수 있게 열어 뒀다.
    const bool resetRequested =
        s_dockLayoutResetRequested.exchange(false, std::memory_order_acq_rel);
    if (m_firstLoop || resetRequested)
    {
        file::path iniPath = PathFinder::ConfigPath("imgui.ini");
        if (resetRequested || !file::exists(iniPath))
        {
            BuildInitialDockLayout(id, size.x, size.y, nodePos.x, nodePos.y);
        }
        m_firstLoop = false;
    }
}

std::atomic_bool EditorRenderer::s_dockLayoutResetRequested{ false };

void EditorRenderer::RequestDockLayoutReset() noexcept
{
    s_dockLayoutResetRequested.store(true, std::memory_order_release);
}

void EditorRenderer::Render()
{
    EditorAssetPresentation::Get().OpenPendingTextureImportSelector();

    // PHASE 21 M4(부록 B.3): 창 프레임을 여는 곳은 여기 하나다.
    //
    // 여기 있던 옛 펌프 — `ImGuiRegister`의 `unordered_map`을 훑어 항목마다
    // `Render()`를 부르던 네 줄 — 는 4단계에서 걷었다. 마지막까지 그것이
    // 그리던 창이 0개가 된 뒤였고, 그 사실은 표에 스물다섯이 다 실린 것과
    // 짝을 이룬다. 순회 순서가 `unordered_map`이라 비결정적이던 것도 함께 갔다.
    //
    // 자리가 BeginRender 뒤인 이유는 그대로다 — 도크스페이스가 이미 서 있어야
    // 창이 자기 도크 노드를 찾는다.
    ::editor::draw_windows(*m_windows);
}

void EditorRenderer::EndRender()
{
    // PHASE 21 W0 전반(계획서 §1.9): 밖에서 배치를 볼 수단.
    //
    // 자리가 `EndFrame` **앞**인 이유는 둘이다 — 이번 프레임의 모든
    // `Begin`/`End` 가 끝나 도크 노드 rect 와 창의 도크 소속이 확정돼 있고,
    // 아직 ImGui 문맥이 살아 있어 읽을 수 있다.
    //
    // 여기서 게시하고 CLI 가 사본을 읽는다. 명령 핸들러는 게임 스레드의
    // `Pump()` 에서 도는데(`App.cpp`) ImGui 프레임은 이 스레드(Presentation)
    // 것이라, 명령이 `ImGui::` 를 직접 부르면 `NewFrame`~`Render` 한복판의
    // 전역 문맥을 읽는 경합이 된다.
    const bool captured = ::editor::capture_chrome_snapshot();

    const std::chrono::steady_clock::time_point uiFrameEnded =
        std::chrono::steady_clock::now();

    m_host->EndFrame();

    // draw data 는 `EndFrame` 안의 `ImGui::Render()` 뒤라야 유효하다
    // (`ImDrawData::Valid`). 그래서 배치·스타일과 자리가 다르고, 뜬 프레임에만
    // 얹어 값 넷이 **같은 프레임**의 것이 되게 한다.
    if (captured)
    {
        const double uiCpuMs =
            std::chrono::duration<double, std::milli>(uiFrameEnded - m_uiFrameBegan).count();
        ::editor::capture_chrome_draw_totals(uiCpuMs);
    }
}
