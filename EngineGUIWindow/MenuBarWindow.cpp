#include "MenuBarWindow.h"
#include "SceneRenderer.h"
#include "SceneManager.h"
#include "DataSystem.h"
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
                    SceneManagers->SaveScene();
                }
                if (ImGui::MenuItem("Load Scene"))
                {
					SceneManagers->resetSelectedObjectEvent.Broadcast();
                    SceneManagers->LoadScene();
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
    bool isClear = Debug->IsClear();
    ImGui::PushFont(m_koreanFont);

    ImGui::Begin("Log", &m_bShowLogWindow, ImGuiWindowFlags_NoDocking);
    if (ImGui::Button("Clear"))
    {
        Debug->Clear();
    }
    ImGui::SameLine();
    ImGui::Combo("Log Filter", &levelFilter,
        "Trace\0Debug\0Info\0Warning\0Error\0Critical\0\0");

    float sizeX = ImGui::GetContentRegionAvail().x;
    float sizeY = ImGui::CalcTextSize(Debug->GetBackLogMessage().c_str()).y;

    if (isClear)
    {
        Debug->toggleClear();
        ImGui::End();
        ImGui::PopFont();
        return;
    }

    auto entries = Debug->get_entries();
    for (size_t i = 0; i < entries.size(); i++)
    {
        const auto& entry = entries[i];
        bool is_selected = (i == m_selectedLogIndex);

        if (entry.level != spdlog::level::trace && entry.level < levelFilter)
            continue;

        ImVec4 color;
        switch (entry.level)
        {
        case spdlog::level::info: color = ImVec4(1, 1, 1, 1); break;
        case spdlog::level::warn: color = ImVec4(1, 1, 0, 1); break;
        case spdlog::level::err:  color = ImVec4(1, 0.4f, 0.4f, 1); break;
        default: color = ImVec4(0.7f, 0.7f, 0.7f, 1); break;
        }

        if (is_selected)
            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(100, 100, 255, 100));

        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(color));

        std::string wrapped = WordWrapText(entry.message, 120);
        int stringLine = std::count(wrapped.begin(), wrapped.end(), '\n');
        ImGui::PushID(i);
        if (ImGui::Selectable(std::string(ICON_FA_CIRCLE_INFO + std::string(" ") + wrapped).c_str(),
            is_selected, ImGuiSelectableFlags_AllowDoubleClick, { sizeX , float(15 * stringLine) }))
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
        ImGui::PopStyleColor();

        if (is_selected)
            ImGui::PopStyleColor();
        ImGui::PopID();
    }

    ImGui::End();
    ImGui::PopFont();
}
