#include "EditorRenderer.h"
#include "RHI/IImGuiHost.h"
#include "ImGUiRegisterClass.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "EditorAssetPresentation.h"
#include "EditorSettingsStore.h"
#include "PathFinder.h"
#include "EditorWindowNames.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <stdexcept>
#include <string>

namespace
{
    // 에디터 위젯 스타일. 구 ImGuiRenderer의 ImGuiBootstrap::ApplyStyle 그대로다.
    // s&box 에디터 계열 다크 스킨. 이전 스킨은 둥근 모서리 5px에 항목 간격
    // 12x8이라 패널 하나에 담기는 행이 적었고, 창 배경(0.22)이 프레임
    // 배경(0.157)보다 밝아 입력란이 배경에 잠겼다. 여기서는 면을 어둡게
    // 깔고 강조를 파랑 하나로 몰아 계층을 밝기가 아니라 색으로 만든다.
    //
    // 팔레트를 상수로 뽑지 않고 각 항목에 직접 적는다 — 색 하나가 여러
    // 역할을 겸하는 순간 한쪽을 고치려다 다른 쪽이 따라 바뀐다.
    void ApplyEditorStyle(ImGuiStyle* _style)
    {
        _style->WindowPadding = ImVec2(8, 6);
        _style->WindowRounding = 0.0f;
        _style->WindowBorderSize = 1.0f;
        _style->WindowTitleAlign = ImVec2(0.0f, 0.5f);
        // 창 제목 왼쪽에 붙던 접기 화살표를 없앤다. 도킹 셸에서는 누를 일이
        // 없는데 제목 문자열을 밀어내기만 했다.
        _style->WindowMenuButtonPosition = ImGuiDir_None;
        _style->ChildRounding = 3.0f;
        _style->PopupRounding = 4.0f;
        _style->PopupBorderSize = 1.0f;
        _style->FramePadding = ImVec2(7, 5);
        _style->FrameRounding = 3.0f;
        _style->FrameBorderSize = 0.0f;
        _style->ItemSpacing = ImVec2(8, 5);
        _style->ItemInnerSpacing = ImVec2(6, 4);
        _style->CellPadding = ImVec2(6, 3);
        _style->IndentSpacing = 18.0f;
        _style->ScrollbarSize = 12.0f;
        _style->ScrollbarRounding = 6.0f;
        _style->GrabMinSize = 8.0f;
        _style->GrabRounding = 3.0f;
        _style->TabRounding = 3.0f;
        _style->TabBarBorderSize = 1.0f;
        _style->TabBarOverlineSize = 2.0f;
        _style->SeparatorTextBorderSize = 1.0f;
        _style->DockingSeparatorSize = 2.0f;

        _style->Colors[ImGuiCol_Text] = ImVec4(0.839f, 0.839f, 0.851f, 1.00f);
        _style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.439f, 0.439f, 0.459f, 1.00f);
        _style->Colors[ImGuiCol_WindowBg] = ImVec4(0.141f, 0.141f, 0.149f, 1.00f);
        _style->Colors[ImGuiCol_ChildBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        _style->Colors[ImGuiCol_PopupBg] = ImVec4(0.106f, 0.106f, 0.114f, 0.98f);
        _style->Colors[ImGuiCol_Border] = ImVec4(0.239f, 0.239f, 0.259f, 1.00f);
        _style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        _style->Colors[ImGuiCol_FrameBg] = ImVec4(0.098f, 0.098f, 0.106f, 1.00f);
        _style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.180f, 0.180f, 0.196f, 1.00f);
        _style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.220f, 0.220f, 0.239f, 1.00f);
        _style->Colors[ImGuiCol_TitleBg] = ImVec4(0.086f, 0.086f, 0.094f, 1.00f);
        _style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.106f, 0.106f, 0.114f, 1.00f);
        _style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.086f, 0.086f, 0.094f, 1.00f);
        _style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.086f, 0.086f, 0.094f, 1.00f);
        _style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.086f, 0.086f, 0.094f, 0.00f);
        _style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.278f, 0.278f, 0.298f, 1.00f);
        _style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.353f, 0.353f, 0.376f, 1.00f);
        _style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.427f, 0.427f, 0.451f, 1.00f);
        _style->Colors[ImGuiCol_CheckMark] = ImVec4(0.290f, 0.522f, 0.910f, 1.00f);
        _style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.290f, 0.522f, 0.910f, 1.00f);
        _style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.361f, 0.588f, 0.945f, 1.00f);
        _style->Colors[ImGuiCol_Button] = ImVec4(0.180f, 0.180f, 0.196f, 1.00f);
        _style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.239f, 0.239f, 0.259f, 1.00f);
        _style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.290f, 0.522f, 0.910f, 1.00f);
        _style->Colors[ImGuiCol_Header] = ImVec4(0.180f, 0.180f, 0.196f, 1.00f);
        _style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.239f, 0.239f, 0.259f, 1.00f);
        _style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.290f, 0.522f, 0.910f, 0.85f);
        _style->Colors[ImGuiCol_Separator] = ImVec4(0.212f, 0.212f, 0.231f, 1.00f);
        _style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.290f, 0.522f, 0.910f, 0.78f);
        _style->Colors[ImGuiCol_SeparatorActive] = ImVec4(0.290f, 0.522f, 0.910f, 1.00f);
        _style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        _style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.290f, 0.522f, 0.910f, 0.55f);
        _style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.290f, 0.522f, 0.910f, 0.90f);
        _style->Colors[ImGuiCol_Tab] = ImVec4(0.118f, 0.118f, 0.126f, 1.00f);
        _style->Colors[ImGuiCol_TabHovered] = ImVec4(0.220f, 0.220f, 0.239f, 1.00f);
        _style->Colors[ImGuiCol_TabSelected] = ImVec4(0.180f, 0.180f, 0.196f, 1.00f);
        _style->Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.290f, 0.522f, 0.910f, 1.00f);
        _style->Colors[ImGuiCol_TabDimmed] = ImVec4(0.098f, 0.098f, 0.106f, 1.00f);
        _style->Colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.141f, 0.141f, 0.149f, 1.00f);
        _style->Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        _style->Colors[ImGuiCol_DockingPreview] = ImVec4(0.290f, 0.522f, 0.910f, 0.45f);
        _style->Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.086f, 0.086f, 0.094f, 1.00f);
        _style->Colors[ImGuiCol_PlotLines] = ImVec4(0.600f, 0.600f, 0.620f, 1.00f);
        _style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.290f, 0.522f, 0.910f, 1.00f);
        _style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.290f, 0.522f, 0.910f, 0.85f);
        _style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.361f, 0.588f, 0.945f, 1.00f);
        _style->Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.141f, 0.141f, 0.149f, 1.00f);
        _style->Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.212f, 0.212f, 0.231f, 1.00f);
        _style->Colors[ImGuiCol_TableBorderLight] = ImVec4(0.169f, 0.169f, 0.184f, 1.00f);
        _style->Colors[ImGuiCol_TableRowBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        _style->Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.000f, 1.000f, 1.000f, 0.025f);
        _style->Colors[ImGuiCol_TextLink] = ImVec4(0.361f, 0.588f, 0.945f, 1.00f);
        _style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.290f, 0.522f, 0.910f, 0.35f);
        _style->Colors[ImGuiCol_DragDropTarget] = ImVec4(0.290f, 0.522f, 0.910f, 0.90f);
        _style->Colors[ImGuiCol_NavCursor] = ImVec4(0.290f, 0.522f, 0.910f, 1.00f);
        _style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);
    }
}

EditorRenderer::EditorRenderer(void* windowHandle)
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

    AddEditorFonts();
    ImGui::GetIO().Fonts->Build();

    m_lastAppliedScale = EditorSettingsStore::Get().Preferences().GetImGuiScale();
    ImGui::GetIO().FontGlobalScale = m_lastAppliedScale;

    ImGuiStyle* style = &ImGui::GetStyle();
    ApplyEditorStyle(style);
    style->ScaleAllSizes(m_lastAppliedScale);
}

EditorRenderer::~EditorRenderer()
{
    m_host->Shutdown();
}

void EditorRenderer::AddEditorFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true; // 아이콘 폰트를 본문 폰트에 병합
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 16.0f);
    io.Fonts->AddFontFromMemoryCompressedTTF(
        FA_compressed_data, FA_compressed_size, 16.0f, &icons_config, icons_ranges);
}

void EditorRenderer::ApplyEditorScale(float newScale, bool rebuildFonts)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    // 스타일을 기준값에서 다시 세운 뒤 스케일한다(누적 방지).
    ApplyEditorStyle(&style);
    style.ScaleAllSizes(newScale);

    io.FontGlobalScale = newScale;
    m_lastAppliedScale = newScale;

    if (rebuildFonts)
    {
        io.Fonts->Clear();
        AddEditorFonts();
        io.Fonts->Build();
        // 폰트 텍스처는 백엔드 소유물이라 재생성은 경계 너머의 일이다.
        m_host->RebuildFontAtlas();
    }
}

void EditorRenderer::BuildInitialDockLayout(unsigned int dockspaceId, float width, float height,
    float posX, float posY)
{
    // s&box 배치. 오른쪽 열이 전체 높이를 쓰고(Hierarchy 위 / Inspector 아래),
    // 남은 왼쪽을 아래로 갈라 자산 브라우저 계열을 넣고, 가운데는 뷰포트가
    // 탭으로 공유한다. 예전 배치는 왼쪽 절반을 Scene과 Game이 위아래로
    // 나눠 써서 뷰포트가 화면의 4분의 1이었다.
    //
    // 이름 리터럴을 직접 적지 않는다 — EditorWindowName이 정본이다.
    // 여기서 공백 하나가 달라 Content Browser가 도크되지 않았다.
    ImGuiID id = dockspaceId;
    const ImVec2 size{ width, height };
    const ImVec2 nodePos{ posX, posY };

    const bool contentBrowserIsDrawer =
        ContentsBrowserStyle::Tile ==
        EditorSettingsStore::Get().Preferences().GetContentsBrowserStyle();

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

    ImGui::DockBuilderDockWindow(EditorWindowName::kScene, centerViewport);
    ImGui::DockBuilderDockWindow(EditorWindowName::kGame, centerViewport);
    ImGui::DockBuilderDockWindow(EditorWindowName::kBehaviorTree, centerViewport);
    ImGui::DockBuilderDockWindow(EditorWindowName::kBlackBoard, centerViewport);

    ImGui::DockBuilderDockWindow(EditorWindowName::kHierarchy, rightColumn);
    ImGui::DockBuilderDockWindow(EditorWindowName::kInspector, inspectorNode);

    ImGui::DockBuilderDockWindow(EditorWindowName::kAssetBundle, bottomPanel);
    ImGui::DockBuilderDockWindow(EditorWindowName::kResourceCounter, bottomPanel);
    // Tile 스타일에서 Content Browser는 팝업 드로어라 도크할 자리가 없다
    // (ContentsBrowserWindow가 SetPopup으로 그렇게 만든다). 도크해 두면
    // 드로어가 열릴 때 빈 탭이 남는다.
    if (!contentBrowserIsDrawer)
        ImGui::DockBuilderDockWindow(EditorWindowName::kContentBrowser, bottomPanel);

    ImGui::DockBuilderFinish(id);
}

void EditorRenderer::BeginRender()
{
    m_host->BeginFrame();

    // 스케일 변경 추적. 재빌드 없이 스타일·폰트 배율만 즉시 적용한다 —
    // 더 선명하게 필요해지면 ApplyEditorScale의 rebuildFonts를 켠다.
    const float targetScale = EditorSettingsStore::Get().Preferences().GetImGuiScale();
    if (m_lastRequestedScale != targetScale)
    {
        ApplyEditorScale(targetScale, /*rebuildFonts=*/false);
        m_lastRequestedScale = targetScale;
    }

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

    auto& container = ImGuiRegister::GetInstance()->m_contexts;
    for (auto& [name, context] : container)
    {
        context.Render();
    }
}

void EditorRenderer::EndRender()
{
    m_host->EndFrame();
}
