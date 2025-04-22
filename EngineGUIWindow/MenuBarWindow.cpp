#include "MenuBarWindow.h"
#include "SceneRenderer.h"
#include "SceneManager.h"
#include "DataSystem.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

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

            ImGui::SetCursorPos(ImVec2((availRegion * 0.5f) + 100.f, 0));

            if (ImGui::Button(SceneManagers->m_isGameStart ? ICON_FA_STOP : ICON_FA_PLAY))
            {
				SceneManagers->m_isGameStart = !SceneManagers->m_isGameStart;
            }

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
                m_sceneRenderer->m_bShowLogWindow = true;
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();
    }
}
