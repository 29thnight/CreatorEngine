#include "EditorRenderer.h"
#include "RHI/IImGuiHost.h"
#include "ImGUiRegisterClass.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "DataSystem.h"
#include "EngineSetting.h"
#include "PathFinder.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <stdexcept>
#include <string>

namespace
{
    // 에디터 위젯 스타일. 구 ImGuiRenderer의 ImGuiBootstrap::ApplyStyle 그대로다.
    void ApplyEditorStyle(ImGuiStyle* _style)
    {
        _style->WindowPadding = ImVec2(5, 5);
        _style->WindowRounding = 5.0f;
        _style->WindowBorderSize = 0.01f;
        _style->FramePadding = ImVec2(5, 5);
        _style->FrameRounding = 4.0f;
        _style->ItemSpacing = ImVec2(12, 8);
        _style->ItemInnerSpacing = ImVec2(8, 6);
        _style->IndentSpacing = 25.0f;
        _style->ScrollbarSize = 15.0f;
        _style->ScrollbarRounding = 9.0f;
        _style->GrabMinSize = 5.0f;
        _style->GrabRounding = 3.0f;

        _style->Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
        _style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
        _style->Colors[ImGuiCol_WindowBg] = ImVec4(0.2196f, 0.2196f, 0.2196f, 1.00f);
        _style->Colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.2353f, 0.2353f, 0.2353f, 1.00f);
        _style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
        _style->Colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.33f, 0.88f);
        _style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
        _style->Colors[ImGuiCol_FrameBg] = ImVec4(0.1569f, 0.1569f, 0.1569f, 1.00f);
        _style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
        _style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
        _style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
        _style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
        _style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
        _style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
        _style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
        _style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
        _style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
        _style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
        _style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
        _style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
        _style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
        _style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
        _style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
        _style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
        _style->Colors[ImGuiCol_Header] = ImVec4(0.1569f, 0.1569f, 0.1569f, 1.00f);
        _style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
        _style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
        _style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        _style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
        _style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
        _style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
        _style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
        _style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
        _style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 0.00f, 0.00f, 1.00f);
        _style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
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

    m_lastAppliedScale = EngineSettingInstance->GetImGuiScale();
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
    ImGuiID id = dockspaceId;
    const ImVec2 size{ width, height };
    const ImVec2 nodePos{ posX, posY };

    ImGuiID dock1;
    ImGuiID dock_gameView;
    ImGuiID dock2;
    ImGuiID dock3;
    ImGuiID dock4;

    switch (EngineSettingInstance->GetContentsBrowserStyle())
    {
    case ContentsBrowserStyle::Tree:
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id);

        ImGui::DockBuilderSetNodeSize(id, size);
        ImGui::DockBuilderSetNodePos(id, nodePos);

        dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.5f, nullptr, &id);
        dock_gameView = ImGui::DockBuilderSplitNode(dock1, ImGuiDir_Down, 0.5f, nullptr, &dock1);
        dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.33f, nullptr, &id);
        dock3 = ImGui::DockBuilderSplitNode(dock2, ImGuiDir_Right, 0.5f, nullptr, &dock2);
        dock4 = ImGui::DockBuilderSplitNode(dock3, ImGuiDir_Down, 0.5f, nullptr, &dock3);

        ImGui::DockBuilderDockWindow(ICON_FA_USERS_VIEWFINDER "  Scene      ", dock1);
        ImGui::DockBuilderDockWindow("Behavior Tree Editor", dock1);
        ImGui::DockBuilderDockWindow("BlackBoard Editor", dock1);
        ImGui::DockBuilderDockWindow(ICON_FA_GAMEPAD "  Game        ", dock_gameView);
        ImGui::DockBuilderDockWindow(ICON_FA_BARS_STAGGERED "  Hierarchy", dock2);
        ImGui::DockBuilderDockWindow(ICON_FA_DIAGRAM_PROJECT "  AssetBundle", dock3);
        ImGui::DockBuilderDockWindow(ICON_FA_HARD_DRIVE "  Content Browser", dock3);
        ImGui::DockBuilderDockWindow(ICON_FA_CIRCLE_INFO "  Inspector", dock4);
        ImGui::DockBuilderFinish(id);

        EngineSettingInstance->SetImGuiInitialized(true);
        break;
    case ContentsBrowserStyle::Tile:
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id);

        ImGui::DockBuilderSetNodeSize(id, size);
        ImGui::DockBuilderSetNodePos(id, nodePos);

        dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.5f, nullptr, &id);
        dock_gameView = ImGui::DockBuilderSplitNode(dock1, ImGuiDir_Down, 0.5f, nullptr, &dock1);
        dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.5f, nullptr, &id);
        dock3 = ImGui::DockBuilderSplitNode(dock2, ImGuiDir_Right, 0.5f, nullptr, &dock2);

        ImGui::DockBuilderDockWindow(ICON_FA_USERS_VIEWFINDER "  Scene      ", dock1);
        ImGui::DockBuilderDockWindow("Behavior Tree Editor", dock1);
        ImGui::DockBuilderDockWindow("BlackBoard Editor", dock1);
        ImGui::DockBuilderDockWindow(ICON_FA_GAMEPAD "  Game        ", dock_gameView);
        ImGui::DockBuilderDockWindow(ICON_FA_BARS_STAGGERED "  Hierarchy", dock2);
        ImGui::DockBuilderDockWindow(ICON_FA_CIRCLE_INFO "  Inspector", dock3);
        ImGui::DockBuilderFinish(id);

        EngineSettingInstance->SetImGuiInitialized(true);
        break;
    }
}

void EditorRenderer::BeginRender()
{
    m_host->BeginFrame();

    // 스케일 변경 추적. 재빌드 없이 스타일·폰트 배율만 즉시 적용한다 —
    // 더 선명하게 필요해지면 ApplyEditorScale의 rebuildFonts를 켠다.
    const float targetScale = EngineSettingInstance->GetImGuiScale();
    if (m_lastRequestedScale != targetScale)
    {
        ApplyEditorScale(targetScale, /*rebuildFonts=*/false);
        m_lastRequestedScale = targetScale;
    }

    // ── 메인 독스페이스 ──
    // 구 ImGuiRenderer에서는 #ifndef BUILD_FLAG 안이었다. 지금은 이 파일
    // 자체가 에디터 exe에만 링크되므로 조건이 필요 없다 — 매크로가 하던
    // 구분을 프로젝트 경계가 한다.
    const float menuBarSize = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    const ImVec2 workCenter{ ImGui::GetMainViewport()->GetWorkCenter() };
    ImGuiID id = ImGui::GetID("MainWindowGroup");
    const ImVec2 size{ ImGui::GetMainViewport()->Size.x,
        ImGui::GetMainViewport()->Size.y - (menuBarSize * 2.f) };
    const ImVec2 nodePos{ workCenter.x - size.x * 0.5f, workCenter.y - size.y * 0.5f };

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
    if (m_firstLoop)
    {
        file::path iniPath = PathFinder::RelativeToExecutable("imgui.ini");
        if (!file::exists(iniPath))
        {
            BuildInitialDockLayout(id, size.x, size.y, nodePos.x, nodePos.y);
        }
        m_firstLoop = false;
    }
}

void EditorRenderer::Render()
{
    auto& directoryQueue = DataSystems->m_LoadTextureAssetQueue;
    if (!directoryQueue.empty())
    {
        DataSystems->SelectTextureType();
    }

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
