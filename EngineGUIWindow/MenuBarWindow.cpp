#ifndef DYNAMICCPP_EXPORTS
#define IMGUI_DEFINE_MATH_OPERATORS
#include "MenuBarWindow.h"
#include "SceneRenderer.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "FileDialog.h"
#include "Profiler.h"
#include "CoreWindow.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "AIManager.h"
#include "BTBuildGraph.h"

void ShowVRAMBarGraph(uint64_t usedVRAM, uint64_t budgetVRAM)
{
    float usagePercent = (float)usedVRAM / (float)budgetVRAM;
    ImGui::Text("VRAM Usage: %.2f MB / %.2f MB", usedVRAM / (1024.0f * 1024.0f), budgetVRAM / (1024.0f * 1024.0f));

    // 바 높이와 너비 정의
    ImVec2 barSize = ImVec2(300, 20);
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 배경 바
    drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + barSize.x, cursorPos.y + barSize.y), IM_COL32(100, 100, 100, 255));

    // 사용량 바
    float fillWidth = barSize.x * usagePercent;
    drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + fillWidth, cursorPos.y + barSize.y), IM_COL32(50, 200, 50, 255));

    ImGui::Dummy(barSize); // 레이아웃 공간 확보
}

std::string WordWrapText(const std::string& input, size_t maxLineLength)
{
    std::istringstream iss(input);
    std::ostringstream oss;
    std::string word;
    size_t lineLength = 0;

    while (iss >> word)
    {
        if (lineLength + word.length() > maxLineLength)
        {
            oss << '\n';
            lineLength = 0;
        }
        else if (lineLength > 0)
        {
            oss << ' ';
            ++lineLength;
        }

        oss << word;
        lineLength += word.length();
    }

    return oss.str();
}

MenuBarWindow::MenuBarWindow(SceneRenderer* ptr) :
    m_sceneRenderer(ptr)
{
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    ImFontConfig font_config;
    icons_config.MergeMode = true; // Merge icon font to the previous font if you want to have both icons and text
    m_koreanFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 16.0f, nullptr, io.Fonts->GetGlyphRangesKorean());
    io.Fonts->AddFontFromMemoryCompressedTTF(FA_compressed_data, FA_compressed_size, 16.0f, &icons_config, icons_ranges);
    io.Fonts->Build();

    ImGui::ContextRegister("LightMap", true, [&]() {
        auto& useTestLightmap = m_sceneRenderer->useTestLightmap;
        auto& m_pPositionMapPass = m_sceneRenderer->m_pPositionMapPass;
        auto& lightMap = m_sceneRenderer->lightMap;
        auto& m_renderScene = m_sceneRenderer->m_renderScene;
        auto& m_pLightMapPass = m_sceneRenderer->m_pLightMapPass;
        static bool isLightMapSwitch{ useTestLightmap.load() };

        ImGui::BeginChild("LightMap", ImVec2(600, 600), false);
        ImGui::Text("LightMap");
        if (ImGui::CollapsingHeader("Settings")) {

            ImGui::Checkbox("LightmapPass On/Off", &isLightMapSwitch);

            ImGui::Text("Position and NormalMap Settings");
            ImGui::DragInt("PositionMap Size", &m_pPositionMapPass->posNormMapSize, 128, 512, 8192);
            if (ImGui::Button("Clear position normal maps")) {
                m_pPositionMapPass->ClearTextures();
            }
            ImGui::Checkbox("IsPositionMapDilateOn", &m_pPositionMapPass->isDilateOn);
            ImGui::DragInt("PosNorm Dilate Count", &m_pPositionMapPass->posNormDilateCount, 1, 0, 16);
            ImGui::Text("LightMap Bake Settings");
            ImGui::DragInt("LightMap Size", &lightMap.canvasSize, 128, 512, 8192);
            ImGui::DragFloat("Bias", &lightMap.bias, 0.001f, 0.001f, 0.2f);
            ImGui::DragInt("Padding", &lightMap.padding);
            ImGui::DragInt("UV Size", &lightMap.rectSize, 1, 20, lightMap.canvasSize - (lightMap.padding * 2));
            ImGui::DragInt("LeafCount", &lightMap.leafCount, 1, 0, 1024);
            ImGui::DragInt("Indirect Count", &lightMap.indirectCount, 1, 0, 128);
            ImGui::DragInt("Indirect Sample Count", &lightMap.indirectSampleCount, 1, 0, 512);
            //ImGui::DragInt("Dilate Count", &lightMap.dilateCount, 1, 0, 16);
            ImGui::DragInt("Direct MSAA Count", &lightMap.directMSAACount, 1, 0, 16);
            ImGui::DragInt("Indirect MSAA Count", &lightMap.indirectMSAACount, 1, 0, 16);
            ImGui::Checkbox("Use Environment Map", &lightMap.useEnvironmentMap);
        }

        if (ImGui::Button("Generate LightMap"))
        {
            Camera c{};
            // 메쉬별로 positionMap 생성
            m_pPositionMapPass->Execute(*m_renderScene, c);
            // lightMap 생성
            lightMap.GenerateLightMap(m_renderScene, m_pPositionMapPass, m_pLightMapPass);

            //m_pLightMapPass->Initialize(lightMap.lightmaps);
        }

        if (ImGui::CollapsingHeader("Baked Maps")) {
            if (lightMap.imgSRV)
            {
                ImGui::Text("LightMaps");
                for (int i = 0; i < lightMap.lightmaps.size(); i++) {
                    if (ImGui::ImageButton("##LightMap", (ImTextureID)lightMap.lightmaps[i]->m_pSRV, ImVec2(300, 300))) {
                        //ImGui::Image((ImTextureID)lightMap.lightmaps[i]->m_pSRV, ImVec2(512, 512));
                    }
                }
                //ImGui::Text("indirectMaps");
                //for (int i = 0; i < lightMap.indirectMaps.size(); i++) {
                //	if (ImGui::ImageButton("##IndirectMap", (ImTextureID)lightMap.indirectMaps[i]->m_pSRV, ImVec2(300, 300))) {
                //		//ImGui::Image((ImTextureID)lightMap.indirectMaps[i]->m_pSRV, ImVec2(512, 512));
                //	}
                //}
                ImGui::Text("environmentMaps");
                for (int i = 0; i < lightMap.environmentMaps.size(); i++) {
                    if (ImGui::ImageButton("##EnvironmentMap", (ImTextureID)lightMap.environmentMaps[i]->m_pSRV, ImVec2(300, 300))) {
                        //ImGui::Image((ImTextureID)lightMap.environmentMaps[i]->m_pSRV, ImVec2(512, 512));
                    }
                }
                ImGui::Text("directionalMaps");
                for (int i = 0; i < lightMap.directionalMaps.size(); i++) {
                    if (ImGui::ImageButton("##DirectionalMap", (ImTextureID)lightMap.directionalMaps[i]->m_pSRV, ImVec2(300, 300))) {
                        //ImGui::Image((ImTextureID)lightMap.environmentMaps[i]->m_pSRV, ImVec2(512, 512));
                    }
                }
            }
            else {
                ImGui::Text("No LightMap");
            }
        }

        ImGui::EndChild();

        useTestLightmap.store(isLightMapSwitch);
    });

    ImGui::GetContext("LightMap").Close();

    ImGui::ContextRegister("CollisionMatrixPopup", true, [&]() 
    {
        ImGui::Text("Collision Matrix");
        ImGui::Separator();
        //todo::grid matrix
        if(collisionMatrix.empty()){
            collisionMatrix = PhysicsManagers->GetCollisionMatrix();
        }
        int flags = ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize;

        if (ImGui::BeginChild("Matrix", ImVec2(0, 0), flags))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 2));
            ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(1.0f, 1.0f, 1.0f, 0.0f)); // 진한 줄 - 반투명 흰색
            ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));  // 연한 줄 - 더 투명

            const int matrixSize = 32;
            const float checkboxSize = ImGui::GetFrameHeight();
            const float cellWidth = checkboxSize;

            // 총 열 개수 = 인덱스 번호 포함해서 33개
            if (ImGui::BeginTable("CollisionMatrixTable", matrixSize + 1,
                ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
            {
                // -------------------------
                // 첫 번째 헤더 행
                // -------------------------
                ImGui::TableNextRow();
                for (int col = -1; col < matrixSize; ++col)
                {
                    ImGui::TableNextColumn();
                    if (col >= 0)
                        ImGui::Text("%2d", col);
                    else
                        ImGui::Text("   "); // 좌상단 빈칸
                }

                // -------------------------
                // 본문 행 렌더링
                // -------------------------
                for (int row = 0; row < matrixSize; ++row)
                {
                    ImGui::TableNextRow();
                    for (int col = -1; col < matrixSize; ++col)
                    {
                        ImGui::TableNextColumn();
                        if (col == -1)
                        {
                            // 행 번호
                            ImGui::Text("%2d", row);
                        }
                        else
                        {
                            ImGui::PushID(row * matrixSize + col);

                            if (row <= col)
                            {
                                bool checkboxValue = (bool)collisionMatrix[row][col];
                                ImGui::Checkbox("##chk", &checkboxValue);
                                collisionMatrix[row][col] = (uint8_t)checkboxValue;
                            }
                            else
                            {
                                // 시각적으로 동일한 크기 확보
                                ImGui::Dummy(ImVec2(cellWidth, checkboxSize));
                            }

                            ImGui::PopID();
                        }
                    }
                }

                ImGui::EndTable();
            }

            ImGui::PopStyleColor(2); // 설정한 2개 색상 pop
            ImGui::PopStyleVar();
            ImGui::EndChild();
        }

        ImGui::Separator();
        if (ImGui::Button("Save"))
        {
            //적용된 충돌 매스릭스 저장
            PhysicsManagers->SetCollisionMatrix(collisionMatrix);
            m_bCollisionMatrixWindow = false;
            ImGui::GetContext("CollisionMatrixPopup").Close();
        }
        
    }, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar);
   
	ImGui::GetContext("CollisionMatrixPopup").Close();
}

void MenuBarWindow::RenderMenuBar()
{
    ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    float height = ImGui::GetFrameHeight();

    if (ImGui::BeginViewportSideBar("##MainMenuBar", viewport, ImGuiDir_Up, height, window_flags))
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene"))
                {
                    m_bShowNewScenePopup = true;
				}
                if (ImGui::MenuItem("Save Current Scene"))
                {
                    //Test
                    SceneManagers->resetSelectedObjectEvent.Broadcast();
                    file::path fileName = ShowSaveFileDialog(
						L"Scene Files (*.creator)\0*.creator\0",
						L"Save Scene",
                        PathFinder::Relative("Scenes\\").wstring()
					);
					if (!fileName.empty())
					{
                        SceneManagers->SaveScene(fileName.string());
					}
                    else
                    {
						Debug->LogError("Failed to save scene.");
                    }
                }
                if (ImGui::MenuItem("Load Scene"))
                {
					SceneManagers->resetSelectedObjectEvent.Broadcast();
                    //SceneManagers->LoadScene();

					file::path fileName = ShowOpenFileDialog(
						L"Scene Files (*.creator)\0*.creator\0",
						L"Load Scene",
						PathFinder::Relative("Scenes\\").wstring()
					);
                    if (!fileName.empty())
                    {
                        SceneManagers->LoadScene(fileName.string());
                    }
                    else
                    {
                        Debug->LogError("Failed to load scene.");
                    }

                }
                if (ImGui::MenuItem("Exit"))
                {
                    // Exit action
					HWND handle = m_sceneRenderer->m_deviceResources->GetWindow()->GetHandle();
                    PostMessage(handle, WM_CLOSE, 0, 0);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("LightMap Window"))
                {
                    if (!ImGui::GetContext("LightMap").IsOpened())
                    {
                        ImGui::GetContext("LightMap").Open();
                    }
                }

                if (ImGui::MenuItem("Effect Editor"))
                {
                    auto& ctx = ImGui::GetContext("EffectEdit");
                    if (ctx.IsOpened())
                    {
                        ctx.Close();
                    }
                    else
                    {
                        ctx.Open();
                    }
                }

                if( ImGui::MenuItem("Behavior Tree Editor"))
                {
                    m_bShowBehaviorTreeWindow = true;
				}

                if (ImGui::MenuItem("Blackboard Editor"))
                {
					m_bShowBlackBoardWindow = true;
				}

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Settings"))
            {
                if (ImGui::MenuItem("Pipeline Setting"))
                {
                    if (!ImGui::GetContext("RenderPass").IsOpened())
                    {
                        ImGui::GetContext("RenderPass").Open();
                    }
                }

                if (ImGui::MenuItem("Collision Matrix"))
                {

                    m_bCollisionMatrixWindow = true;
                }

                ImGui::EndMenu();
            }

            float availRegion = ImGui::GetContentRegionAvail().x;

            ImGui::SetCursorPos(ImVec2((availRegion * 0.5f) + 100.f, 1));

            if (ImGui::Button(SceneManagers->m_isGameStart ? ICON_FA_STOP : ICON_FA_PLAY))
            {
                Meta::UndoCommandManager->ClearGameMode();
				SceneManagers->m_isGameStart = !SceneManagers->m_isGameStart;
				Meta::UndoCommandManager->m_isGameMode = SceneManagers->m_isGameStart;
            }

            ImVec2 curPos = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(curPos.x, 1));

			if (ImGui::Button(ICON_FA_PAUSE))
			{
			}

            ImGui::EndMainMenuBar();
        }
        ImGui::End();
    }

    if (ImGui::BeginViewportSideBar("##MainStatusBar", viewport, ImGuiDir_Down, height + 1, window_flags)) {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::Button(ICON_FA_HARD_DRIVE " Content Drawer"))
            {
                auto& contentDrawerContext = ImGui::GetContext(ICON_FA_HARD_DRIVE " Content Browser");
                if (!contentDrawerContext.IsOpened())
                {
                    contentDrawerContext.Open();
                }
                else
                {
                    contentDrawerContext.Close();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TERMINAL " Output Log "))
            {
                m_bShowLogWindow = !m_bShowProfileWindow;
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_BUG " ProfileFrame "))
            {
                m_bShowProfileWindow = !m_bShowProfileWindow;
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();
    }

    if (m_bShowLogWindow)
    {
        ShowLogWindow();
    }

    if (m_bShowBehaviorTreeWindow)
    {
        ShowBehaviorTreeWindow();
	}

    if (m_bShowBlackBoardWindow)
    {
        ShowBlackBoardWindow();
    }

    if (m_bShowProfileWindow)
    {
        ImGui::Begin(ICON_FA_CHART_BAR " FrameProfiler", &m_bShowProfileWindow);
        {
            ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindow());
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

            const float vramPanelHeight = 50.0f; // VRAM 그래프 높이
            const float contentWidth = ImGui::GetContentRegionAvail().x;
            const float contentHeight = ImGui::GetContentRegionAvail().y;

            // 위쪽: HUD
            ImGui::BeginChild("Profiler HUD", ImVec2(contentWidth, contentHeight - vramPanelHeight), false);
            {
                DrawProfilerHUD();
            }
            ImGui::EndChild();

            // 아래쪽: VRAM 그래프
            ImGui::BeginChild("VRAM Panel", ImVec2(contentWidth, vramPanelHeight), false);
            {
                auto info = m_sceneRenderer->m_deviceResources->GetVideoMemoryInfo();
                ShowVRAMBarGraph(info.CurrentUsage, info.Budget); // 가로 막대 그래프
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    if (m_bShowNewScenePopup)
    {
        ImGui::OpenPopup("NewScenePopup");
        m_bShowNewScenePopup = false;
	}


    if (m_bCollisionMatrixWindow) 
    {
        ImGui::GetContext("CollisionMatrixPopup").Open();
		m_bCollisionMatrixWindow = false;
    }

    if (ImGui::BeginPopup("NewScenePopup"))
    {
        static char newSceneName[256];
        ImGui::InputText("Scene Name", newSceneName, 256);
        ImGui::Separator();
        if (ImGui::Button("Create"))
        {
            SceneManagers->resetSelectedObjectEvent.Broadcast();
            std::string sceneName = newSceneName;
            if (sceneName.empty())
            {
                SceneManagers->CreateScene();
            }
            else
            {
                SceneManagers->CreateScene(sceneName);
            }
            memset(newSceneName, 0, sizeof(newSceneName));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            memset(newSceneName, 0, sizeof(newSceneName));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MenuBarWindow::ShowLogWindow()
{
    static int levelFilter = spdlog::level::trace;
    static bool autoScroll = true;
    bool isClear = Debug->IsClear();

    ImGui::PushFont(m_koreanFont);
    ImGui::Begin(ICON_FA_TERMINAL " Log", &m_bShowLogWindow, ImGuiWindowFlags_NoDocking);
    ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindow());
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    // == 상단 고정 헤더 영역 ==
    ImGui::BeginChild("LogHeader", ImVec2(0, 0),
        ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar);
    {
        if (ImGui::Button("Clear"))
        {
            Debug->Clear();
        }
        ImGui::SameLine();
        ImGui::Combo("Log Filter", &levelFilter,
            "Trace\0Debug\0Info\0Warning\0Error\0Critical\0\0");
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &autoScroll);
    }
    ImGui::EndChild();

    ImGui::Separator();

    // == 스크롤 가능한 로그 영역 ==
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    {
        if (isClear)
        {
            Debug->toggleClear();
            ImGui::EndChild();
            ImGui::End();
            ImGui::PopFont();
            return;
        }

        auto entries = Debug->get_entries();
        float sizeX = ImGui::GetContentRegionAvail().x;

        // 현재 스크롤 상태 감지
        bool shouldScroll = autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f;

        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];
            if (entry.level != spdlog::level::trace && entry.level < levelFilter)
                continue;

            bool is_selected = (i == m_selectedLogIndex);

            ImVec4 color;
            switch (entry.level)
            {
            case spdlog::level::info:       color = ImVec4(1,    1,    1,    1); break;
            case spdlog::level::warn:       color = ImVec4(1,    1,    0,    1); break;
            case spdlog::level::err:        color = ImVec4(1,    0.4f, 0.4f, 1); break;
            case spdlog::level::critical:   color = ImVec4(1,    0,    0,    1); break;
            default:                        color = ImVec4(0.7f, 0.7f, 0.7f, 1); break;
            }

            if (is_selected)
                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(100, 100, 255, 100));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(color));

            std::string wrapped = WordWrapText(entry.message, 120);
            int stringLine = std::count(wrapped.begin(), wrapped.end(), '\n');

            ImGui::PushID(i);
            if (ImGui::Selectable((ICON_FA_CIRCLE_INFO + std::string(" ") + wrapped).c_str(),
                is_selected, ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(sizeX, float(35 * stringLine))))
            {
                m_selectedLogIndex = i;

                std::regex pattern(R"(([A-Za-z]:\\.*))");
                std::istringstream iss(wrapped);
                std::string line;

                while (std::getline(iss, line))
                {
                    std::smatch match;
                    if (std::regex_search(line, match, pattern) && entry.level != spdlog::level::debug)
                    {
                        std::string fileDirectory = match[1].str();
                        DataSystems->OpenFile(fileDirectory);
                    }
                }
            }
            ImGui::PopID();
            ImGui::PopStyleColor();
            if (is_selected)
                ImGui::PopStyleColor();
        }

        if (shouldScroll)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopFont();
}

void MenuBarWindow::ShowLightMapWindow()
{
    ImGui::GetContext("LightMap").Open();
}

ed::EditorContext* s_MenuBarBTEditorContext{ nullptr };

void MenuBarWindow::ShowBehaviorTreeWindow()
{
    static BTBuildGraph graph;
    static bool showEditor = false;
    static bool isfirstLoad = false;
	static std::string BTName;

    if (m_bShowBehaviorTreeWindow)
    {
        showEditor = true;
    }

    if (showEditor)
    {
        ImGui::Begin("Behavior Tree Editor", &showEditor);

        if (ImGui::Button("Create"))
        {
            file::path BTSavePath = ShowSaveFileDialog(L"", L"Save Behavior Tree Asset",
                PathFinder::Relative("BehaviorTree"));

            if (!BTSavePath.empty())
            {
                BTName = BTSavePath.stem().string();
                if (!file::exists(BTSavePath.parent_path()))
                {
                    file::create_directories(BTSavePath.parent_path());
                }
                graph.CleanUp();
            }
            else
            {
				Debug->LogError("Failed to create Behavior Tree.");
            }
        }
		ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            file::path fileName = ShowOpenFileDialog(
                L"Behavior Tree Files (*.bt)\0*.bt\0",
                L"Load Behavior Tree",
                PathFinder::Relative("BehaviorTree").wstring()
            );

            if (!fileName.empty())
            {
                BTName = fileName.filename().string();
                if (file::exists(fileName))
                {
                    graph.CleanUp();
                    auto node = MetaYml::LoadFile(fileName.string());
                    const YAML::Node& nodeList = node["NodeList"];
                    if (nodeList && nodeList.IsSequence())
                    {
                        for (const auto& node : nodeList)
                        {
                            graph.DeserializeSingleNode(node);
                        }
                    }
                }
                else
                {
                    Debug->LogError("Behavior Tree file does not exist: " + fileName.string());
                }
            }
            else
            {
                Debug->LogError("Failed to load Behavior Tree.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            if (BTName.empty())
            {
                file::path fileName = ShowSaveFileDialog(
                    L"Behavior Tree Files (*.bt)\0*.bt\0",
                    L"Save Behavior Tree",
                    PathFinder::Relative("BehaviorTree").wstring()
				);

                if (!fileName.empty())
                {
                    BTName = fileName.stem().string();
                }
                else
                {
                    Debug->LogError("Failed to save Behavior Tree.");
				}
            }

            file::path BTPath = PathFinder::Relative("BehaviorTree\\" + BTName + ".bt");

            if (!file::exists(BTPath.parent_path()))
            {
                file::create_directories(BTPath.parent_path());
            }

            for (auto& node : graph.NodeList)
            {
                node.Position = BT::ToMathfVec2(node.PositionEditor);
            }

            // Save the graph to a file
            auto node = Meta::Serialize(&graph);

            std::ofstream outFile(BTPath.string());

            if (outFile.is_open())
            {
                outFile << node; // Pretty print with 4 spaces
                outFile.close();
            }
            else
            {
                std::cerr << "Failed to open file for writing: " << BTPath.string() << std::endl;
            }
		}

		ImGui::Separator();

        static BTBuildNode*& selectNode = graph.SelectedNode;
        bool nodeMenuOpen = false;
		bool addScriptMenuOpen = false;

        if (!s_MenuBarBTEditorContext)
        {
            // Ensure the file path is valid and exists
            ed::Config config;
            config.SettingsFile = nullptr;
            isfirstLoad = true;
            s_MenuBarBTEditorContext = ed::CreateEditor(&config);
        }

        static ed::NodeId s_SelectedNodeId = 0;
        static ed::NodeId s_newNodeId = 0;
        constexpr float rounding = 5.0f;
        constexpr float padding = 15.0f;
        constexpr ImVec2 insidePadding = { 8.f, 0.f };
        static ImVec2 s_DragDelta{};
        static ImVec2 s_DragStartNodePos{};

        ed::SetCurrentEditor(s_MenuBarBTEditorContext);
        ed::Begin("BTEditor");

        for (auto& node : graph.NodeList)
        {
            ed::NodeId nid{ node.ID.m_ID_Data };
            ImVec2 prev = BT::ToImVec2(node.Position);

            ed::PinId inPin = node.InputPinId;
            ed::PinId outPin = node.OutputPinId;
            std::string nodeName = node.Name;

            const ImVec4 pinBackground = ed::GetStyle().Colors[ed::StyleColor_NodeBg];
            ImColor nodeBgColor = pinBackground + ImColor(15, 15, 15, 0);

            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(nodeBgColor));
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(32, 32, 32, 200));
            ed::PushStyleColor(ed::StyleColor_PinRect, ImColor(60, 180, 255, 150));
            ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImColor(60, 180, 255, 150));

            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
            ed::PushStyleVar(ed::StyleVar_NodeRounding, rounding);
            ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(0.0f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
            ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f);
            ed::PushStyleVar(ed::StyleVar_PinBorderWidth, 1.0f);
            ed::PushStyleVar(ed::StyleVar_PinRadius, 5.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));

            ImGui::BeginGroup();

            ed::BeginNode(nid);

            if (isfirstLoad)
            {
                ed::SetNodePosition(nid, prev);
            }

            if (0 != s_newNodeId.Get())
            {
				ed::SetNodePosition(s_newNodeId, s_DragStartNodePos + s_DragDelta);
				s_newNodeId = 0;
            }

            ImRect inputsRect;
            int inputAlpha = 200;
            if (!node.IsRoot)
            {
                ImGui::Dummy(ImVec2(160, padding));
                inputsRect = 
                    ImRect(ImGui::GetItemRectMin() + 
                        insidePadding, ImGui::GetItemRectMax() - insidePadding);

                ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
                ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 10.0f);
                ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersBottom);

                ed::BeginPin(inPin, ed::PinKind::Input);
                ed::PinPivotRect(inputsRect.GetTL(), inputsRect.GetBR());
                ed::PinRect(inputsRect.GetTL(), inputsRect.GetBR());
                ed::EndPin();
                ed::PopStyleVar(3);
            }
            else
                ImGui::Dummy(ImVec2(160, padding));

            ImGui::Dummy(ImVec2(160, 10));
            ImRect contentScriptRect;
            ImVec2 textScriptSize;
            ImVec2 textScriptPos;
            bool isScriptNode = node.HasScript;
            if (isScriptNode)
            {
                if (AIManagers->IsActionNodeRegistered(node.ScriptName)
                    || AIManagers->IsConditionNodeRegistered(node.ScriptName))
                {
                    ImGui::Dummy(ImVec2(160, 50));
                    contentScriptRect = ImRect(ImGui::GetItemRectMin() + insidePadding, ImGui::GetItemRectMax() - insidePadding);
                    textScriptSize = ImGui::CalcTextSize(node.ScriptName.c_str());
                    textScriptPos = ImVec2(contentScriptRect.GetTL().x + (152 - textScriptSize.x) / 2, contentScriptRect.GetTL().y + (50 - textScriptSize.y) / 2);
                    ImGui::GetWindowDrawList()->AddText(textScriptPos, IM_COL32_WHITE, node.ScriptName.c_str());
                }
            }

            ImGui::Dummy(ImVec2(160, 50));
            ImRect contentRect(ImGui::GetItemRectMin() + insidePadding, ImGui::GetItemRectMax() - insidePadding);
            ImVec2 textSize = ImGui::CalcTextSize(node.Name.c_str());
            ImVec2 textPos = ImVec2(contentRect.GetTL().x + (152 - textSize.x) / 2, contentRect.GetTL().y + (50 - textSize.y) / 2);
            ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32_WHITE, node.Name.c_str());

            ImRect outputsRect;
            int outputAlpha = 200;

            ImGui::Dummy(ImVec2(160, 10));

            if (BT::IsCompositeNode(node.Type) || BT::IsDecoratorNode(node.Type))
            {
                ImGui::Dummy(ImVec2(160, padding));
                outputsRect = ImRect(ImGui::GetItemRectMin() + insidePadding, ImGui::GetItemRectMax() - insidePadding);

                ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersTop);
                ed::BeginPin(outPin, ed::PinKind::Output);
                ed::PinPivotRect(outputsRect.GetTL(), outputsRect.GetBR());
                ed::PinRect(outputsRect.GetTL(), outputsRect.GetBR());
                ed::EndPin();
                ed::PopStyleVar();
            }
            else
                ImGui::Dummy(ImVec2(160, padding));

            ed::EndNode();
            ed::PopStyleVar(7);
            ed::PopStyleColor(4);

            ImGui::EndGroup();

            ImGui::PopStyleVar(3);

            auto drawList = ed::GetNodeBackgroundDrawList(nid);

            const auto topRoundCornersFlags = ImDrawFlags_RoundCornersTop;
            const auto bottomRoundCornersFlags = ImDrawFlags_RoundCornersBottom;

            if (!node.IsRoot)
            {
                drawList->AddRectFilled(inputsRect.GetTL() + ImVec2(0, 1), inputsRect.GetBR(),
                    IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), inputAlpha), 4.0f, bottomRoundCornersFlags);
                drawList->AddRect(inputsRect.GetTL() + ImVec2(0, 1), inputsRect.GetBR(),
                    IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), inputAlpha), 4.0f, bottomRoundCornersFlags);
            }

            if (BT::IsCompositeNode(node.Type) || BT::IsDecoratorNode(node.Type))
            {
                drawList->AddRectFilled(outputsRect.GetTL(), outputsRect.GetBR() - ImVec2(0, 1),
                    IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), outputAlpha), 4.0f, topRoundCornersFlags);
                drawList->AddRect(outputsRect.GetTL(), outputsRect.GetBR() - ImVec2(0, 1),
                    IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), outputAlpha), 4.0f, topRoundCornersFlags);
            }

            drawList->AddRectFilled(contentRect.GetTL(), contentRect.GetBR(), IM_COL32(170, 100, 255, 255), 0.0f);
            drawList->AddRect(
                contentRect.GetTL(),
                contentRect.GetBR(),
                IM_COL32(200, 140, 255, 255), 0.0f);

            if (isScriptNode)
            {
                drawList->AddRectFilled(contentScriptRect.GetTL(), contentScriptRect.GetBR(), IM_COL32(100, 200, 255, 255), 0.0f);
                drawList->AddRect(
                    contentScriptRect.GetTL(),
                    contentScriptRect.GetBR(),
                    IM_COL32(140, 220, 255, 255), 0.0f);
            }

            static ed::PinId prevPinID{};
            static ed::PinId m_DraggingPin = 0;
            static BTBuildNode* waitRaw = nullptr;
            static ImVec2 pinPos;

            ed::PinId pinID = ed::GetHoveredPin();
            if (pinID.Get() != 0 &&
                prevPinID.Get() != pinID.Get() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left)
                )
            {
                s_SelectedNodeId = ed::NodeId(pinID.Get() >> 1);
                m_DraggingPin = pinID;
                prevPinID = pinID;
                s_DragStartNodePos = ed::GetNodePosition(s_SelectedNodeId);
                s_DragDelta = {};
                pinPos = ImGui::GetMousePos();
            }

            if (m_DraggingPin.Get() != 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                m_DraggingPin = 0;
                prevPinID = 0;
                pinPos = {};

                auto it = graph.Nodes.find(HashedGuid{ s_SelectedNodeId.Get() });
                if (it != graph.Nodes.end())
                {
                    waitRaw = it->second;
                }
            }

            if (waitRaw)
            {
                ed::Suspend();
                ImGui::OpenPopup("NodeMenu");
                if (node.ID == waitRaw->ID) {
                    selectNode = &node;
                    nodeMenuOpen = true;
                    waitRaw = nullptr;
                }
                ed::Resume();
            }

            if (m_DraggingPin)
            {
                ImVec2 p1 = pinPos;
                ImVec2 p2 = ImGui::GetMousePos();
                ImVec2 dir = p2 - p1;
                ImVec2 absDelta = dir;
                float length = sqrt(dir.x * dir.x + dir.y * dir.y);
                if (length > 0.f)
                {
                    ImVec2 normDir = ImVec2(dir.x / length, dir.y / length);
                    ImVec2 perpendicular = ImVec2(-normDir.y, normDir.x);
                    float offsetAmount = 5.0f;
                    ImVec2 offset = perpendicular * offsetAmount;

                    ImVec2 p1_offset = p1 + offset;
                    ImVec2 p2_offset = p2 + offset;
                    ImVec2 cp1 = p1_offset + normDir * (length * 0.3f);
                    ImVec2 cp2 = p2_offset - normDir * (length * 0.3f);
                    ImU32 color = IM_COL32(255, 255, 255, 125);
                    float thickness = 3.0f;
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddBezierCubic(p1_offset, cp1, cp2, p2_offset, color, thickness);
                }

                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    s_DragDelta = absDelta;
                }
            }

            if (ImGui::IsItemVisible() && ed::IsNodeSelected(nid))
            {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    node.Position = { prev.x + delta.x, prev.y + delta.y };
                }
            }

            node.PositionEditor = ed::GetNodePosition(nid);
        }

        for (auto& node : graph.NodeList)
        {
            if (BT::IsCompositeNode(node.Type) || BT::IsDecoratorNode(node.Type))
            {
                for (const auto& childId : node.Children)
                {
                    auto childIt = graph.Nodes.find(childId);
                    if (childIt != graph.Nodes.end())
                    {
                        BTBuildNode& child = *childIt->second;
                        ed::Link(ed::LinkId(child.ID.m_ID_Data), node.OutputPinId, child.InputPinId);
                    }
                }
            }
        }

        isfirstLoad = false;

        ed::Suspend();
        if (nodeMenuOpen)
        {
            ImGui::OpenPopup("NodeMenu");
        }
        ed::Resume();

        ed::Suspend();
        if (ImGui::BeginPopup("NodeMenu"))
        {
            if (selectNode)
            {
                if (!selectNode->Name.empty())
                {
                    ImGui::MenuItem(selectNode->Name.c_str(), nullptr, false, false);
                }
                ImGui::Separator();
                if (BT::IsCompositeNode(selectNode->Type) || BT::IsDecoratorNode(selectNode->Type))
                {
                    auto& selectedNodeChildContainer = selectNode->Children;
                    bool isNotAbleAddChild = BT::IsDecoratorNode(selectNode->Type) && 1 < selectedNodeChildContainer.size();

                    if (ImGui::BeginMenu("Add Child", !isNotAbleAddChild))
                    {
                        for (auto& key : graph.GetRegisteredKey())
                        {
                            if (key != "Action" && key != "Condition")
                            {
                                if (ImGui::MenuItem(key.c_str()))
                                {
                                    BehaviorNodeType type = BT::StringToNodeType(key);
                                    Mathf::Vector2 newPos =
                                        BT::ToMathfVec2(s_DragStartNodePos + s_DragDelta);
                                    BTBuildNode* newNode = graph.CreateNode(type, key, newPos);
                                    s_newNodeId = ed::NodeId(newNode->ID.m_ID_Data);
                                    graph.AddChildNode(newNode);
                                }
                            }
                            else if (key == "Action")
                            {
                                if(ImGui::BeginMenu("Action"))
                                {
                                    for (auto& actionName : AIManagers->GetActionNodeNames())
                                    {
                                        if (ImGui::MenuItem(actionName.c_str()))
                                        {
                                            BehaviorNodeType type = BT::StringToNodeType(key);
                                            Mathf::Vector2 newPos =
                                                BT::ToMathfVec2(s_DragStartNodePos + s_DragDelta);
                                            BTBuildNode* newNode = graph.CreateNode(type, key, newPos);
                                            s_newNodeId = ed::NodeId(newNode->ID.m_ID_Data);
                                            graph.AddChildNode(newNode);
                                            
                                            newNode->HasScript = true;
                                            newNode->ScriptName = actionName;
                                        }
                                    }
                                    ImGui::Separator();
                                    if (ImGui::MenuItem("Add Action"))
                                    {
                                        addScriptMenuOpen = true;
                                    }
                                    ImGui::EndMenu();

                                }
                            }
                            else if (key == "Condition")
                            {
                                if (ImGui::BeginMenu("Condition"))
                                {
                                    for (auto& conditionName : AIManagers->GetConditionNodeNames())
                                    {
                                        if (ImGui::MenuItem(conditionName.c_str()))
                                        {
                                            BehaviorNodeType type = BT::StringToNodeType(key);
                                            Mathf::Vector2 newPos =
                                                BT::ToMathfVec2(s_DragStartNodePos + s_DragDelta);
                                            BTBuildNode* newNode = graph.CreateNode(type, key, newPos);
                                            s_newNodeId = ed::NodeId(newNode->ID.m_ID_Data);

                                            newNode->HasScript = true;
                                            newNode->ScriptName = conditionName;
                                        }
                                    }
                                    ImGui::Separator();
                                    if (ImGui::MenuItem("Add Condition"))
                                    {
                                        addScriptMenuOpen = true;
                                    }
                                    ImGui::EndMenu();
                                }
                            }
                            
                        }
                        ImGui::EndMenu();
                    }
                }
                if (selectNode)
                {
                    bool isRoot = selectNode->IsRoot;
                    bool notRoot = !isRoot;
                    if (ImGui::MenuItem("Delete Node", nullptr, false, notRoot))
                    {
                        graph.DeleteNode(selectNode->ID);
                        selectNode = nullptr;
                    }
                }
            }
            ImGui::EndPopup();
        }
        ed::Resume();

        ed::Suspend();
        if (addScriptMenuOpen)
        {
            ImGui::OpenPopup("AddScriptNode");
            addScriptMenuOpen = false;
		}
        ed::Resume();

        ed::Suspend();
        if (ImGui::BeginPopup("AddScriptNode"))
        {
			static char newNodeName[256] = "";
			static const char* scriptNodeTypes[2] = { "Action", "Condition" };
			static int selectedNodeType = 0;
            ImGui::Text("Select Node Type to Add:");
            ImGui::Separator();
			// Show a combo box to select the node type
			ImGui::Combo("Node Type", 
                &selectedNodeType, scriptNodeTypes, IM_ARRAYSIZE(scriptNodeTypes));
			ImGui::SameLine();
			ImGui::InputText("Node Name", newNodeName, IM_ARRAYSIZE(newNodeName));
            
            if (ImGui::Button("Add Script Node"))
            {
                switch (selectedNodeType)
                {
                case 0:
                    ScriptManager->CreateActionNodeScript(newNodeName);
					break;
                case 1:
					ScriptManager->CreateConditionNodeScript(newNodeName);
					break;
                default:
                    break;
                }

                memset(newNodeName, 0, sizeof(newNodeName));
				selectedNodeType = 0; // Reset to the first type
            }

            ImGui::EndPopup();
        }
        ed::Resume();

        ed::End();
        ed::SetCurrentEditor(nullptr);

        ImGui::End();
    }
    else
    {
        if (s_MenuBarBTEditorContext)
        {
            ed::DestroyEditor(s_MenuBarBTEditorContext);
            s_MenuBarBTEditorContext = nullptr;

            file::path BTPath = PathFinder::Relative("BehaviorTree\\" + BTName + ".bt");

            if (!file::exists(BTPath.parent_path()))
            {
                file::create_directories(BTPath.parent_path());
            }

            for (auto& node : graph.NodeList)
            {
                node.Position = BT::ToMathfVec2(node.PositionEditor);
            }

            // Save the graph to a file
            auto node = Meta::Serialize(&graph);

            std::ofstream outFile(BTPath.string());

            if (outFile.is_open())
            {
                outFile << node; // Pretty print with 4 spaces
                outFile.close();
            }
            else
            {
                std::cerr << "Failed to open file for writing: " << BTPath.string() << std::endl;
            }

            graph.Clear();
			BTName.clear();
			m_bShowBehaviorTreeWindow = false;
        }
    }
}

void MenuBarWindow::ShowBlackBoardWindow()
{
    static char newKeyBuffer[128] = "";
    static int selectedTypeIndex = 0;
    static bool showEditor = false;

    if (m_bShowBlackBoardWindow)
    {
        showEditor = true;
    }

    //if(showEditor)
    //{
    //    ImGui::Begin("BlackBoard Editor");
    //    ImGui::InputText("New Key", newKeyBuffer, IM_ARRAYSIZE(newKeyBuffer));
    //    ImGui::SameLine();
    //    if (ImGui::Button("Add Key"))
    //    {
    //        std::string key(newKeyBuffer);
    //        if (!key.empty() && !blackboard.HasKey(key))
    //        {
    //            // 기본 타입으로 생성
    //            blackboard.SetValueAsInt(key, 0);
    //        }
    //        newKeyBuffer[0] = '\0';
    //    }

    //    ImGui::Separator();

    //    // 기존 키 목록
    //    for (auto& [key, value] : blackboard.m_values)
    //    {
    //        ImGui::PushID(key.c_str());

    //        ImGui::Text("Key: %s", key.c_str());
    //        ImGui::SameLine();
    //        if (ImGui::Button("Remove"))
    //        {
    //            blackboard.RemoveKey(key);
    //            ImGui::PopID();
    //            break;
    //        }

    //        // 타입 선택 (변경시 자동 변환은 생략)
    //        int typeIndex = static_cast<int>(value.Type);
    //        if (ImGui::Combo("Type", &typeIndex, "None\0Bool\0Int\0Float\0String\0Vector2\0Vector3\0Vector4\0GameObject\0Transform\0"))
    //        {
    //            value.Type = static_cast<BlackBoardType>(typeIndex);
    //        }

    //        // 값 편집 UI
    //        switch (value.Type)
    //        {
    //        case BlackBoardType::Bool:
    //            ImGui::Checkbox("Value", &value.BoolValue);
    //            break;
    //        case BlackBoardType::Int:
    //            ImGui::InputInt("Value", &value.IntValue);
    //            break;
    //        case BlackBoardType::Float:
    //            ImGui::InputFloat("Value", &value.FloatValue);
    //            break;
    //        case BlackBoardType::String:
    //        case BlackBoardType::GameObject:
    //        case BlackBoardType::Transform:
    //        {
    //            static char buf[256];
    //            strncpy(buf, value.StringValue.c_str(), sizeof(buf));
    //            if (ImGui::InputText("Value", buf, IM_ARRAYSIZE(buf)))
    //            {
    //                value.StringValue = buf;
    //            }
    //            break;
    //        }
    //        case BlackBoardType::Vector2:
    //            ImGui::InputFloat2("Value", &value.Vec2Value.x);
    //            break;
    //        case BlackBoardType::Vector3:
    //            ImGui::InputFloat3("Value", &value.Vec3Value.x);
    //            break;
    //        case BlackBoardType::Vector4:
    //            ImGui::InputFloat4("Value", &value.Vec4Value.x);
    //            break;
    //        default:
    //            ImGui::Text("Unsupported type");
    //            break;
    //        }

    //        ImGui::Separator();
    //        ImGui::PopID();
    //    }

    //    ImGui::End();
    //}
}
#endif // DYNAMICCPP_EXPORTS