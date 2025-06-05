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

    static bool m_bShowContentsBrowser = false;
    if (ImGui::BeginViewportSideBar("##MainStatusBar", viewport, ImGuiDir_Down, height + 1, window_flags)) {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::Button(ICON_FA_FOLDER_TREE " Content Drawer"))
            {
                m_bShowContentsBrowser = !m_bShowContentsBrowser;
            }

            if (m_bShowContentsBrowser)
            {
                DataSystems->OpenContentsBrowser();
            }
            else
            {
                DataSystems->CloseContentsBrowser();
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
