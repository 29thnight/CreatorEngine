#pragma once
// 에디터 크롬 스냅샷 (PHASE 21 W0 전반 · 계획서 §1.9) — 밖에서 배치를 볼 수단.
//
// ── 왜 스냅샷인가 ────────────────────────────────────────────────────────
//
// `editor.windows`(M4)는 **선언 표**를 읽는다. 표는 부팅 때 한 번 쓰이고 그 뒤로
// 읽기만 하므로 어느 스레드에서 읽어도 된다. 그런데 배치·스타일은 선언이 아니라
// **살아 있는 ImGui 상태**다. 그리고 ImGui 프레임은 PresentationThread 에서 돌고
// (`EditorMain::PresentFrame`), CLI 명령은 게임 스레드의 `Pump()` 에서 돈다
// (`App.cpp:274`). 둘은 `m_sceneStructureMutex` 로만 겹치므로 명령 핸들러가
// `ImGui::` 를 직접 부르면 `NewFrame`~`Render` 한복판의 전역 문맥을 읽는 경합이다.
//
// 그래서 **그리는 쪽이 프레임 끝에 값을 게시하고 읽는 쪽은 그 사본을 본다.**
// 이 저장소가 렌더 델타에 쓰는 것과 같은 형태이고, 게이트 입장에서는 한 프레임
// 안에서 일관된 그림을 본다는 이득이 더 크다 — 뮤텍스를 들고 ImGui 를 읽으면
// 노드 rect 와 창 배치가 서로 다른 프레임 것일 수 있다.
//
// ── 이 헤더가 ImGui 를 모르는 이유 ───────────────────────────────────────
//
// `ImGuiCol_COUNT` 를 쓰면 이 헤더가 `imgui.h` 를 타고 CLI TU 까지 퍼진다. 색과
// 스칼라를 **이름·값 쌍의 벡터**로 들면 개수를 박지 않아도 되고(항목이 늘면
// 덤프가 알아서 늘어난다) 헤더는 std 만 의존한다. 이름은 게시하는 쪽이
// `ImGui::GetStyleColorName` 에서 받아 넣는다. M1 과 같은 가름이다 — ImGui 를
// 아는 곳은 `EditorChromeProbe.cpp` 하나다.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor
{
    // ── 게시되는 값 ───────────────────────────────────────────────────────

    /// 도크 노드 하나. ImGui 의 `ImGuiDockNode` 에서 밖으로 낼 몫만 뽑았다.
    struct dock_node_view
    {
        std::uint32_t id{ 0 };
        std::uint32_t parent{ 0 };          ///< 0 == 뿌리
        float         rect[4]{};            ///< x0 y0 x1 y1
        int           split_axis{ -1 };     ///< -1 없음 · 0 X · 1 Y
        int           tab_count{ 0 };
        bool          is_central{ false };
        bool          is_leaf{ false };
        bool          is_visible{ false };
    };

    /// 선언된 창 하나가 이번 프레임에 어디 놓였는가.
    struct window_placement_view
    {
        std::string   stable_id;

        /// 사람이 보는 라벨. 대개 `stable_id` 와 같은 값이고, 갈린 창은 아직
        /// Content Browser 하나다(W3 이 나머지를 가른다).
        std::string   label;

        /// 라벨에서 **폰트에 없는 코드포인트**의 수. 없는 글리프는 네모 한
        /// 칸으로 그려지고 아무도 그것을 실패로 읽지 않는다 — 아이콘을 바꿀
        /// 때마다 사람이 눈으로 확인하는 대신 이 수를 센다.
        int           missing_glyphs{ 0 };

        std::uint32_t dock_node{ 0 };       ///< 0 == 도크되지 않음
        float         rect[4]{};
        bool          known_to_imgui{ false };  ///< ImGui 가 이 이름의 창을 아는가
        bool          dock_active{ false };

        /// 도크되지 않은 것이 **정상**인 창. 두 경우다 — 선언이 자리를
        /// `floating` 으로 준 창, 그리고 Tile 스타일에서 팝업 드로어가 되는
        /// Content Browser(도크 빌더가 같은 조건으로 건너뛴다).
        ///
        /// 판단을 게시하는 쪽에 둔 이유는 그 조건이 매 프레임 갈리는 설정값이고
        /// 도크 빌더가 이미 그 값을 보기 때문이다. 감사 쪽에서 다시 판단하면
        /// 같은 규칙이 두 군데가 되어 갈릴 수 있다.
        bool          dock_exempt{ false };
    };

    /// 도크 노드가 들고 있는 탭 이름. 선언에 없는 이름(유령)을 가르는 데 쓴다.
    struct dock_tab_view
    {
        std::uint32_t node{ 0 };
        std::string   window;
    };

    /// 내부 API 를 읽어도 되는 ImGui 판. 이 스냅샷은 `imgui_internal.h` 의
    /// `ImGuiDockNode`·`ImGuiWindow` 필드를 직접 읽는다 — 그 구조체는 공개
    /// 계약이 아니라 판마다 바뀐다(1.92 에서 `ImGuiDockNode` 의 플래그 멤버가
    /// 셋으로 갈렸다). vcpkg 가 판을 올리면 컴파일은 통과하면서 읽는 값만
    /// 어긋날 수 있으므로 번호를 못 박고 감사가 대조한다. 계획서 §9 가 W8 에
    /// 요구한 "ImGui internal adapter version canary" 가 이것이다.
    ///
    /// ★ 이 값은 **런타임이 보고한 값이다.** 헤더를 눈으로 읽어 정하지 마라 —
    ///   이 기계에는 imgui 설치본이 둘 있다. 전역 classic vcpkg 의
    ///   `installed/x64-windows` 는 1.91.7 이고, 실제로 컴파일에 쓰이는 것은
    ///   저장소 매니페스트의 `vcpkg_installed/x64-windows/x64-windows` 로
    ///   1.92.8 이다. 앞쪽을 읽고 판단했다가 결론이 뒤집혔다.
    inline constexpr int expected_imgui_version_num = 19280;

    /// 이번 실행에서 적재한 폰트 하나. `editor::fonts` 의 기록을 그대로 옮긴다.
    ///
    /// 감사가 이것을 드는 이유는 폰트가 **스타일의 일부**이고, 파일이 없을 때
    /// 죽던 자리가 여기였기 때문이다(2026-09-11 ACCESS_VIOLATION 실측).
    /// 경로를 보고하면 어느 후보가 이겼는지, 기본 폰트로 내려갔는지가 밖에서
    /// 보인다 — 내려간 채 도는 것은 틀린 것이 아니지만 조용하면 안 된다.
    struct font_view
    {
        std::string role;
        std::string resolved_path;      ///< 빈 것이면 후보가 하나도 없었다
        float       size_pixels{ 0.f };
        int         candidates_tried{ 0 };
        bool        used_fallback{ false };
        bool        icon_merged{ false };
        bool        present{ false };   ///< ImFont 가 실제로 섰는가
    };

    struct chrome_snapshot
    {
        bool          valid{ false };
        std::uint64_t frame{ 0 };

        std::string   imgui_version;
        int           imgui_version_num{ 0 };

        std::vector<dock_node_view>        nodes;
        std::vector<dock_tab_view>         tabs;
        std::vector<window_placement_view> placements;

        /// 스타일 값. 색은 RGBA8 로 접어 담는다(0xRRGGBBAA).
        std::vector<std::pair<std::string, std::uint32_t>> style_colors;
        std::vector<std::pair<std::string, float>>         style_scalars;
        std::size_t   colors_differing_from_default{ 0 };

        /// 글자 배율의 **정식 경로**. 1.92 에서 `io.FontGlobalScale` 이
        /// obsolete 가 됐고 W1 이 여기로 옮겼다.
        float         font_scale_main{ 0.f };
        /// 적재한 폰트 전부(PHASE 21 W1).
        std::vector<font_view> fonts;
        /// 후보가 하나도 없을 때 해상이 빈 것을 돌려주는가. 살아 있는
        /// 에디터에서 negative 경로를 재는 자리다 — 실제 시스템 폰트를
        /// 지울 수 없으므로 해상 함수만 태운다.
        bool          font_fallback_probe_ok{ false };

        float         preference_scale{ 0.f };

        std::string   ini_path;
        bool          ini_exists{ false };
        std::uint64_t ini_bytes{ 0 };

        /// 이 스냅샷을 뜨는 데 걸린 시간. W0 의 성능 기준선이 읽는다 —
        /// 매 프레임 도는 계측이 자기 비용을 숨기면 기준선이 거짓이 된다.
        double        capture_ms{ 0.0 };

        // ── 이 프레임의 UI 비용 (PHASE 21 W0 후반, 계획서 §11) ────────────
        //
        // W7·W8 의 성능 판정이 "W0 기준선과 비교" 를 전제한다. 그 기준선을
        // 밖에서 읽을 수단이 없으면 판정문이 추정치가 된다.
        //
        // 정점·인덱스·커맨드 수는 `ImGui::Render()` **뒤**라야 유효하다
        // (`ImDrawData::Valid`). 배치·스타일을 뜨는 자리는 `EndFrame` 앞이라야
        // 하므로 두 계측의 자리가 다르고, 그래서 이 셋만 뒤에서 채운다.
        std::int64_t  imgui_vertices{ -1 };
        std::int64_t  imgui_indices{ -1 };
        std::int64_t  imgui_draw_commands{ -1 };

        /// 에디터 UI 한 프레임을 만드는 데 든 CPU 시간. `BeginRender` 부터
        /// `EndRender` 까지이고 GPU 제출·Present 는 빠진다.
        double        ui_cpu_ms{ -1.0 };
    };

    // ── 게시와 읽기 ───────────────────────────────────────────────────────

    /// PresentationThread 가 프레임 끝에 부른다.
    void publish_chrome_snapshot(chrome_snapshot&& snapshot);

    /// 방금 게시한 스냅샷에 draw data 계측만 얹는다.
    ///
    /// 따로 있는 이유는 **유효한 자리가 다르기 때문**이다. 배치와 스타일은
    /// `EndFrame` 앞에서 떠야 하고(창의 도크 소속이 확정된 자리), draw data 는
    /// `ImGui::Render()` 뒤라야 유효하다. 게시를 두 번 하지 않고 뒤엣것만
    /// 얹는다 — 값 셋이 **같은 프레임**의 것이어야 표가 거짓말을 하지 않는다.
    ///
    /// 게시된 스냅샷이 없으면 아무 일도 하지 않는다.
    void amend_chrome_draw_totals(std::int64_t vertices,
                                  std::int64_t indices,
                                  std::int64_t draw_commands,
                                  double ui_cpu_ms);

    /// 게임 스레드(CLI)가 부른다. **사본을 돌려준다** — 호출자가 들고 있는 동안
    /// 다음 프레임이 게시해도 안전해야 한다.
    chrome_snapshot read_chrome_snapshot();

    // ── 언제 뜨는가 ───────────────────────────────────────────────────────
    //
    // 매 프레임 뜨지 않는다. Debug 실측으로 한 번에 0.47 ms 였다 — 스타일 색
    // 63 개와 스칼라 28 개의 이름을 문자열로 만드는 값이고, 프레임마다 물리면
    // 진단 장치가 진단 대상보다 비싸진다.
    //
    // 그래서 **주기 + 요청**이다. `kCaptureIntervalFrames` 마다 한 번 뜨고(60fps
    // 기준 0.5 초, 프레임당 평균 0.016 ms), 명령이 읽은 뒤 다음 한 번을
    // 요청한다. 묵은 정도는 숨기지 않는다 — 덤프가 `frame=` 을 찍으므로 읽는
    // 쪽이 언제 뜬 값인지 본다.
    inline constexpr int kCaptureIntervalFrames = 30;

    /// 다음 프레임에 한 번 반드시 뜨게 한다. CLI 가 읽은 뒤 부른다.
    void request_chrome_snapshot();

    /// 요청 깃발을 내려 읽는다. 게시하는 쪽만 부른다.
    bool consume_chrome_snapshot_request();

    // ── 감사 ──────────────────────────────────────────────────────────────
    //
    // 세 감사 모두 `clean()` 에 **프로그램이 책임지는 것만** 담는다. 사용자
    // 상태가 흐른 흔적(옛 빌드의 창이 ini 에 남은 것 따위)은 덤프에 싣되
    // 판정에 넣지 않는다 — 그것으로 붉어지는 게이트는 도는 세트에 못 들어간다.

    struct dock_audit
    {
        std::size_t nodes{ 0 };
        std::size_t leaf_nodes{ 0 };
        /// 중앙 노드 수. **존재는 단정하지 않는다** — 실측으로 지금 0 이고,
        /// 중앙 `ViewportHost` 노드를 세우는 것은 W4 의 일이다(`BuildInitialDockLayout`
        /// 이 `DockBuilderAddNode` 를 `ImGuiDockNodeFlags_DockSpace` 없이 부르고
        /// 아무도 중앙을 지정하지 않는다). 여기서 존재를 단정하면 W4 착수 전까지
        /// 게이트가 내내 붉어 도는 세트에 들어갈 수 없다. **유일성만** 본다.
        std::size_t central_nodes{ 0 };
        std::size_t docked_windows{ 0 };

        /// 그려지고 있는데 도크 노드가 0인 창.
        std::vector<std::string> undocked_slots;
        /// 도크 노드가 들고 있는데 선언 표에 없는 이름. 옛 `GetContext` 가
        /// `operator[]` 라 오타로 유령 창을 만들던 자리의 관측이다.
        std::vector<std::string> ghost_tabs;

        /// 내부 구조체를 읽어도 되는 판인가. 계획서 §9 가 W8 에 요구한
        /// "ImGui internal adapter version canary" 의 씨앗이다. 여기 두는 이유는
        /// 이 감사가 `imgui_internal.h` 구조체를 가장 깊이 읽기 때문이다.
        bool internal_api_version_known{ false };
        int  imgui_version_num{ 0 };

        bool clean() const noexcept
        {
            return (2 > central_nodes) && undocked_slots.empty() &&
                   ghost_tabs.empty() && internal_api_version_known;
        }
    };

    struct theme_audit
    {
        std::size_t colors{ 0 };
        std::size_t scalars{ 0 };
        std::size_t colors_differing_from_default{ 0 };

        float font_scale_main{ 0.f };
        float preference_scale{ 0.f };

        /// 에디터 스타일이 실제로 적용됐는가. ImGui 기본값과 다른 색이 하나도
        /// 없으면 `ApplyEditorStyle` 이 돌지 않았다는 뜻이다.
        bool  style_applied{ false };
        /// 배율의 출처가 하나인가. 적용 경로가 둘(최초·변경 시 재적용)이라
        /// 한쪽만 고치면 여기서 갈린다.
        bool  scale_matches{ false };

        /// 본문 폰트가 섰는가. 이것이 거짓이면 글자가 아예 안 그려진다.
        bool  body_font_present{ false };
        /// 본문 폰트가 ImGui 기본 폰트로 내려갔는가. 틀린 것은 아니지만
        /// 조용히 내려가 있으면 안 된다 — 그래서 센다.
        bool  body_font_used_fallback{ false };
        /// 아이콘이 본문 폰트에 병합됐는가.
        bool  icon_font_merged{ false };
        /// 해상의 negative 경로가 성립하는가.
        bool  font_fallback_probe_ok{ false };
        /// 적재한 폰트 수.
        std::size_t fonts{ 0 };

        /// 글리프가 빠진 라벨들. `<창 안정 id>:<빠진 수>` 로 적는다.
        ///
        /// 감사가 이것을 드는 이유는 아이콘이 **스타일의 일부**여서다. 폰트
        /// 블롭은 서브셋이라 `IconsFontAwesome6.h` 에 정의돼 있다고 해서 실제로
        /// 들어 있는 것은 아니고, 없으면 조용히 네모가 그려진다. W1 이 토큰을
        /// 갈아엎을 때 이 수가 곧바로 답한다.
        std::vector<std::string> labels_missing_glyphs;

        bool clean() const noexcept
        {
            return style_applied && scale_matches && labels_missing_glyphs.empty()
                && body_font_present && icon_font_merged && font_fallback_probe_ok;
        }
    };

    struct layout_audit
    {
        std::string   ini_path;
        bool          ini_exists{ false };
        std::uint64_t ini_bytes{ 0 };

        std::size_t   ini_entries{ 0 };
        std::size_t   matched{ 0 };

        /// 같은 창 이름이 ini 에 두 번. 표시 문자열이 persistence ID 인 계약의
        /// 대가가 실제로 나타나는 모양이다(계획서 §1.4 — 공백 하나가 달라
        /// Content Browser 항목이 둘로 갈린 이력이 있다).
        std::vector<std::string> duplicate_entries;
        /// ini 에 있으나 선언에 없는 항목. 옛 빌드의 흔적일 수 있어 **판정에
        /// 넣지 않는다.** W3 이주가 읽을 값이다.
        std::vector<std::string> orphan_entries;

        bool clean() const noexcept
        {
            // 빈 집합을 성공으로 읽지 않는다. ini 가 있다면 맞물린 항목이
            // 하나라도 있어야 한다 — 파일만 있고 하나도 안 맞는 것은 이름
            // 계약이 통째로 어긋난 상태다.
            const bool readable = (!ini_exists) || (0 != matched);
            return duplicate_entries.empty() && readable;
        }
    };

    dock_audit   audit_dock_tree(const chrome_snapshot& snapshot);
    theme_audit  audit_theme(const chrome_snapshot& snapshot);
    layout_audit audit_layout(const chrome_snapshot& snapshot);

    // ── 덤프 ──────────────────────────────────────────────────────────────
    //
    // 형식은 `editor.windows` 와 `editor.menu` 가 정한 TSV 관례를 따른다
    // (계획서 §1.9 — 그 둘이 먼저 나서 관례를 정했다).

    std::string dump_dock_tree(const chrome_snapshot& snapshot);
    std::string dump_theme(const chrome_snapshot& snapshot);
    std::string dump_fonts(const chrome_snapshot& snapshot);
    std::string dump_layout(const chrome_snapshot& snapshot);

    std::string dump_dock_audit(const chrome_snapshot& snapshot);
    std::string dump_theme_audit(const chrome_snapshot& snapshot);
    std::string dump_layout_audit(const chrome_snapshot& snapshot);
}
