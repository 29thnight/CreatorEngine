#include "ViewportHostWindow.h"
#include "EditorTheme.h"
#include "EditorWindowNames.h"
#include "Windows/EditorViewportWindows.h"
#include <imgui.h>
#include <mutex>

namespace editor::windows
{
    namespace
    {
        // 모드의 주인은 UI 스레드다. 게시본과 요청함만 잠금 아래 둔다 —
        // 읽는 쪽(게임 스레드의 뷰 제작자와 CLI)도 쓰는 쪽(CLI 의 모드 인자)도
        // 다른 스레드다. workspace 상태를 가른 것과 같은 이유다.
        std::mutex demandMutex;
        viewport_demand published{};

        viewport_mode currentMode{ viewport_mode::scene };
        bool modeRequested{};
        viewport_mode requestedMode{ viewport_mode::scene };

        // 이번 프레임에 실제로 돈 본문. 선언 표의 `open` 을 읽지 않는 이유는 표가
        // 여는 것과 본문이 프레임을 받는 것이 다르기 때문이다 — 접힌 도크는 열려
        // 있어도 본문을 부르지 않는다. "그렸는가" 가 곧 "보이는가" 다.
        bool hostDrewThisFrame{};
        bool previewDrewThisFrame{};
        bool hostFocusedThisFrame{};
        bool hostHoveredThisFrame{};
        bool focusRequested{};
        std::uint64_t canvasClicks{};
    }

    void request_viewport_focus()
    {
        std::lock_guard lock(demandMutex);
        focusRequested = true;
    }

    void note_game_canvas_clicked()
    {
        std::lock_guard lock(demandMutex);
        ++canvasClicks;
    }

    viewport_mode get_viewport_mode()
    {
        // 게시본을 읽는다. `publish_viewport_demand` 가 프레임 끝에서 돌고
        // workspace 저장은 그 **뒤**라(`EditorRenderer::Render`), 저장이 보는
        // 값은 같은 프레임의 모드다.
        std::lock_guard lock(demandMutex);
        return published.mode;
    }

    void request_viewport_mode(viewport_mode mode)
    {
        std::lock_guard lock(demandMutex);
        modeRequested = true;
        requestedMode = mode;
    }

    viewport_demand read_viewport_demand()
    {
        std::lock_guard lock(demandMutex);
        return published;
    }

    void note_view_submission(bool editorSkippedByDemand, bool gameSkippedByDemand)
    {
        std::lock_guard lock(demandMutex);
        if (editorSkippedByDemand) ++published.suppressedEditorViews;
        if (gameSkippedByDemand) ++published.suppressedGameViews;
    }

    void publish_viewport_demand()
    {
        // 자리가 프레임 **끝**인 이유는 Host 본문과 Preview 본문의 순서가 선언
        // 순서에 달려 있기 때문이다. 본문 안에서 게시하면 뒤에 오는 본문의 사실이
        // 한 프레임 늦게 반영되고, 그 지연은 "닫았는데 아직 만든다" 로 보인다.
        std::lock_guard lock(demandMutex);
        published.hostPresent = hostDrewThisFrame;
        published.mode = currentMode;
        // ★ 에디터 타깃은 **대칭으로 끄지 않는다.** Game 모드일 때 씬 뷰가
        //   보이지 않는 것은 맞지만, 그 타깃을 프레임마다 세웠다 무너뜨리는
        //   것은 타깃의 생성·회수를 두 백엔드에서 검증한 뒤에야 안전하다
        //   (계획서가 extent 기반 resize 를 그 뒤로 미룬 것과 같은 이유다).
        //   지금 거두는 것은 비싼 쪽 하나 — 게임 카메라의 전체 씬이다.
        published.editorTarget = true;
        published.gameTarget = (hostDrewThisFrame && viewport_mode::game == currentMode) ||
                               previewDrewThisFrame;
        ++published.publishedFrames;
        if (viewport_mode::game == currentMode) ++published.gameModeFrames;
        else                                    ++published.sceneModeFrames;
        if (previewDrewThisFrame) ++published.gamePreviewFrames;

        // W5: UI 입력 상태. `EndRender` 가 `ImGui::Render` **앞**에서 부르므로
        // 문맥이 살아 있고, 이 셋은 `NewFrame` 이 정한 뒤 프레임 내내 같은 값이다.
        const ImGuiIO& io = ImGui::GetIO();
        published.uiWantCaptureMouse = io.WantCaptureMouse;
        published.uiWantCaptureKeyboard = io.WantCaptureKeyboard;
        published.uiWantTextInput = io.WantTextInput;
        published.hostFocused = hostFocusedThisFrame;
        published.hostHovered = hostHoveredThisFrame;
        published.gameCanvasClicks = canvasClicks;

        hostDrewThisFrame = false;
        previewDrewThisFrame = false;
        hostFocusedThisFrame = false;
        hostHoveredThisFrame = false;
    }

    void draw_viewport_host()
    {
        hostDrewThisFrame = true;

        // 모드 막대는 이 창이 대신한 **도크 탭 바와 같은 스타일**로 그린다. 창을
        // 둘에서 하나로 합치는 것이 화면의 모양까지 바꿀 이유는 없다 — 가운데
        // 노드의 탭 바는 `EditorRenderer` 가 끈다(`ImGuiDockNodeFlags_NoTabBar`).
        bool applyRequest{};
        bool applyFocus{};
        viewport_mode requested{ viewport_mode::scene };
        {
            std::lock_guard lock(demandMutex);
            applyRequest = modeRequested;
            requested = requestedMode;
            modeRequested = false;
            applyFocus = focusRequested;
            focusRequested = false;
        }
        if (applyFocus) ImGui::SetWindowFocus();
        hostFocusedThisFrame = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        hostHoveredThisFrame = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

        {
            const ::editor::TabStyleScope tabs;
            if (ImGui::BeginTabBar("##Editor.ViewportModes", ImGuiTabBarFlags_None))
            {
                const auto item = [&](const char* label, viewport_mode mode)
                {
                    const ImGuiTabItemFlags flags = applyRequest && requested == mode
                        ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                    if (ImGui::BeginTabItem(label, nullptr, flags))
                    {
                        // 요청을 적용하는 프레임에는 ImGui 가 아직 옛 탭을 선택된
                        // 것으로 보고할 수 있다. 그 프레임의 보고로 요청을 덮어쓰지
                        // 않는다 — 덮어쓰면 복원한 모드가 한 프레임 만에 되돌아간다.
                        if (!applyRequest) currentMode = mode;
                        ImGui::EndTabItem();
                    }
                };
                item(EditorWindowName::kSceneLabel, viewport_mode::scene);
                item(EditorWindowName::kGameLabel, viewport_mode::game);
                ImGui::EndTabBar();
            }
        }
        if (applyRequest) currentMode = requested;

        if (viewport_mode::game == currentMode) draw_game_view();
        else                                    draw_scene_view();
    }

    void draw_game_preview()
    {
        previewDrewThisFrame = true;
        draw_game_view();
    }
}
