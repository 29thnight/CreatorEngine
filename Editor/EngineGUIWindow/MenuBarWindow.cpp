#include "ImGui.h"
#include "ClrHost.h"
#include "EditorImGuiTexture.h"
#include "MenuBarWindow.h"
#include "RHI/IRHIDeviceResources.h"
#include "SceneManager.h"
// SceneManager.h는 Scene을 전방 선언만 한다. 여기서는 m_sceneName을 읽으므로
// 완전한 형이 필요하고, PhysicsManagers도 직접 받는다.
// 유니티 빌드에서는 같은 블롭의 앞선 파일이 둘 다 공급했다.
#include "Scene.h"
#include "PhysicsManager.h"
#include "DataSystem.h"
#include "FileDialog.h"
#include "ProfilerHUD.h"
#include "CoreWindow.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "Prefab.h"
#include "PrefabUtility.h"
#include "AIManager.h"
#include "BTBuildGraphAuthoring.h"
#include "BTEditorBridge.h"
#include "AuthoringParsedDocument.h"
#include "ReflectionUndo.h"
#include "BlackBoard.h"
#include "InputActionManager.h"
#include "TagManager.h"
#include "EditorMenuDraw.h"
#include "EditorMenuTargets.h"
#include "EditorSettingsStore.h"
#include "EditorPlatform.h"
#include "RuntimeSettings.h"
#include "EnhancedGizmoSceneBinding.h"
#include "ToggleUI.h"
#include "GameBuilderSystem.h"
#include "EditorRenderer.h"
#include "EditorWindowChrome.h"
#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "Windows/EditorStandardWindows.h"
#include "Windows/EditorToolboxWindows.h"
#include "Core.Definition.h"
#include <regex>

constexpr int MAX_LAYER_SIZE = 32;

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

MenuBarWindow::MenuBarWindow()
{
    ImGuiIO& io = ImGui::GetIO();
    // 범위가 FA6 의 것인지 컴파일 시점에 못 박는다 — 이유는
    // `EditorRenderer::AddEditorFonts` 의 같은 단정에 적혀 있다.
    static_assert(0xe005 == ICON_MIN_FA && 0xf8ff == ICON_MAX_FA,
        "아이콘 범위가 FA6 의 값이 아니다. 같은 유니티 blob 안의 TU 가 "
        "IconsFontAwesome4.h 를 들였을 가능성이 높다");

    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    ImFontConfig font_config;
    icons_config.MergeMode = true; // Merge icon font to the previous font if you want to have both icons and text
    // 1.92 의 아틀라스는 동적이다 — 글리프는 그릴 때 구워지므로 한글 범위를
    // 미리 못 박을 필요도, `Build()` 를 부를 필요도 없다. 범위를 못 박던 옛
    // 코드는 그 밖의 글자를 네모로 만들었다.
    m_koreanFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\malgun.ttf", 16.0f);
    io.Fonts->AddFontFromMemoryCompressedTTF(FA_compressed_data, FA_compressed_size, 16.0f, &icons_config, icons_ranges);

    // PHASE 21 M4 2단계: 프레임은 셸이 연다. 처음 닫혀 있다는 사실은
    // 선언이 든다(open_by_default(false)) — 여기서 다시 닫지 않는다.
    editor::windows::bind_window_body(EditorWindowName::kLightMap, [&]() {
        ImGui::TextUnformatted("Legacy DX11 LightMap editor is unavailable in Enhanced-only mode.");
        ImGui::TextUnformatted("A DX12 bake path must be wired before this tool is re-enabled.");
    });


    editor::windows::bind_window_body(EditorWindowName::kFrameProfiler,
        [this]() { ShowProfilerWindow(); });
    editor::windows::bind_window_body(EditorWindowName::kOutputLog,
        [this]() { ShowLogWindow(); });
    editor::windows::bind_window_body(EditorWindowName::kAbout,
        [this]() { ShowAboutWindow(); });
    editor::windows::bind_window_body(EditorWindowName::kBehaviorTree,
        [this]() { DrawBehaviorTreeWindow(); });
    editor::windows::bind_window_body(EditorWindowName::kBlackBoard,
        [this]() { DrawBlackBoardWindow(); });
    editor::windows::bind_window_body(EditorWindowName::kInputActionMaps,
        [this]() { SHowInputActionMap(); });
    editor::windows::bind_window_body(EditorWindowName::kBuildSceneSetting,
        [this]() { ShowBuildSceneSettingWindow(); });
    editor::windows::bind_window_body(EditorWindowName::kRenderPassDebug,
        [this]() { ShowRenderDebugWindow(); });

    editor::windows::bind_window_body(EditorWindowName::kCollisionMatrix, [&]() 
    {
        const auto& layers = TagManager::GetInstance()->GetLayers();
        const int layerCount = static_cast<int>(layers.size());
        const int matrixSize = std::min(layerCount, 32); // 최대 32개 제한
        const float checkboxSize = ImGui::GetFrameHeight();
        const float cellSize = checkboxSize;

        ImGui::Text("Collision Matrix");
        ImGui::Separator();
        //todo::grid matrix
        if(collisionMatrix.empty()){
            collisionMatrix = PhysicsManagers->GetCollisionMatrix();
        }
        if (ImGui::BeginChild("CollisionMatrix", ImVec2(0, 0), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_TableAngledHeadersAngle, 0.5f); // 기울기 설정
            ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(1, 1, 1, 0));
            ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(1, 1, 1, 0));

            const ImGuiTableFlags tableFlags =
                ImGuiTableFlags_SizingFixedFit |
                ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                ImGuiTableFlags_Hideable;

            if (ImGui::BeginTable("CollisionMatrixTable", matrixSize + 1, tableFlags))
            {
                // -------------------------
                // 1. TableSetupColumn 설정
                // -------------------------
                ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_NoHeaderLabel); // 좌측 인덱스용
                for (int col = 0; col < matrixSize; ++col)
                {
                    ImGui::TableSetupColumn(
                        layers[col].c_str(),
                        ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_NoHide
                    );
                }

                // -------------------------
                // 2. 헤더 렌더링
                // -------------------------
                ImGui::TableAngledHeadersRow(); // 대각선 헤더 출력

                // -------------------------
                // 3. 본문 렌더링
                // -------------------------
                for (int row = 0; row < matrixSize; ++row)
                {
                    ImGui::TableNextRow();
                    for (int col = -1; col < matrixSize; ++col)
                    {
                        ImGui::TableNextColumn();
                        if (col == -1)
                        {
                            // 행 인덱스 이름 출력
                            ImGui::TextUnformatted(layers[row].c_str());
                        }
                        else
                        {
                            ImGui::PushID(row * MAX_LAYER_SIZE + col);
                            if (row <= col)
                            {
                                bool value = collisionMatrix[row][col] != 0;
                                if (ImGui::Checkbox("##chk", &value))
                                {
                                    collisionMatrix[row][col] = (uint8_t)value;
                                    collisionMatrix[col][row] = (uint8_t)value; // 대칭
                                }
                            }
                            else
                            {
                                ImGui::Dummy(ImVec2(cellSize, checkboxSize));
                            }
                            ImGui::PopID();
                        }
                    }
                }

                ImGui::EndTable();
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            ImGui::EndChild();
        }


        ImGui::Separator();
        if (ImGui::Button("Save"))
        {
            //적용된 충돌 매스릭스 저장
            PhysicsManagers->SetCollisionMatrix(collisionMatrix);
			PhysicsManagers->SaveCollisionMatrix();
            editor::close_window(EditorWindowName::kCollisionMatrix);
        }
		ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
			PhysicsManagers->LoadCollisionMatrix();
			collisionMatrix = PhysicsManagers->GetCollisionMatrix();
            editor::close_window(EditorWindowName::kCollisionMatrix);
		}
        
    });
   
}

void MenuBarWindow::RenderMenuBar()
{
    // 선택 문맥은 프레임당 한 번 유도한다. 선언 항목 중 선택 대상 서명을 가진
    // 것들이 이것으로 활성·비활성이 갈린다(서명이 결속을 선언한다 — A.4).
    const std::optional<::editor::entity_target> selection =
        ::editor::targets::selected_entity();
    const ::editor::entity_target* selectionPtr = selection ? &*selection : nullptr;

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
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    SceneManagers->resetSelectedObjectEvent.Broadcast();
                    std::string sceneName = SceneManagers->GetActiveScene()->m_sceneName.ToString();
					file::path fileName = PathFinder::Relative("Scenes\\" + sceneName + ".creator").wstring();

                    if (file::exists(fileName))
                    {
                        SceneManagers->SaveScene(fileName.string());
                    }
                    else 
                    {
                        fileName = ShowSaveFileDialog(
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
                }
                if (ImGui::MenuItem("Save As", "Ctrl+Shift+S"))
                {
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
					file::path fileName = ShowOpenFileDialog(
						L"Scene Files (*.creator)\0*.creator\0",
						L"Load Scene",
						PathFinder::Relative("Scenes\\").wstring()
					);
                    if (!fileName.empty())
                    {
                        SceneManagers->LoadSceneImmediate(fileName.string());
                    }
                    else
                    {
                        Debug->LogError("Failed to load scene.");
                    }

                }
                ImGui::Separator();
                if (ImGui::MenuItem("GameBuild"))
                {
					std::wstring startupSceneName =
						EditorSettingsStore::Get().Build().GetStartupSceneName();
                    if(!startupSceneName.empty())
                    {
						GameBuilderSystem::GetInstance()->Initialize();
                        GameBuilderSystem::GetInstance()->BuildGame();
                    }
                }
                if (ImGui::MenuItem("Exit"))
                {
                    // Exit action
					//
					// 창 핸들은 CoreWindow 싱글턴에서 온다. 예전에는 DeviceResources를
					// 거쳤는데, 창을 디바이스가 들고 있을 이유가 없었고 그 경유가
					// DeviceResources::GetActive의 마지막 소비자였다(2026-08-10).
					if (auto* window = CoreWindow::GetForCurrentInstance())
					{
						PostMessage(window->GetHandle(), WM_CLOSE, 0, 0);
					}
                }
                ::editor::append_top_menu_items(::editor::top_menu_root::file, selectionPtr);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("LightMap Window"))
                {
                    if (!editor::is_window_open(EditorWindowName::kLightMap))
                    {
                        editor::open_window(EditorWindowName::kLightMap);
                    }
                }

                if (ImGui::MenuItem("Effect Editor"))
                {
                    // "EffectEdit" 은 이 저장소 어디에서도 등록된 적이 없다.
                    // 옛 GetContext 는 operator[] 라 이 줄이 빈 창 하나를
                    // 순회에 영구히 만들어 넣었다. 이제 없는 이름은 없는
                    // 것으로 답하므로 이 메뉴 항목은 아무 일도 하지 않는다 —
                    // 이펙트 편집기를 실제로 붙이는 것은 별건이다.
                    if (editor::is_window_open("EffectEdit"))
                    {
                        editor::close_window("EffectEdit");
                    }
                    else
                    {
                        editor::open_window("EffectEdit");
                    }
                }

                if( ImGui::MenuItem("Behavior Tree Editor"))
                {
                    editor::open_window(EditorWindowName::kBehaviorTree);
				}

                if (ImGui::MenuItem("Blackboard Editor"))
                {
					editor::open_window(EditorWindowName::kBlackBoard);
				}

                if (ImGui::MenuItem("InputAction Maps"))
                {
                    editor::open_window(EditorWindowName::kInputActionMaps);
                }

                ::editor::append_top_menu_items(::editor::top_menu_root::edit, selectionPtr);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings"))
            {
                if (ImGui::MenuItem("Pipeline Setting"))
                {
                    if (!editor::is_window_open(EditorWindowName::kRenderPass))
                    {
                        editor::open_window(EditorWindowName::kRenderPass);
                    }
                }

                if (ImGui::MenuItem("Collision Matrix"))
                {
                    editor::open_window(EditorWindowName::kCollisionMatrix);
                }

                if (ImGui::MenuItem("Build Settings"))
                {
                    editor::open_window(EditorWindowName::kBuildSceneSetting);
				}

                if (ImGui::MenuItem("Render Debug"))
                {
                    editor::open_window(EditorWindowName::kRenderPassDebug);
				}

                if (ImGui::MenuItem("Resource Counter"))
                {
                    if (!editor::is_window_open(EditorWindowName::kResourceCounter))
                    {
                        editor::open_window(EditorWindowName::kResourceCounter);
                    }
                }

                // 밝은 팝업 배경(0.95 회색) 위에서 읽히게 하려고 항목마다
                // 검은 글씨와 밝은 입력란 색을 눌러 담던 자리였다. 팝업이
                // 다크 스킨을 따라가므로 그 우회로가 전부 필요 없어졌다.
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

                EditorPreferences& preferences = EditorSettingsStore::Get().Preferences();
                float imguiScale = preferences.GetImGuiScale();
                if (ImGui::DragFloat("ImGuiScale", &imguiScale, 0.1f, 0.8f, 1.5f))
                {
                    preferences.SetImGuiScale(imguiScale);
                    EditorSettingsStore::Get().Save();
                }
                ImGui::PopStyleVar(2);
                ::editor::append_top_menu_items(::editor::top_menu_root::settings, selectionPtr);
                ImGui::EndMenu();
            }

            // Tools — 인라인 메뉴가 없는 새 뿌리다. 선언 항목이 0 이면 BeginMenu
            // 자체를 부르지 않으므로 이 줄만으로는 화면이 달라지지 않는다(A.6).
            ::editor::draw_top_menu_root(::editor::top_menu_root::tools, selectionPtr);

            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("Reset Layout"))
                {
                    // imgui.ini에 재생성 경로가 없어서 배치가 한 번 어긋나면
                    // 파일을 손으로 지우는 것이 유일한 복구였다.
                    EditorRenderer::RequestDockLayoutReset();
                }
                ImGui::Separator();

                // 이름은 EditorWindowName이 정본이다. GetContext는 operator[]라
                // 오타 하나가 그려지지 않는 유령 창을 표에 영구히 꽂는다.
                const char* const panels[] = {
                    EditorWindowName::kHierarchy,
                    EditorWindowName::kInspector,
                    EditorWindowName::kContentBrowser,
                    EditorWindowName::kAssetBundle,
                    EditorWindowName::kResourceCounter,
                    EditorWindowName::kRenderPass,
                };
                // 여닫기는 **안정 식별자**로 하고 그리기는 **선언의 라벨**로
                // 한다. 둘이 갈린 창이 생겼기 때문이다(Content Browser). 라벨을
                // 여기서 다시 적으면 선언과 메뉴가 따로 놀 자리가 생기므로
                // 표에서 읽는다 — 선언되지 않은 이름이면 식별자를 그려 그
                // 사실이 화면에 드러나게 둔다.
                for (const char* const panel : panels)
                {
                    const ::editor::window_entry* const entry =
                        ::editor::find_window_of(panel);
                    const std::string label =
                        (entry && !entry->label.empty()) ? std::string{ entry->label }
                                                         : std::string{ panel };

                    const bool opened = editor::is_window_open(panel);
                    if (!ImGui::MenuItem(label.c_str(), nullptr, opened)) continue;
                    if (opened) editor::close_window(panel);
                    else        editor::open_window(panel);
                }

                ImGui::Separator();
                // 표시 상태는 이제 표 항목이 든다. bool 주소를 넘기던 자리는
                // 값을 읽어 넘기고, 눌리면 표를 고친다. 메뉴 문구는 바꾸지
                // 않았다 — 이관은 프레임만 옮긴다.
                const struct { const char* label; const char* window; } toggles[] = {
                    { ICON_FA_TERMINAL " Output Log",     EditorWindowName::kOutputLog },
                    { ICON_FA_CHART_GANTT " Frame Profiler", EditorWindowName::kFrameProfiler },
                };
                for (const auto& toggle : toggles)
                {
                    const bool opened = editor::is_window_open(toggle.window);
                    if (!ImGui::MenuItem(toggle.label, nullptr, opened)) continue;
                    if (opened) editor::close_window(toggle.window);
                    else        editor::open_window(toggle.window);
                }
                ::editor::append_top_menu_items(::editor::top_menu_root::window, selectionPtr);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About Creator Engine"))
                {
                    editor::open_window(EditorWindowName::kAbout);
                }
                ::editor::append_top_menu_items(::editor::top_menu_root::help, selectionPtr);
                ImGui::EndMenu();
            }

            // 재생 컨트롤과 스타일 토글이 여기 있었다. s&box 배치에서 이 행은
            // 제목표시줄이고 가운데는 창 제목이 쓴다 — 조작 버튼은 RenderToolBar로
            // 내렸다. 그러지 않으면 제목과 버튼이 같은 자리를 다툰다.
            EditorWindowChrome::Get().DrawTitleBarTail();

            ImGui::EndMainMenuBar();
        }
        ImGui::End();
    }

    RenderToolBar();

    if (ImGui::BeginViewportSideBar("##MainStatusBar", viewport, ImGuiDir_Down, height + 1, window_flags)) {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::Button(ICON_FA_TERMINAL " Output Log "))
            {
                if (editor::is_window_open(EditorWindowName::kOutputLog))
                    editor::close_window(EditorWindowName::kOutputLog);
                else
                    editor::open_window(EditorWindowName::kOutputLog);
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_CHART_GANTT " ProfileFrame "))
            {
                if (editor::is_window_open(EditorWindowName::kFrameProfiler))
                    editor::close_window(EditorWindowName::kFrameProfiler);
                else
                    editor::open_window(EditorWindowName::kFrameProfiler);
            }

            {
                const char* kIconBtn = ICON_FA_CUBES_STACKED " LiveCode ";   // 새로 추가할 앞쪽 아이콘 버튼
                const char* kMainLbl = ICON_FA_BUG;   // 기존 디버그 버튼

                const ImGuiStyle& style = ImGui::GetStyle();

                // 아이콘 전용 버튼은 정사각 사이즈로: 텍스트(아이콘) + 패딩*2
                ImVec2 iconTxt = ImGui::CalcTextSize(kIconBtn);
                ImVec2 iconBtn = { iconTxt.x + style.FramePadding.x * 2.0f,
                                   iconTxt.y + style.FramePadding.y * 2.0f };

                // 메인 버튼
                ImVec2 mainTxt = ImGui::CalcTextSize(kMainLbl);
                ImVec2 mainBtn = { mainTxt.x + style.FramePadding.x * 2.0f,
                                   mainTxt.y + style.FramePadding.y * 2.0f };

                // 두 버튼 사이 간격
                float gap = style.ItemInnerSpacing.x;

                // 두 버튼을 합친 총 너비
                float groupWidth = iconBtn.x + gap + mainBtn.x;

                // 그룹을 오른쪽 정렬
                float avail = ImGui::GetContentRegionAvail().x;
                if (avail > groupWidth) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - groupWidth));
                }
                else {
                    ImGui::SameLine();
                }

                bool isGameRunning = SceneManagers->IsGameStart();
                if (isGameRunning)
                {
                    ImGui::BeginDisabled(true);
                }

                // 1) 아이콘 전용 버튼
                // C++ 핫리로드 은퇴(9-4): 컴파일 버튼은 자리만 유지한다(비활성 동작 없음).
                ImGui::BeginDisabled(true);
                ImGui::Button(kIconBtn, iconBtn);
                ImGui::EndDisabled();

                if (isGameRunning)
                {
                    ImGui::EndDisabled();
                }

                // 같은 라인에 메인 버튼 배치
                ImGui::SameLine(0.0f, gap);

                // 2) 메인 디버그 버튼 (기존 로직)
                bool wasDebug = ShouldCollectGizmoColliders();
                if (wasDebug) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }

                if (ImGui::Button(kMainLbl, mainBtn)) {
                    SetCollectGizmoColliders(!wasDebug);
                }

                if (wasDebug) ImGui::PopStyleColor(3);

                //const char* kDebugLabel = ICON_FA_BUG;
                //ImVec2 text = ImGui::CalcTextSize(kDebugLabel);
                //const ImGuiStyle& style = ImGui::GetStyle();
                //ImVec2 btn = { text.x + style.FramePadding.x * 2.0f,
                //               text.y + style.FramePadding.y * 2.0f };

                //// 남은 폭(avail)만큼 오른쪽으로 이동하되, 버튼 너비만큼 빼서 오른쪽 끝에 정렬
                //float avail = ImGui::GetContentRegionAvail().x;
                //if (avail > btn.x) {
                //    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - btn.x));
                //}
                //else {
                //    ImGui::SameLine(); // 공간이 없으면 같은 라인에라도 붙이기
                //}

                //bool wasDebug = ShouldCollectGizmoColliders();

                //// 활성화 색상 토글(선택)
                //if (wasDebug) {
                //    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
                //    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                //    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                //}

                //if (ImGui::Button(kDebugLabel, btn)) {
                //    SetCollectGizmoColliders(!wasDebug);
                //}

                //if (wasDebug) ImGui::PopStyleColor(3);
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();
    }

    // PHASE 21 M4 3단계: 여덟 창의 프레임은 셸이 연다. 여기서 그것들을
    // 부르던 여덟 줄이 사라졌다. 아래 둘만 남는데, 창이 **닫혀 있을 때**
    // 돌아야 하는 정리 경로라서다 — 편집 문맥 파괴와 상태 비우기는 본문이
    // 아니므로 셸이 프레임을 열지 않는 프레임에도 돌아야 한다.
    ShowBehaviorTreeWindow();
    ShowBlackBoardWindow();

    if (m_bShowNewScenePopup)
    {
        ImGui::OpenPopup("NewScenePopup");
        m_bShowNewScenePopup = false;
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

	bool isPressedControl = InputManagement->IsKeyPressed((size_t)KeyBoard::LeftControl);
	bool isPressedShift = InputManagement->IsKeyPressed((size_t)KeyBoard::LeftShift);
	bool isDownS = ImGui::IsKeyPressed(ImGuiKey_S, false);

    // Ctrl + Shift + S : Save As
    if (isPressedControl &&
        isPressedShift &&
        isDownS)
    {
        // 기존 Save As 로직 호출
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
    else if (isPressedControl && isDownS)
    {
        // 기존 Save 로직 호출
        SceneManagers->resetSelectedObjectEvent.Broadcast();
        std::string sceneName = SceneManagers->GetActiveScene()->m_sceneName.ToString();
        file::path fileName = PathFinder::Relative("Scenes\\" + sceneName + ".creator").wstring();

        if (file::exists(fileName))
        {
            SceneManagers->SaveScene(fileName.string());
        }
        else
        {
            fileName = ShowSaveFileDialog(
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
    }
}

void MenuBarWindow::RenderToolBar()
{
    // 제목표시줄 바로 아래 한 줄. 재생 컨트롤이 메뉴 행에 있었을 때는
    // 가운데 위치를 availRegion * 0.5 + 100 이라는 고정 오프셋으로 잡았고,
    // 창 폭이나 메뉴 개수가 바뀌면 그대로 어긋났다. 여기서는 버튼 묶음의
    // 실제 폭을 재서 가운데를 잡는다.
    ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    const float height = ImGui::GetFrameHeight();

    if (!ImGui::BeginViewportSideBar("##MainToolBar", viewport, ImGuiDir_Up, height, flags))
        return;

    if (ImGui::BeginMenuBar())
    {
        const bool isGameRunning = SceneManagers->IsGameStart();
        const bool canPause = isGameRunning;
        const bool isPaused = SceneManagers->IsGamePaused();

        const char* const playIcon = isGameRunning ? ICON_FA_STOP : ICON_FA_PLAY;
        const char* const pauseIcon = isPaused ? ICON_FA_PLAY : ICON_FA_PAUSE;

        const ImGuiStyle& style = ImGui::GetStyle();
        const float playWidth =
            ImGui::CalcTextSize(playIcon).x + style.FramePadding.x * 2.0f;
        const float pauseWidth =
            ImGui::CalcTextSize(pauseIcon).x + style.FramePadding.x * 2.0f;
        const float groupWidth = playWidth + pauseWidth + style.ItemSpacing.x;

        const float rowWidth = ImGui::GetWindowWidth();
        ImGui::SetCursorPosX((rowWidth - groupWidth) * 0.5f);

        if (ImGui::Button(playIcon))
        {
            // ★ LC6(§9): Undo 정책은 Editor::PlayModeController 가 소유한다.
            //   이 버튼이 ClearGameMode 와 m_isGameMode 대입을 직접 하던 시절에는
            //   그 둘이 버튼을 누른 경우에만 일어나 CLI 재생과 스택이 갈렸다.
            SceneManagers->SetGameStart(!isGameRunning);
        }

        ImGui::BeginDisabled(!canPause);
        if (canPause && isPaused)
        {
            const ImVec4 active = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
            ImGui::PushStyleColor(ImGuiCol_Button, active);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
        }
        if (ImGui::Button(pauseIcon))
        {
            SceneManagers->ToggleGamePaused();
        }
        if (canPause && isPaused)
        {
            ImGui::PopStyleColor(3);
        }
        ImGui::EndDisabled();

        // Content Browser 표시 스타일을 가르던 ToggleSwitch 가 여기 있었다.
        // 스타일이 하나가 되면서 스위치도 걷혔다.

        ImGui::EndMenuBar();
    }
    ImGui::End();
}

void MenuBarWindow::ShowAboutWindow()
{
    // 버전과 실행 중인 백엔드가 제목표시줄에 문자열로 붙어 있었다. 초당
    // 몇 번씩 갱신되는 자리에 두면 정작 읽을 때 잘려 있어서, 읽고 싶을 때
    // 여는 이 창으로 옮겼다.
    const BuildSettings& buildSettings = EditorSettingsStore::Get().Build();

    ImGui::TextUnformatted("Creator Engine");
    ImGui::Separator();

    const auto row = [](const char* label, const char* value)
    {
        ImGui::TextDisabled("%-24s", label);
        ImGui::SameLine();
        ImGui::TextUnformatted(value);
    };

    row("Engine version", ENGINE_VERSION);
    row("Project", buildSettings.GetProjectName().c_str());
    row("Dear ImGui", IMGUI_VERSION);
#if defined(_DEBUG)
    row("Editor configuration", "Debug");
#else
    row("Editor configuration", "Release");
#endif

    ImGui::Separator();
    // 에디터가 실제로 돌고 있는 백엔드와 Player가 받을 백엔드를 나란히 둔다.
    // 하나만 보이면 "설정을 바꿨는데 왜 그대로냐"를 가릴 수 없다.
    row("Editor render backend",
        RenderBackendName(RuntimeSettings::Get().GetRenderBackend()));
    row("Player build backend",
        RenderBackendName(buildSettings.GetRenderBackend()));
    ImGui::TextDisabled(
        "The Editor host is fixed; only the Player build backend is configurable\n"
        "(Settings > Build Settings).");

    ImGui::Separator();
    if (ImGui::Button("Close")) editor::close_window(EditorWindowName::kAbout);
}

void MenuBarWindow::ShowLogWindow()
{
    static int levelFilter = spdlog::level::trace;
    static bool autoScroll = true;
    bool isClear = Debug->IsClear();

    // 폰트 밀기는 본문에 남는다. 옛 코드는 `Begin` 앞에서 밀어 제목표시줄까지
    // 덮었지만 제목이 ASCII 라 보이는 차이가 없다.
    ImGui::PushFont(m_koreanFont, 0.0f);

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
            ImGui::PopFont();
            return;
        }

        auto entries = Debug->get_entries();
        float sizeX = ImGui::GetContentRegionAvail().x;
		static bool isCopyPopupOpen = false;
		static std::string copiedText;
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
			EditorPlatform::Get().OpenFile(fileDirectory);
                    }
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                isCopyPopupOpen = true;
                copiedText = entry.message;
            }
            ImGui::PopID();
            ImGui::PopStyleColor();
            if (is_selected)
                ImGui::PopStyleColor();
        }

        if (shouldScroll)
            ImGui::SetScrollHereY(1.0f);

        if (isCopyPopupOpen)
        {
            ImGui::OpenPopup("CopyLogPopup");
			isCopyPopupOpen = false;
        }

        if (ImGui::BeginPopup("CopyLogPopup"))
        {
            ImGui::Text("Copy Log Text");
            ImGui::Separator();
            if (ImGui::Button("Copy"))
            {
                ImGui::SetClipboardText(copiedText.c_str());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
		}
    }
    ImGui::EndChild();
    ImGui::PopFont();
}

ed::EditorContext* s_MenuBarBTEditorContext{ nullptr };

// 창이 닫혀 있을 때 돌아야 하는 정리 경로다. 프레임 루프가 매 프레임 부른다.
void MenuBarWindow::ShowBehaviorTreeWindow()
{
    if (!editor::is_window_open(EditorWindowName::kBehaviorTree))
    {
        BehaviorTreeWindow(false);
    }
}

// 셸이 창 본문으로 부른다.
void MenuBarWindow::DrawBehaviorTreeWindow()
{
    BehaviorTreeWindow(true);
}

// 두 경로가 같은 함수 지역 static 을 나눠 쓰므로 하나로 둔다 — 쪼개면
// 그래프와 편집기 문맥이 서로 다른 저장소가 된다.
void MenuBarWindow::BehaviorTreeWindow(bool drawing)
{
    static BTBuildGraph graph;
    // 편집기 화면 좌표 — 저작 데이터(BTBuildNode) 밖의 편집기 세션 상태다(E3-5).
    static std::unordered_map<HashedGuid, ImVec2> s_nodeScreenPos;
    static bool isfirstLoad = false;
	static std::string BTName;

    if (drawing)
    {
        if (ImGui::Button("Create"))
        {
            isfirstLoad = true;
            file::path BTSavePath = ShowSaveFileDialog(
                L"Behavior Tree Files (*.bt)\0*.bt\0", 
                L"Save Behavior Tree Asset",
                PathFinder::Relative("BehaviorTree"));

            if (!BTSavePath.empty())
            {
                BTName = BTSavePath.stem().string();
                if (!file::exists(BTSavePath.parent_path()))
                {
                    file::create_directories(BTSavePath.parent_path());
                }
                graph.Clear();
                s_nodeScreenPos.clear();

                for (auto& node : graph.NodeList)
                {
                    node.Position = BTEd::ToMathfVec2(s_nodeScreenPos[node.ID]);
                }

                // Save the graph to a file
				Authoring::WriteDocument document = Meta::SerializeDocument(&graph);

                std::ofstream outFile(BTSavePath.string(), std::ios::binary | std::ios::trunc);

                if (outFile.is_open())
                {
					outFile << document.Dump();
                    outFile.close();
                    outFile.flush();
                }
                else
                {
                    std::cerr << "Failed to open file for writing: " << BTSavePath.string() << std::endl;
                }
            }
            else
            {
				Debug->LogError("Failed to create Behavior Tree.");
            }
        }
		ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            isfirstLoad = true;
            file::path fileName = ShowOpenFileDialog(
                L"Behavior Tree Files (*.bt)\0*.bt\0",
                L"Load Behavior Tree",
                PathFinder::Relative("BehaviorTree").wstring()
            );

            if (!fileName.empty())
            {
                BTName = fileName.stem().string();
				if (file::exists(fileName))
				{
					graph.CleanUp();
					s_nodeScreenPos.clear();
					std::string parseError;
					const Authoring::ParsedDocument document =
						Authoring::ParsedDocument::ParseFile(
							fileName.string(), parseError);
					if (!document)
					{
						Debug->LogError("Behavior Tree parse failed: " + parseError);
					}
					else
					{
						const Authoring::ReadNode node = document.Root();
						const Authoring::ReadNode nodeList = node["NodeList"];
						if (nodeList && nodeList.IsSequence())
						{
							for (const Authoring::ReadNode buildNode : nodeList)
							{
								BTBuildGraphAuthoring::DeserializeSingleNode(
									graph, buildNode);
							}
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
            isfirstLoad = true;
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
                node.Position = BTEd::ToMathfVec2(s_nodeScreenPos[node.ID]);
            }

            // Save the graph to a file
			Authoring::WriteDocument document = Meta::SerializeDocument(&graph);

            std::ofstream outFile(BTPath.string(), std::ios::binary | std::ios::trunc);

            if (outFile.is_open())
            {
				outFile << document.Dump();
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
            ImVec2 prev = BTEd::ToImVec2(node.Position);

            ed::PinId inPin = BTEd::InputPin(node.ID);
            ed::PinId outPin = BTEd::OutputPin(node.ID);
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

            constexpr int dummy_x = 180;
            ImRect inputsRect;
            int inputAlpha = 200;
            if (!node.IsRoot)
            {
                ImGui::Dummy(ImVec2(dummy_x, padding));
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
                ImGui::Dummy(ImVec2(dummy_x, padding));

            ImGui::Dummy(ImVec2(dummy_x, 10));
            ImRect contentScriptRect;
            ImVec2 textScriptSize;
            ImVec2 textScriptPos;
            bool isScriptNode = node.HasScript;
            if (isScriptNode)
            {
                if (ClrHost::Get().HasBTNodeType(node.ScriptName))
                {
                    ImGui::Dummy(ImVec2(dummy_x, 40));
                    contentScriptRect = ImRect(ImGui::GetItemRectMin() + insidePadding, ImGui::GetItemRectMax() - insidePadding);
                    textScriptSize = ImGui::CalcTextSize(node.ScriptName.c_str());
                    textScriptPos = ImVec2(contentScriptRect.GetTL().x + ((dummy_x - 8) - textScriptSize.x) / 2, contentScriptRect.GetTL().y + (40 - textScriptSize.y) / 2);
                    ImGui::GetWindowDrawList()->AddText(textScriptPos, IM_COL32_WHITE, node.ScriptName.c_str());
                }
            }

            ImGui::Dummy(ImVec2(dummy_x, 40));
            ImRect contentRect(ImGui::GetItemRectMin() + insidePadding, ImGui::GetItemRectMax() - insidePadding);
            ImVec2 textSize = ImGui::CalcTextSize(node.Name.c_str());
            ImVec2 textPos = ImVec2(contentRect.GetTL().x + ((dummy_x - 8) - textSize.x) / 2, contentRect.GetTL().y + (40 - textSize.y) / 2);
            ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32_WHITE, node.Name.c_str());

            if (node.Type == BehaviorNodeType::WeightedSelector)
            {
                // Ensure ChildWeights has the same size as Children.
                // This is a safeguard; the main logic should be in link creation/deletion.
                if (node.ChildWeights.size() != node.Children.size())
                {
                    node.ChildWeights.resize(node.Children.size(), 1.0f);
                }

                // Set a smaller width for the input boxes
                ImGui::PushItemWidth(50.0f);

                // Draw an InputFloat for each child weight
                for (size_t i = 0; i < node.Children.size(); ++i)
                {
                    // Create a sequential label as requested by the user.
                    std::string childLabel = std::to_string(i + 1) + " index Node";

                    // Use a unique ID for each InputFloat to avoid conflicts
                    std::string label = "##weight_" + std::to_string(node.ID.m_ID_Data) + "_" + std::to_string(i);

                    // Center the content
                    ImGui::Dummy(ImVec2(dummy_x, 5));
                    ImRect weightRect(ImGui::GetItemRectMin() + insidePadding, ImGui::GetItemRectMax() - insidePadding);

                    // Draw the input box and the sequential label on the same line
                    ImGui::SetCursorPosX(weightRect.GetTL().x + 5);

                    // MODIFIED for 1% precision: step=0.01, step_fast=0.1, format="%.2f" 
                    if (ImGui::InputFloat(label.c_str(), &node.ChildWeights[i], 0.01f, 0.1f, "%.2f"))
                    {
                        // Optional: handle value change, e.g., ensure weight is not negative
                        if (node.ChildWeights[i] < 0.0f) node.ChildWeights[i] = 0.0f;
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(childLabel.c_str());
                }

                ImGui::PopItemWidth();
            }

            ImRect outputsRect;
            int outputAlpha = 200;

            ImGui::Dummy(ImVec2(dummy_x, 10));

            if (BT::IsCompositeNode(node.Type) || BT::IsDecoratorNode(node.Type))
            {
                ImGui::Dummy(ImVec2(dummy_x, padding));
                outputsRect = ImRect(ImGui::GetItemRectMin() + insidePadding, ImGui::GetItemRectMax() - insidePadding);

                ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersTop);
                ed::BeginPin(outPin, ed::PinKind::Output);
                ed::PinPivotRect(outputsRect.GetTL(), outputsRect.GetBR());
                ed::PinRect(outputsRect.GetTL(), outputsRect.GetBR());
                ed::EndPin();
                ed::PopStyleVar();
            }
            else
                ImGui::Dummy(ImVec2(dummy_x, padding));

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
            static ed::LinkId selectedLink;
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

            
            if(0 == selectedLink.Get())
            {
                if (ed::IsLinkSelected(node.ID.m_ID_Data))
                {
                    selectedLink = ed::LinkId(node.ID);
                }
            }

            if(0 < selectedLink.Get() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
				ed::PinId out_dPin, in_dPin;
				ed::GetLinkPins(selectedLink, &out_dPin, &in_dPin);

				ed::NodeId inputNodeId = BTEd::GetTreeNodeIdFromPin(in_dPin);
				ed::NodeId outputNodeId = BTEd::GetTreeNodeIdFromPin(out_dPin);

                BTBuildNode* inputNodeRaw = nullptr;
				BTBuildNode* outputNodeRaw = nullptr;

                if(graph.Nodes.find(inputNodeId.Get()) != graph.Nodes.end())
                {
                    inputNodeRaw = graph.Nodes[inputNodeId.Get()];
				}

                if (graph.Nodes.find(outputNodeId.Get()) != graph.Nodes.end())
                {
					outputNodeRaw = graph.Nodes[outputNodeId.Get()];
                }

                /*if (inputNodeRaw && outputNodeRaw && inputNodeRaw != outputNodeRaw)
                {
                    inputNodeRaw->ParentID = 0;
                    outputNodeRaw->Children.erase(
                        std::remove(outputNodeRaw->Children.begin(), outputNodeRaw->Children.end(), inputNodeRaw->ID),
                        outputNodeRaw->Children.end()
                    );

                    ed::DeleteLink(selectedLink);
                    selectedLink = 0;
                }*/
                if (inputNodeRaw && outputNodeRaw && inputNodeRaw != outputNodeRaw)
                {
                    inputNodeRaw->ParentID = 0;

                    // Find the iterator to the child to be removed
                    auto childIter = std::find(outputNodeRaw->Children.begin(), outputNodeRaw->Children.end(), inputNodeRaw->ID);

                    if (childIter != outputNodeRaw->Children.end())
                    {
                        // Calculate the index before erasing
                        size_t childIndex = std::distance(outputNodeRaw->Children.begin(), childIter);

                        // Erase the child from the Children vector
                        outputNodeRaw->Children.erase(childIter);

                        // If the parent is a WeightedSelector, erase the corresponding weight
                        if (outputNodeRaw->Type == BehaviorNodeType::WeightedSelector)
                        {
                            // Check bounds before erasing
                            if (childIndex < outputNodeRaw->ChildWeights.size())
                            {
                                outputNodeRaw->ChildWeights.erase(outputNodeRaw->ChildWeights.begin() + childIndex);
                            }
                        }
                    }

                    ed::DeleteLink(selectedLink);
                    selectedLink = 0;
                }

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

            s_nodeScreenPos[node.ID] = ed::GetNodePosition(nid);
        }

        if (ed::BeginCreate())
        {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId))
            {
                ed::PinId inputPinId, outputPinId;
                if (BTEd::IsInputPin(startPinId))
                {
                    inputPinId = startPinId;
                    outputPinId = endPinId;
                }
                else
                {
                    inputPinId = endPinId;
                    outputPinId = startPinId;
                }

                if (inputPinId && outputPinId)
                {
                    if (ed::AcceptNewItem())
                    {
                        BTBuildNode* inputNodeRaw = nullptr;
                        BTBuildNode* outputNodeRaw = nullptr;
                        for (auto& node : graph.NodeList)
                        {
                            if (BTEd::InputPin(node.ID) == inputPinId)
                                inputNodeRaw = &node;
                            if (BTEd::OutputPin(node.ID) == outputPinId)
                                outputNodeRaw = &node;
                        }

                        if (inputNodeRaw && outputNodeRaw && inputNodeRaw != outputNodeRaw)
                        {
                            bool hasParent = false;
                            for (auto& p_node : graph.NodeList)
                            {
                                if (std::find(p_node.Children.begin(), p_node.Children.end(), inputNodeRaw->ID) != p_node.Children.end())
                                {
                                    hasParent = true;
                                    break;
                                }
                            }

                            bool canAcceptChild = !BT::IsDecoratorNode(outputNodeRaw->Type) || outputNodeRaw->Children.empty();

                            /*if (!inputNodeRaw->IsRoot && !hasParent && canAcceptChild)
                            {
                                outputNodeRaw->Children.push_back(inputNodeRaw->ID);
                            }*/
                            if (!inputNodeRaw->IsRoot && !hasParent && canAcceptChild)
                            {
                                outputNodeRaw->Children.push_back(inputNodeRaw->ID);

                                // If the parent is a WeightedSelector, add a default weight
                                if (outputNodeRaw->Type == BehaviorNodeType::WeightedSelector)
                                {
                                    outputNodeRaw->ChildWeights.push_back(1.0f);
                                }
                            }
                        }
                    }
                }
            }
        }
        ed::EndCreate();

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
                        ed::Link(ed::LinkId(child.ID.m_ID_Data), BTEd::OutputPin(node.ID), BTEd::InputPin(child.ID));
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
                            if (key != "Action" && key != "Condition" && key != "ConditionDecorator")
                            {
                                if (ImGui::MenuItem(key.c_str()))
                                {
                                    BehaviorNodeType type = BT::StringToNodeType(key);
                                    math::vector2 newPos =
                                        BTEd::ToMathfVec2(s_DragStartNodePos + s_DragDelta);
                                    BTBuildNode* newNode = graph.CreateNode(type, key, newPos);
                                    s_newNodeId = ed::NodeId(newNode->ID.m_ID_Data);
                                    graph.AddChildNode(newNode);
                                }
                            }
                            else if (key == "Action")
                            {
                                if(ImGui::BeginMenu("Action"))
                                {
                                    for (const auto& actionName : ClrHost::Get().GetBTNodeTypeNames(ClrHost::BTNodeKind::Action))
                                    {
                                        if (ImGui::MenuItem(actionName.c_str()))
                                        {
                                            BehaviorNodeType type = BT::StringToNodeType(key);
                                            math::vector2 newPos =
                                                BTEd::ToMathfVec2(s_DragStartNodePos + s_DragDelta);
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
                                    for (const auto& conditionName : ClrHost::Get().GetBTNodeTypeNames(ClrHost::BTNodeKind::Condition))
                                    {
                                        if (ImGui::MenuItem(conditionName.c_str()))
                                        {
                                            BehaviorNodeType type = BT::StringToNodeType(key);
                                            math::vector2 newPos =
                                                BTEd::ToMathfVec2(s_DragStartNodePos + s_DragDelta);
                                            BTBuildNode* newNode = graph.CreateNode(type, key, newPos);
                                            s_newNodeId = ed::NodeId(newNode->ID.m_ID_Data);
                                            graph.AddChildNode(newNode);

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
                            else if (key == "ConditionDecorator")
                            {
                                if (ImGui::BeginMenu("ConditionDecorator"))
                                {
                                    for (const auto& conditionDecoratorName : ClrHost::Get().GetBTNodeTypeNames(ClrHost::BTNodeKind::ConditionDecorator))
                                    {
                                        if (ImGui::MenuItem(conditionDecoratorName.c_str()))
                                        {
                                            BehaviorNodeType type = BT::StringToNodeType(key);
                                            math::vector2 newPos =
                                                BTEd::ToMathfVec2(s_DragStartNodePos + s_DragDelta);
                                            BTBuildNode* newNode = graph.CreateNode(type, key, newPos);
                                            s_newNodeId = ed::NodeId(newNode->ID.m_ID_Data);
                                            graph.AddChildNode(newNode);

                                            newNode->HasScript = true;
                                            newNode->ScriptName = conditionDecoratorName;
                                        }
                                    }
                                    ImGui::Separator();
                                    if (ImGui::MenuItem("Add ConditionDecorator"))
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
                        ed::BreakLinks(ed::NodeId(selectNode->ID.m_ID_Data));

                        s_nodeScreenPos.erase(selectNode->ID);
                        graph.DeleteNode(selectNode->ID);
                        selectNode = nullptr;
                    }
                }
            }

            // 선언된 행동 트리 노드 항목(PHASE 21 M1). 문맥은 트리를 가진 엔티티의
            // 신원이다 — 노드 자체는 신원이 없고, CLI 도 트리를 소유 엔티티로
            // 가리킨다. 항목 0 이면 구분선조차 넣지 않는다(A.6).
            if (::editor::popup_host_has_items(::editor::popup_host::behavior_tree_node))
            {
                if (const std::optional<::editor::entity_target> owner =
                        ::editor::targets::selected_entity())
                {
                    ImGui::Separator();
                    ::editor::draw_popup_menu_items<
                        ::editor::popup_host::behavior_tree_node>(*owner);
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
			// C++ 스크립트 노드 생성은 은퇴(9-4) — BT 노드는 빌트인만 지원한다.
			ImGui::TextDisabled("C++ script nodes are retired.");
			if (ImGui::Button("Close"))
			{
				ImGui::CloseCurrentPopup();
			}

            ImGui::EndPopup();
        }
        ed::Resume();

        ed::End();
        ed::SetCurrentEditor(nullptr);
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
                node.Position = BTEd::ToMathfVec2(s_nodeScreenPos[node.ID]);
            }

            // Save the graph to a file
			Authoring::WriteDocument document = Meta::SerializeDocument(&graph);

            std::ofstream outFile(BTPath.string(), std::ios::binary | std::ios::trunc);

            if (outFile.is_open())
            {
				outFile << document.Dump();
                outFile.close();
            }
            else
            {
                std::cerr << "Failed to open file for writing: " << BTPath.string() << std::endl;
            }

            graph.Clear();
            s_nodeScreenPos.clear();
			BTName.clear();
			editor::close_window(EditorWindowName::kBehaviorTree);
        }
    }
}

static ImVec4 GetColorForType(BlackBoardType type)
{
    switch (type)
    {
    case BlackBoardType::Bool:       return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  // Red
    case BlackBoardType::Int:        return ImVec4(0.4f, 1.0f, 0.4f, 1.0f);  // Green
    case BlackBoardType::Float:      return ImVec4(0.4f, 0.4f, 1.0f, 1.0f);  // Blue
    case BlackBoardType::String:     return ImVec4(1.0f, 1.0f, 0.4f, 1.0f);  // Yellow
    case BlackBoardType::Vector2:    return ImVec4(0.4f, 1.0f, 1.0f, 1.0f);  // Cyan
    case BlackBoardType::Vector3:    return ImVec4(1.0f, 0.4f, 1.0f, 1.0f);  // Magenta
    case BlackBoardType::Vector4:    return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);  // Orange
    case BlackBoardType::Entity: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // White
    case BlackBoardType::Transform:  return ImVec4(0.8f, 0.4f, 1.0f, 1.0f);  // Purple
    case BlackBoardType::None:
    default:                         return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray
    }
}

// 창이 닫혀 있을 때 돌아야 하는 정리 경로다.
void MenuBarWindow::ShowBlackBoardWindow()
{
    if (!editor::is_window_open(EditorWindowName::kBlackBoard))
    {
        BlackBoardWindow(false);
    }
}

// 셸이 창 본문으로 부른다.
void MenuBarWindow::DrawBlackBoardWindow()
{
    BlackBoardWindow(true);
}

void MenuBarWindow::BlackBoardWindow(bool drawing)
{
    static BlackBoard editorBlackBoard;
    static std::string blackBoardName;
    static std::string selectedKey;

    if (drawing)
    {
        // --- Toolbar Buttons ---
        if (ImGui::Button("Create"))
        {
            file::path BBSavePath = ShowSaveFileDialog(
                L"BlackBoard Files (*.blackboard)\0*.blackboard\0",
                L"Save BlackBoard Asset",
                PathFinder::Relative("BehaviorTree").wstring()
            );

            if (!BBSavePath.empty())
            {
                blackBoardName = BBSavePath.stem().string();
                editorBlackBoard.Clear();
                selectedKey = "";
                editorBlackBoard.m_name = blackBoardName;
                if (!editorBlackBoard.Serialize(blackBoardName))
                {
                    Debug->LogError("Failed to create BlackBoard: " + blackBoardName);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            file::path fileName = ShowOpenFileDialog(
                L"BlackBoard Files (*.blackboard)\0*.blackboard\0",
                L"Load BlackBoard",
                PathFinder::Relative("BehaviorTree").wstring()
            );

            if (!fileName.empty())
            {
                editorBlackBoard.Clear();
                selectedKey = "";
                blackBoardName = fileName.stem().string();
                try
                {
                    editorBlackBoard.Deserialize(blackBoardName);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError("Failed to load BlackBoard: " + std::string(e.what()));
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            if (!blackBoardName.empty())
            {
                if (!editorBlackBoard.Serialize(blackBoardName))
                {
                    Debug->LogError("Failed to save BlackBoard: " + blackBoardName);
                }
            }
        }

        ImGui::Separator();

        // --- Main Layout ---
        ImGui::Columns(2, "BlackboardColumns", true);
        static float initialColumnWidth = 200.0f;
        if (initialColumnWidth > 0)
        {
            ImGui::SetColumnWidth(0, initialColumnWidth);
            initialColumnWidth = 0; // Only set initial width once
        }


        // --- Left Pane: Key List ---
        ImGui::BeginChild("KeyListPane");
        {
            // Button to add a new key
            if (ImGui::Button("Add New Key", ImVec2(-1, 0)))
            {
                ImGui::OpenPopup("AddNewKeyPopup");
            }

            // --- Add New Key Popup ---
            if (ImGui::BeginPopup("AddNewKeyPopup"))
            {
                static char searchQuery[128] = "";
                ImGui::Text("Select Type");
                ImGui::Separator();
                ImGui::InputText("Search", searchQuery, sizeof(searchQuery));
                ImGui::Separator();

                const char* typeNames[] = { "Bool", "Int", "Float", "String", "Vector2", "Vector3", "Vector4", "Entity", "Transform" };
                for (int i = 0; i < IM_ARRAYSIZE(typeNames); ++i)
                {
                    // Simple search filter
                    if (searchQuery[0] == '\0' || ImStristr(typeNames[i], typeNames[i] + strlen(typeNames[i]), searchQuery, searchQuery + strlen(searchQuery)))
                    {
                        ImDrawList* draw_list = ImGui::GetWindowDrawList();
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        float line_height = ImGui::GetTextLineHeight();
                        ImVec4 color = GetColorForType(static_cast<BlackBoardType>(i + 1));

                        draw_list->AddRectFilled(p, ImVec2(p.x + 5, p.y + line_height), ImGui::GetColorU32(color));

                        ImGui::Dummy(ImVec2(7, 0));
                        ImGui::SameLine();

                        if (ImGui::Selectable(typeNames[i]))
                        {
                            std::string newKeyName = "NewKey";
                            int counter = 0;
                            while (editorBlackBoard.HasKey(newKeyName)) {
                                newKeyName = "NewKey_" + std::to_string(++counter);
                            }

                            // Create the key with the selected type
                            BlackBoardType selectedType = static_cast<BlackBoardType>(i + 1); // +1 to offset None
                            editorBlackBoard.AddKey(newKeyName, selectedType);
                            selectedKey = newKeyName;

                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::Separator();

            // List of existing keys
            std::vector<std::pair<std::string, BlackBoardValue>> sortedKeys;
            for (const auto& pair : editorBlackBoard.m_values) {
				sortedKeys.push_back(pair);
            }
			std::sort(sortedKeys.begin(), sortedKeys.end(),
				[](const auto& a,const auto& b) {
					const BlackBoardValue& valueA = a.second;
					const BlackBoardValue& valueB = b.second;

					if (valueA.Type != valueB.Type) {
						return static_cast<int>(valueA.Type) < static_cast<int>(valueB.Type);
					}

					return a.first < b.first; // Sort by key name if types are the same

				});
            for (auto const& [key, value] : sortedKeys)
            //for (auto const& [key, value] : editorBlackBoard.m_values)
            {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                float line_height = ImGui::GetTextLineHeight();
                ImVec4 color = GetColorForType(value.Type);

                draw_list->AddRectFilled(p, ImVec2(p.x + 5, p.y + line_height), ImGui::GetColorU32(color));

                ImGui::Dummy(ImVec2(7, 0)); // Add some space between the box and the text
                ImGui::SameLine();

                if (ImGui::Selectable(key.c_str(), selectedKey == key, ImGuiSelectableFlags_AllowOverlap))
                {
                    selectedKey = key;   
                }
                
                /*ImGui::SameLine();

                if (value.Type == BlackBoardType::Bool) {
					ImGui::Text(value.BoolValue ? "True" : "False");
                }
				else if (value.Type == BlackBoardType::Int) {
					ImGui::Text("%d", value.IntValue);
				}
				else if (value.Type == BlackBoardType::Float) {
					ImGui::Text("%.2f", value.FloatValue);
				}
				else if (value.Type == BlackBoardType::String || value.Type == BlackBoardType::Entity || value.Type == BlackBoardType::Transform) {
					ImGui::Text("%s", value.StringValue.c_str());
				}
				else if (value.Type == BlackBoardType::Vector2) {
					ImGui::Text("(%.2f, %.2f)", value.Vec2Value.x, value.Vec2Value.y);
				}
				else if (value.Type == BlackBoardType::Vector3) {
					ImGui::Text("(%.2f, %.2f, %.2f)", value.Vec3Value.x, value.Vec3Value.y, value.Vec3Value.z);
				}
				else if (value.Type == BlackBoardType::Vector4) {
					ImGui::Text("(%.2f, %.2f, %.2f, %.2f)", value.Vec4Value.x, value.Vec4Value.y, value.Vec4Value.z, value.Vec4Value.w);
				}*/

                std::string valueText;
                char buffer[256]; // 충분한 크기의 버퍼
                switch (value.Type) {
                    case BlackBoardType::Bool:   valueText = value.BoolValue ? "True" : "False"; break;
                    case BlackBoardType::Int:    snprintf(buffer, sizeof(buffer), "%d", value.IntValue); valueText = buffer; break;
					case BlackBoardType::Float:  snprintf(buffer, sizeof(buffer), "%.2f", value.FloatValue); valueText = buffer; break;
					case BlackBoardType::String:
					case BlackBoardType::Entity:
					case BlackBoardType::Transform: valueText = value.StringValue; break;
					case BlackBoardType::Vector2: snprintf(buffer, sizeof(buffer), "(%.2f, %.2f)", value.Vec2Value.x, value.Vec2Value.y); valueText = buffer; break;
					case BlackBoardType::Vector3: snprintf(buffer, sizeof(buffer), "(%.2f, %.2f, %.2f)", value.Vec3Value.x, value.Vec3Value.y, value.Vec3Value.z); valueText = buffer; break;
					case BlackBoardType::Vector4: snprintf(buffer, sizeof(buffer), "(%.2f, %.2f, %.2f, %.2f)", value.Vec4Value.x, value.Vec4Value.y, value.Vec4Value.z, value.Vec4Value.w); valueText = buffer; break;
					default:                      valueText = "Unknown Type"; break;
                }

                float textWidth = ImGui::CalcTextSize(valueText.c_str()).x;
                float rightAlignedX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - textWidth;
                ImGui::SameLine(rightAlignedX);
                ImGui::Text("%s", valueText.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        // --- Right Pane: Value Editor ---
        ImGui::BeginChild("ValueEditorPane");
        {
            if (!selectedKey.empty() && editorBlackBoard.HasKey(selectedKey))
            {
                auto& value = editorBlackBoard.m_values.at(selectedKey);
                std::string currentKey = selectedKey;

                // --- Key Name Editor ---
                char keyBuffer[128];
                strncpy_s(keyBuffer, sizeof(keyBuffer), currentKey.c_str(), _TRUNCATE);
                ImGui::Text("Key Name");
                if (ImGui::InputText("##KeyNameEditor", keyBuffer, sizeof(keyBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    std::string newKey(keyBuffer);
                    if (!newKey.empty() && newKey != currentKey && !editorBlackBoard.HasKey(newKey))
                    {
                        editorBlackBoard.RenameKey(currentKey, newKey);
                        selectedKey = newKey;
                    }
                }

                ImGui::SameLine();
                // --- Remove Key Button ---
                if (ImGui::Button("Remove Key"))
                {
                    editorBlackBoard.RemoveKey(currentKey);
                    selectedKey = "";
                }
                else // Continue only if the key was not removed
                {
                    ImGui::Separator();

                    // --- Type Selector ---
                    const char* typeNames[] = { "None", "Bool", "Int", "Float", "String", "Vector2", "Vector3", "Vector4", "Entity", "Transform" };
                    const char* currentTypeName = (value.Type >= BlackBoardType::None && value.Type <= BlackBoardType::Transform) ? typeNames[static_cast<int>(value.Type)] : "Unknown";

                    ImGui::Text("Type");
                    if (ImGui::BeginCombo("##TypeSelector", currentTypeName))
                    {
                        for (int i = 0; i < IM_ARRAYSIZE(typeNames); ++i)
                        {
                            ImDrawList* draw_list = ImGui::GetWindowDrawList();
                            ImVec2 p = ImGui::GetCursorScreenPos();
                            float line_height = ImGui::GetTextLineHeight();
                            ImVec4 color = GetColorForType(static_cast<BlackBoardType>(i));

                            draw_list->AddRectFilled(p, ImVec2(p.x + 5, p.y + line_height), ImGui::GetColorU32(color));

                            ImGui::Dummy(ImVec2(7, 0));
                            ImGui::SameLine();

                            if (ImGui::Selectable(typeNames[i], value.Type == static_cast<BlackBoardType>(i)))
                            {
                                value.Type = static_cast<BlackBoardType>(i);
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::Separator();
                    ImGui::Text("Value");

                    // --- Value Editor ---
                    switch (value.Type)
                    {
                    case BlackBoardType::Bool:
                        ImGui::Checkbox("##BoolValue", &value.BoolValue);
                        break;
                    case BlackBoardType::Int:
                        ImGui::DragInt("##IntValue", &value.IntValue);
                        break;
                    case BlackBoardType::Float:
                        ImGui::DragFloat("##FloatValue", &value.FloatValue);
                        break;
                    case BlackBoardType::String:
                    case BlackBoardType::Entity:
                    case BlackBoardType::Transform:
                    {
                        char buf[256];
                        strncpy_s(buf, sizeof(buf), value.StringValue.c_str(), _TRUNCATE);
                        if (ImGui::InputText("##StringValue", buf, sizeof(buf)))
                        {
                            value.StringValue = buf;
                        }
                        break;
                    }
                    case BlackBoardType::Vector2:
                        ImGui::DragFloat2("##Vector2Value", &value.Vec2Value.x);
                        break;
                    case BlackBoardType::Vector3:
                        ImGui::DragFloat3("##Vector3Value", &value.Vec3Value.x);
                        break;
                    case BlackBoardType::Vector4:
                        ImGui::DragFloat4("##Vector4Value", &value.Vec4Value.x);
                        break;
                    default:
                        ImGui::Text("Unsupported or None type selected.");
                        break;
                    }
                }
            }
            else
            {
                ImGui::Text("Select a key from the list on the left to edit its properties.");
            }
        }
        ImGui::EndChild();

        ImGui::Columns(1); // Reset columns
    }
    else
    {
        if (!blackBoardName.empty())
        {
            editorBlackBoard.Clear();
            blackBoardName.clear();
            selectedKey.clear();
		}
    }
}

void MenuBarWindow::SHowInputActionMap()
{
    static int preseletedActionMapIndex = -1;
    static int seletedActionMapIndex = -1;
    static int editingMapIndex = -1;
    static int preseletedActionIndex = -1;
    static int seletedActionIndex = -1;
    static int editingActionIndex = -1;
    static int index = 0;
    {
        // 옛 표시 가드가 있던 자리다. 범위만 남긴다 — 본문을 통째로
        // 들여쓰기 바꾸면 진짜 변경이 공백에 묻힌다.
        //
        // 첫 크기는 선언이 든다. `open` 은 `Begin` 의 반환이었는데 셸이
        // 그것을 보고 본문을 부를지 정하므로 여기서는 항상 참이다.
        if (ImGui::IsWindowAppearing())
        {
            preseletedActionMapIndex = -1;
            seletedActionMapIndex = -1;
            editingMapIndex = -1;
            preseletedActionIndex = -1;
            seletedActionIndex = -1;
            editingActionIndex = -1;
        }

        if (ImGui::Button("Save"))
        {
            if (!InputActionManagers->SaveManager())
            {
                Debug->LogError("Failed to save one or more input action maps");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            InputActionManagers->LoadManager();
        }
        ImGui::Separator();


        ImGui::BeginChild("ActionMaps", ImVec2(200, 0), true); // 왼쪽
        ImGui::Text("Action Maps");
        ImGui::SameLine();  // 바로 옆에 버튼 배치
        if (ImGui::Button("+"))
        {
            InputActionManagers->AddActionMap();
        }

        ImGui::Separator();
        ImGui::Separator();
       
        for (int i = 0; i < InputActionManagers->m_actionMaps.size(); ++i)
        {
            ImGui::PushID(i);
            if (editingMapIndex != -1 && editingMapIndex ==i)
            {
                char buffer[128];
                strcpy_s(buffer, InputActionManagers->m_actionMaps[editingMapIndex]->m_name.c_str());
                buffer[sizeof(buffer) - 1] = '\0';
                ImGui::SetNextItemWidth(200);
                if (ImGui::InputText("##Rename", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                {
                    // 엔터 눌러서 이름 확정
                    InputActionManagers->m_actionMaps[editingMapIndex]->m_name = buffer;
                    editingMapIndex = -1;
                }

                if (ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())
                {
                    InputActionManagers->m_actionMaps[editingMapIndex]->m_name = buffer;
                    editingMapIndex = -1;
                }
            }
            else
            {
                if (ImGui::Selectable(InputActionManagers->m_actionMaps[i]->m_name.c_str(), true, 0, ImVec2(200, 0)))
                {
                    preseletedActionMapIndex = seletedActionMapIndex;
                    seletedActionMapIndex = i;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    editingMapIndex = i;
                    ImGui::SetKeyboardFocusHere();
             
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    ImGui::OpenPopup("RightClickMenuActinMaps");
                    preseletedActionMapIndex = seletedActionMapIndex;
                    seletedActionMapIndex = i;
                }
                if (ImGui::BeginPopup("RightClickMenuActinMaps"))
                {
                    if (ImGui::MenuItem("Delete ActionMap"))
                    {
                        InputActionManagers->DeleteActionMap(InputActionManagers->m_actionMaps[seletedActionMapIndex]->m_name);
                        seletedActionMapIndex = -1;
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
        
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("Actions", ImVec2(300, 0), true); 
        ImGui::Text("Actions");
        ImGui::SameLine();
        if (seletedActionMapIndex != -1)
        {
            if (ImGui::Button("+"))
            {
                if (seletedActionMapIndex != -1)
                {
                    InputActionManagers->m_actionMaps[seletedActionMapIndex]->AddAction();
                }
            }
        }
        ImGui::Separator();
        ImGui::Separator();
        if (preseletedActionMapIndex != seletedActionMapIndex)
        {
            preseletedActionMapIndex = seletedActionMapIndex;
            seletedActionIndex = -1;
        }
        if (seletedActionMapIndex != -1)
        {
            auto map = InputActionManagers->m_actionMaps[seletedActionMapIndex];
            for (int i = 0; i < map->m_actions.size(); ++i)
            {
                ImGui::PushID(i);
                if (editingActionIndex != -1 && editingActionIndex == i)
                {
                    char buffer[128];
                    strcpy_s(buffer, map->m_actions[editingActionIndex]->actionName.c_str());
                    buffer[sizeof(buffer) - 1] = '\0';
                    ImGui::SetNextItemWidth(300);
                    if (ImGui::InputText("##Rename", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    {
                        // 엔터 눌러서 이름 확정
                        map->m_actions[editingActionIndex]->actionName = buffer;
                        editingActionIndex = -1;
                    }

                    if (ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())
                    {
                        map->m_actions[editingActionIndex]->actionName = buffer;
                        editingActionIndex = -1;
                    }
                }
                else
                {
                    if (ImGui::Selectable(map->m_actions[i]->actionName.c_str(), true, 0, ImVec2(200, 0)))
                    {
                        preseletedActionIndex = seletedActionIndex;
                        seletedActionIndex = i;
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        editingActionIndex = i;
                        ImGui::SetKeyboardFocusHere();

                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        preseletedActionIndex = seletedActionIndex;
                        seletedActionIndex = i;
                        ImGui::OpenPopup("RightClickMenuAction");
                        
                    }
                    if (ImGui::BeginPopup("RightClickMenuAction"))
                    {
                        if (ImGui::MenuItem("Delete Action"))
                        {
                            //delete&&&&&
                            map->DeleteAction(map->m_actions[seletedActionIndex]->actionName);
                            seletedActionIndex = -1;
                        }
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }
            
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("ActinSetting", ImVec2(0, 0), true); 
        ImGui::Text("ActionSetting");
        ImGui::Separator();
        ImGui::Separator();
        if (seletedActionMapIndex != -1 && seletedActionIndex != -1)
        {
            auto action = InputActionManagers->m_actionMaps[seletedActionMapIndex]->m_actions[seletedActionIndex];
           
            static int floatId = 0;
            if (ImGui::CollapsingHeader("Action Type"))
            {
                if (ImGui::Button(ActionTypeString(action->actionType).c_str()))
                {
                    ImGui::OpenPopup("SelectActionType");
                }
            }
            if (ImGui::BeginPopup("SelectActionType"))
            {
                if (ImGui::MenuItem("Button"))
                    action->SetActionType(ActionType::Button);
                else if (ImGui::MenuItem("Value"))
                    action->SetActionType(ActionType::Value);
                ImGui::EndPopup();
            }

            if (ImGui::CollapsingHeader("Key Bind"))
            {
                ImGui::Text("Input Type : ");
                ImGui::SameLine();
                if (ImGui::Button(InputTypeString(action->inputType).c_str()))
                {
                    ImGui::OpenPopup("SelectInputType");
                }
                //value 면 state무시하고 pressed만받고 value 에 vector2에 컨트롤러면 왼스틱,오른스틱  float이면 왼오,트리거만 받게끔 키보드는 다가능 // 키보드는 다가능 4개받게끔 0,1,2,3순 

                //Key State는 value타입일경우 출력x
                ImGui::Text("Key State : ");
                ImGui::SameLine();
                if(ImGui::Button(KeyStateString(action->keystate).c_str()))
                {
                    ImGui::OpenPopup("SelectKeyState");
                }

                if (action->actionType == ActionType::Button)
                {
                    ImGui::Text("Key : ");
                    ImGui::SameLine();
                    if (action->inputType == InputType::KeyBoard)
                    {
                        if (ImGui::Button(KeyBoardString(static_cast<KeyBoard>(action->key[0])).c_str()))
                        {
                            floatId = 0;
                            index = 0;
                            ImGui::OpenPopup("KeyBaordButtonFloat");
                        }
                    }
                    else if (action->inputType == InputType::GamePad)
                    {
                        if (ImGui::Button(ControllerButtonString(static_cast<ControllerButton>(action->key[0])).c_str()))
                        {
                            index = 0;
                            ImGui::OpenPopup("ControllerButton");
                        }
                    }
                    else if (action->inputType == InputType::Mouse)
                    {
                        if (ImGui::Button("Mouse Key chull"))
                        {
                            index = 0;
                        }
                    }
                }
                else if (action->actionType == ActionType::Value)
                {
                    
                    if (action->valueType == InputValueType::Float)
                    {
                        //나중에 구현
                    }
                    else if (action->valueType == InputValueType::Vector2)
                    {
                        if (action->inputType == InputType::KeyBoard)
                        {
                            ImGui::Text("LeftKey : ");
                            ImGui::SameLine();
                            ImGui::PushID(1);
                            if (ImGui::Button(KeyBoardString(static_cast<KeyBoard>(action->key[0])).c_str()))
                            {
                                ImGui::PopID();
                                //첫번째키
                                floatId = 0;
                                index = 0;
                                ImGui::OpenPopup("KeyBaordButtonFloat");
                            }
                            else
                            {
                                ImGui::PopID();
                            }
                            ImGui::Text("RightKey : ");
                            ImGui::SameLine();
                            ImGui::PushID(2);
                            if (ImGui::Button(KeyBoardString(static_cast<KeyBoard>(action->key[1])).c_str()))
                            {
                                ImGui::PopID();
                                //2번째키
                                floatId = 1;
                                index = 0;
                                ImGui::OpenPopup("KeyBaordButtonFloat");
                            }
                            else
                            {
                                ImGui::PopID();
                            }
                            ImGui::Text("DownKey : ");
                            ImGui::SameLine();
                            ImGui::PushID(3);
                            if (ImGui::Button(KeyBoardString(static_cast<KeyBoard>(action->key[2])).c_str()))
                            {
                                ImGui::PopID();
                                //2번째키
                                floatId = 2;
                                index = 0;
                                ImGui::OpenPopup("KeyBaordButtonFloat");
                            }
                            else
                            {
                                ImGui::PopID();
                            }
                            ImGui::Text("UpKey : ");
                            ImGui::SameLine();
                            ImGui::PushID(4);
                            if (ImGui::Button(KeyBoardString(static_cast<KeyBoard>(action->key[3])).c_str()))
                            {
                                ImGui::PopID();
                                //2번째키
                                floatId = 3;
                                index = 0;
                                ImGui::OpenPopup("KeyBaordButtonFloat");
                            }
                            else
                            {
                                ImGui::PopID();
                            }

                        }
                        else if (action->inputType == InputType::GamePad)
                        {
                            ImGui::Text("Key : ");
                            ImGui::SameLine();
                            if (ImGui::Button(ControllerButtonString(action->m_controllerButton).c_str()))
                            {
                                //게임패드는 left 스틱 light 스틱 만 넣게끔
                                index = 0;
                                ImGui::OpenPopup("ControllerButtonFlaot4");
                            }
                        }
                    }
                    
                }

                

                if (ImGui::CollapsingHeader("Funciton"))
                {
                    ImGui::Text("Script : ");
                    ImGui::SameLine();
                    if (ImGui::Button(action->m_scriptName.c_str()))
                    {
                        ImGui::OpenPopup("selectScript");
                    }

                    if (ImGui::BeginPopup("selectScript"))
                    {
                        // C++ 스크립트 은퇴(9-4): 이름을 직접 입력한다(C# 전달 경로는 후속).
                        static char actionScriptName[128] = "";
                        ImGui::InputText("Script", actionScriptName, sizeof(actionScriptName));
                        if (ImGui::Button("Set") && actionScriptName[0] != '\0')
                        {
                            action->m_scriptName = actionScriptName;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::Text("Function : ");
                    ImGui::SameLine();
                    if (ImGui::Button(action->funName.c_str()))
                    {
                        ImGui::OpenPopup("selectMethod");
                    }

                    if (ImGui::BeginPopup("selectMethod"))
                    {
                        auto type = Meta::Find(action->m_scriptName);
                        if (type != nullptr)
                        {
                            for (auto& method : type->methods)
                            {
                                if (ImGui::MenuItem(method.name))
                                {
                                    action->funName = method.name;
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }


                    //char buffer[128];
                    //strcpy_s(buffer, action->funName.c_str());
                    //buffer[sizeof(buffer) - 1] = '\0';
                    //ImGui::SetNextItemWidth(200);
                    //if (ImGui::InputText("##Rename", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                    //{
                    //    // 엔터 눌러서 이름 확정
                    //    action->funName = buffer;
                    //}
                }

            }
            if (ImGui::BeginPopup("SelectInputType"))
            {
                if (ImGui::MenuItem("KeyBoard"))
                    action->SetInputType(InputType::KeyBoard);
                else if (ImGui::MenuItem("GamePad"))
                    action->SetInputType(InputType::GamePad);
                else if (ImGui::MenuItem("Mouse"))
                    action->SetInputType(InputType::Mouse);
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("SelectKeyState"))
            {
                if (ImGui::MenuItem("Down"))
                    action->SetKeyState(KeyState::Down);
                else if (ImGui::MenuItem("Pressed"))
                    action->SetKeyState(KeyState::Pressed);
                else if (ImGui::MenuItem("Released"))
                    action->SetKeyState(KeyState::Released);
                ImGui::EndPopup();
            }

            std::string popupName = "KeyBaordButtonFloat";
            if (ImGui::BeginPopup(popupName.c_str()))
            {
                if (InputManagement->IsWheelDown())
                {
                    index++;
                    constexpr int maxIndex = (int)keyboardButtons.size() - 10;
                    if (index > maxIndex)
                        index = maxIndex;
                }
                else if(InputManagement->IsWheelUp())
                {
                    index--;
                    if (index < 0)
                    {
                        index = 0;
                    }
                }
                for (int i = 0; i < 10; i++)
                {
                    int realIndex = index + i;
                    if (realIndex >= keyboardButtons.size())
                        break; 
                    if (ImGui::MenuItem(KeyBoardString(keyboardButtons[realIndex]).c_str()))
                    {
                        action->key[floatId] = static_cast<size_t>(keyboardButtons[realIndex]);
                        
                    }
                }
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopup("ControllerButton"))
            {
                
                if (InputManagement->IsWheelDown())
                {
                    index++;
                    constexpr int maxIndex = (int)controllerButtons.size() - 10;
                    if (index > maxIndex)
                        index = maxIndex;
                }
                else if (InputManagement->IsWheelUp())
                {
                    index--;
                    if (index < 0)
                    {
                        index = 0;
                    }
                }
                for (int i = 0; i < 10; i++)
                {
                    int realIndex = index + i;
                    if (realIndex >= controllerButtons.size())
                        break;
                    if (ImGui::MenuItem(ControllerButtonString(controllerButtons[realIndex]).c_str()))
                    {
                        //action->key[floatId] = static_cast<size_t>(controllerButtons[realIndex]);
                        action->SetControllerButton(controllerButtons[realIndex]);
                    }
                }
                //컨트롤러 버튼 스크롤
               
         
                ImGui::EndPopup();
            }


            if (ImGui::BeginPopup("ControllerButtonFlaot4"))
            {
                if (ImGui::MenuItem("LEFT_THUMB"))
                    action->SetControllerButton(ControllerButton::LEFT_THUMB);
                else if (ImGui::MenuItem("RIGHT_THUMB"))
                    action->SetControllerButton(ControllerButton::RIGHT_THUMB);
                ImGui::EndPopup();
            }
            
        }

        ImGui::EndChild();
    }
}
void MenuBarWindow::ShowBuildSceneSettingWindow()
{
    {
        // 옛 표시 가드가 있던 자리다. 범위만 남긴다 — 본문을 통째로
        // 들여쓰기 바꾸면 진짜 변경이 공백에 묻힌다.
		BuildSettings& buildSettings = EditorSettingsStore::Get().Build();
		const std::wstring& startupSceneName = buildSettings.GetStartupSceneName();
		ImGui::Text("Startup Scene: %s",
			startupSceneName.empty() ? "(none)" :
			file::path(startupSceneName).string().c_str());
		static char sceneName[128] = "";
        static file::path sceneFileName{};
        if (ImGui::InputText("Scene Name", sceneName, sizeof(sceneName), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            sceneFileName = std::string(sceneName) + ".creator";
		}
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            if (!sceneFileName.empty())
            {
                buildSettings.SetStartupSceneName(sceneFileName.wstring());
                EditorSettingsStore::Get().Save();
				memset(sceneName, 0, sizeof(sceneName)); // Clear the input field
                sceneFileName.clear();
                editor::close_window(EditorWindowName::kBuildSceneSetting);
            }
        }

		ImGui::Separator();
		int buildBackend = RenderBackend::Vulkan ==
			buildSettings.GetRenderBackend() ? 1 : 0;
		constexpr const char* backendItems[] = { "DX12", "Vulkan" };
		if (ImGui::Combo("Player Render Backend", &buildBackend,
			backendItems, IM_ARRAYSIZE(backendItems)))
		{
			buildSettings.SetRenderBackend(
				1 == buildBackend ? RenderBackend::Vulkan : RenderBackend::DX12);
			EditorSettingsStore::Get().Save();
		}
		ImGui::TextDisabled("Packaging projects this value into runtime render.backend.");
		ImGui::TextDisabled("The Player reads only runtime render.backend at process startup.");
	}
}
void MenuBarWindow::ShowRenderDebugWindow()
{
    {
        // 옛 표시 가드가 있던 자리다. 범위만 남긴다 — 본문을 통째로
        // 들여쓰기 바꾸면 진짜 변경이 공백에 묻힌다.
        // RenderDebugManager는 ID3D11DeviceContext로 패스 결과를 복사해 두는
        // DX11 전용 장치이고, 그것을 채우던 GBuffer/Deferred/Forward 패스는
        // SceneRenderer와 함께 메인 배선에서 빠졌다. 그래서 이 창은 열려도
        // 언제나 비어 있었다 — 빈 창은 "캡처가 없다"와 "경로가 죽었다"를
        // 구분해 주지 않으므로, 살아 있는 DX12 표면으로 안내한다.
        // 기본 폰트는 라틴 전용이라 여기 문자열은 영문으로 쓴다(한글은 ??로 나온다).
        ImGui::TextUnformatted("The DX11 pass capture viewer does not work in Enhanced-only mode.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Per-pass GPU timings and validation messages for");
        ImGui::TextUnformatted("EnhancedRenderer (DX12) live in Settings > Pipeline Setting.");
        ImGui::Spacing();
        if (ImGui::Button("Open Pipeline Setting"))
        {
            if (!editor::is_window_open(EditorWindowName::kRenderPass))
            {
                editor::open_window(EditorWindowName::kRenderPass);
            }
            editor::close_window(EditorWindowName::kRenderPassDebug);
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("For per-pass resource contents, take a PIX capture:");
        ImGui::TextUnformatted("  Tools\\dx12-validation\\Invoke-DX12Validation.ps1 -Action PixCapture");
	}
}

// PHASE 21 M4 3단계: 프레임 루프 안에 인라인으로 있던 본문이다. 셸이 프레임을
// 소유하려면 본문이 부를 수 있는 것이어야 해서 메서드로 냈다. 맨 앞의
// BringWindowToFocusFront/DisplayFront 둘은 선언의 stacking 으로 갔다.
void MenuBarWindow::ShowProfilerWindow()
{
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
        if (auto* resources = GetDiagnosticsDeviceResources())
        {
            // 그래프는 바이트를 받는다 — 계약이 MB로 주므로 되돌린다.
            constexpr uint64_t megabyte = 1024ull * 1024ull;
            const RHIVideoMemoryInfo info = resources->QueryVideoMemory();
            ShowVRAMBarGraph(info.usedMB * megabyte, info.budgetMB * megabyte);
        }
    }
    ImGui::EndChild();
}
