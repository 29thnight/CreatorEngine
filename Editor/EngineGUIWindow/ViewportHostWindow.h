#pragma once
// PHASE 21 W4 — 중앙 ViewportHost (계획서 §1.5).
//
// Scene 과 Game 은 더 이상 창 둘이 아니다. 가운데 노드를 차지하는 **닫을 수 없는
// 창 하나**가 표시 모드를 고르고, 모드가 자기 캔버스를 그린다.
//
// 창을 둘로 두었을 때의 대가는 두 가지였다.
//
//   ① 좌표 규약이 창마다 갈렸다(§1.5). Host 가 텍스처만 바꿔서는 못 고친다 —
//      `EditorViewportCanvas.h` 가 그 규약을 하나로 세우고 모드가 정책만 고른다.
//   ② 보이지 않는 뷰의 그림을 계속 만들었다. 창이 둘이면 "지금 무엇이 보이는가"
//      를 아무도 답하지 않으므로 제작자(`App.cpp`)가 둘 다 밀봉할 수밖에 없었다.
//      모드가 생기면 그 물음에 답이 생긴다 — 그것이 아래 view demand 다.
#include <cstdint>

namespace editor::windows
{
    enum class viewport_mode : std::uint8_t { scene = 0, game = 1 };

    // 중앙 Host 의 본문. 모드 막대를 그리고 고른 모드에게 캔버스를 넘긴다.
    void draw_viewport_host();

    // Scene 모드일 때도 게임 화면을 곁에 띄우는 선택적 패널. 기본은 닫힘이다 —
    // 열려 있는 동안에만 Game 타깃의 수요가 선다.
    void draw_game_preview();

    /// 지금 화면에 있는 모드. **게시본**을 읽으므로 다른 스레드에서도 안전하다 —
    /// 쓰는 쪽은 UI 스레드(모드 막대)이고 읽는 쪽은 UI 스레드(workspace 저장)와
    /// 게임 스레드(CLI) 둘이다.
    viewport_mode get_viewport_mode();

    /// 다음 프레임에 모드를 바꾼다. workspace 복원(UI 스레드)과 CLI(게임
    /// 스레드)가 쓰므로 요청함도 잠금 아래 있다.
    void request_viewport_mode(viewport_mode mode);

    /// 이번 프레임에 실제로 화면에 있는 표시 타깃. 게임 스레드의 뷰 제작자가
    /// 읽는다(`App.cpp`). 누계는 "쓸데없이 만든 적이 있는가" 를 밖에서 셀 수
    /// 있게 하려고 함께 든다 — 계획서 W4 판정이 요구하는 계측이다.
    struct viewport_demand
    {
        viewport_mode mode{ viewport_mode::scene };
        bool editorTarget{ true };
        bool gameTarget{ true };
        std::uint64_t publishedFrames{};   ///< UI 가 수요를 게시한 프레임 수
        std::uint64_t sceneModeFrames{};
        std::uint64_t gameModeFrames{};
        std::uint64_t gamePreviewFrames{}; ///< Game Preview 가 열려 있던 프레임 수
        /// 만들 수 있었는데 **수요가 없어** 만들지 않은 프레임 수. 카메라가 없어
        /// 못 만든 프레임은 여기 들어오지 않는다 — 섞어 두면 이 수가 "안 만들었다"
        /// 와 "못 만들었다" 의 합이 되고, 게임 카메라가 없는 씬에서는 수요 문을
        /// 통째로 걷어도 수가 그대로라 이것을 읽는 단정이 아무것도 재지 않게 된다.
        std::uint64_t suppressedGameViews{};
        std::uint64_t suppressedEditorViews{};
        bool hostPresent{};                ///< Host 본문이 한 번이라도 돌았는가

        // ── UI 입력 상태 (PHASE 21 W5 선행 2 · 계획서 §1.8) ──
        //
        // ImGui 가 **출력**하는 값이다 — "이번 프레임 UI 가 마우스/키보드/글자를
        // 가져갔는가". 게임 스레드의 PlayModeController 가 입력 소유자를 정할 때
        // 읽고, `editor.viewport` 가 밖으로 낸다. 옛 `ImGuiHost::BeginFrame` 은
        // 이 셋을 프레임마다 true 로 덮어썼는데, 그 대입은 `ImGui::NewFrame` 이
        // 곧바로 다시 계산하므로 **죽은 줄**이었다 — 게시본이 프레임마다 다른
        // 값을 보이는 것이 그 증명이다(W5 착지 기록).
        bool uiWantCaptureMouse{};
        bool uiWantCaptureKeyboard{};
        bool uiWantTextInput{};
        bool hostFocused{};                ///< Host 창(자식 포함)이 ImGui 포커스인가
        bool hostHovered{};
        /// 게임 캔버스(Host Game 모드 · Game Preview)가 클릭된 누계. possess 요청.
        std::uint64_t gameCanvasClicks{};

        // ── 뷰포트 extent (PHASE 21 W4 후속 — 계획서가 미뤄 둔 extent 기반 resize) ──
        //
        // Host 본문이 이번 프레임에 받은 content region 을 **물리 픽셀**로 낸 값.
        // 라이브 뷰의 렌더 해상도가 여기서 나온다 — 예전에는 창 클라이언트 크기가
        // 곧 렌더 해상도였고, 창을 DPI 배수로 키우자 보이는 것보다 2.2 배를 그리고
        // 잘라 버리고 있었다(W4 착지 뒤 실측). Godot 의 `SubViewportContainer`
        // (stretch: 컨테이너 크기가 곧 SubViewport 해상도)와 Unreal 의 Slate
        // 뷰포트가 쓰는 계약이 이것이다.
        std::uint32_t canvasWidth{};
        std::uint32_t canvasHeight{};
        float dpiScale{ 1.f };       ///< 창의 DPI 배수(96 기준)
        float renderScale{ 1.f };    ///< 이번 프레임에 적용된 렌더 배율
    };
    viewport_demand read_viewport_demand();

    // ── 렌더 배율 (PHASE 21 W4 후속 · Unreal 의 secondary screen percentage) ──
    //
    // 표시 크기보다 **낮게 그리는** 손잡이다. Unreal 에디터의 기본값이
    // `SecondaryScreenPercentage = 100 / OS's DPI Scale` 이고 그 이유를 문서가
    // 둘로 적는다 — 고밀도 디스플레이에서 성능을 일정하게 유지하는 것과, GPU 가
    // 감당 못 할 만큼 큰 중간 렌더 타깃을 만들지 않는 것이다. 끄는 선택지
    // (`Disable DPI Based Editor Viewport Scaling`)도 함께 둔다.
    //
    // Godot 은 같은 일을 `SubViewportContainer::stretch_shrink` 로 한다 —
    // "Divides the sub-viewport's effective resolution by this value while
    // preserving its scale". 화면에서 차지하는 자리는 그대로고 해상도만 내려간다.
    enum class render_scale_mode : std::uint8_t
    {
        dpi_auto = 0,  ///< 1 / DPI 배수. 창을 DPI 로 키운 몫을 정확히 상쇄한다.
        off      = 1,  ///< 언제나 1.0 — 표시 해상도 그대로 그린다.
        fixed    = 2,  ///< 사용자가 못 박은 값.
    };

    struct viewport_render_scale
    {
        render_scale_mode mode{ render_scale_mode::dpi_auto };
        float fixedValue{ 1.f };   ///< `fixed` 일 때만 뜻이 있다.
        float applied{ 1.f };      ///< 마지막 프레임에 실제로 쓴 값.
        float dpiScale{ 1.f };
    };

    /// 최소·최대는 규약이다. 0 에 가까운 배율은 렌더 타깃을 0 으로 만들고,
    /// 1 을 넘는 배율은 이 작업이 없애려던 바로 그 낭비를 되살린다.
    inline constexpr float kMinViewportRenderScale = 0.25f;
    inline constexpr float kMaxViewportRenderScale = 1.f;

    void set_viewport_render_scale(render_scale_mode mode, float fixedValue = 1.f);
    viewport_render_scale get_viewport_render_scale();

    /// 다음 프레임에 Host 창에 ImGui 포커스를 준다. Stop 이 재생 전 포커스를
    /// 되돌릴 때 게임 스레드가 부른다(요청함, 잠금 아래).
    void request_viewport_focus();

    /// 게임 캔버스가 클릭됐다. UI 스레드가 부른다.
    void note_game_canvas_clicked();

    /// 프레임 끝에 한 번. 이번 프레임에 실제로 돈 본문을 수요로 굳힌다.
    void publish_viewport_demand();

    /// 뷰 제작자가 프레임마다 부른다. **수요가 없어서** 건너뛴 사실만 누계로
    /// 남긴다 — 건너뛴 것을 세지 않으면 "안 만들었다" 를 증명할 수 없고,
    /// 못 만든 것까지 세면 "안 만들었다" 가 공짜로 참이 된다.
    void note_view_submission(bool editorSkippedByDemand, bool gameSkippedByDemand);
}
