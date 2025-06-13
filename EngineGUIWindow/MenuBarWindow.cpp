#include "MenuBarWindow.h"
#include "SceneRenderer.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "FileDialog.h"
#include "IconsFontAwesome6.h"
#include "fa.h"


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
                if (ImGui::MenuItem("Save Current Scene"))
                {
                    //Test
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
                    PostQuitMessage(0);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Pipeline Setting"))
                {
                    if (!ImGui::GetContext("RenderPass").IsOpened())
                    {
                        ImGui::GetContext("RenderPass").Open();
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Bake Lightmap"))
            {
                if (ImGui::MenuItem("LightMap Window"))
                {
                    if (!ImGui::GetContext("LightMap").IsOpened())
                    {
                        ImGui::GetContext("LightMap").Open();
                    }
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
            if (ImGui::Button(ICON_FA_FOLDER_TREE " Content Drawer"))
            {
                auto& contentDrawerContext = ImGui::GetContext(ICON_FA_FOLDER_OPEN " Content Browser");
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
            if (ImGui::Button(ICON_FA_CODE " Output Log "))
            {
                m_bShowLogWindow = true;
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();
    }

    if (m_bShowLogWindow)
    {
        ShowLogWindow();
    }
}

void MenuBarWindow::ShowLogWindow()
{
    static int levelFilter = spdlog::level::trace;
    static bool autoScroll = true;
    bool isClear = Debug->IsClear();

    ImGui::PushFont(m_koreanFont);
    ImGui::Begin("Log", &m_bShowLogWindow, ImGuiWindowFlags_NoDocking);
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
            case spdlog::level::info:  color = ImVec4(1, 1, 1, 1); break;
            case spdlog::level::warn:  color = ImVec4(1, 1, 0, 1); break;
            case spdlog::level::err:   color = ImVec4(1, 0.4f, 0.4f, 1); break;
            case spdlog::level::critical: color = ImVec4(1, 0, 0, 1); break;
            default:                   color = ImVec4(0.7f, 0.7f, 0.7f, 1); break;
            }

            if (is_selected)
                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(100, 100, 255, 100));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(color));

            std::string wrapped = WordWrapText(entry.message, 120);
            int stringLine = std::count(wrapped.begin(), wrapped.end(), '\n');

            ImGui::PushID(i);
            if (ImGui::Selectable((ICON_FA_CIRCLE_INFO + std::string(" ") + wrapped).c_str(),
                is_selected, ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(sizeX, float(15 * stringLine))))
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
