#pragma once

// PHASE 21 W7-0 — 패널별 draw 비용 관측.
//
// W7 의 판정은 "p95 CPU·allocation 개선 수치를 기록한다. 캐시만 추가하고 실측
// 이득이 없으면 제거한다" 다. 그런데 패널 하나가 프레임에서 얼마를 쓰는지 낼
// 창구가 없었다 — `profile.stats` 는 프로파일러 **자체** 비용과 용량만 낸다.
// 재는 법을 먼저 세우지 않으면 어떤 캐시든 "빨라졌다" 를 증명할 수 없다.
//
// 두 가지를 함께 센다.
//
//   ① 시간 — 이 패널이 이번 프레임에 쓴 벽시계 시간. 여러 구간으로 나뉘어도
//      **누적**한다(Browser 는 트리와 목록이 서로 다른 자리에서 그려진다).
//   ② 일의 양 — 그린 행/항목 수(`units`)와 **디렉터리 스캔 수**(`scans`).
//      시간만 재면 기계가 빠른 날 캐시가 없는 것이 안 보인다. 캐시의 목적은
//      "프레임마다 하던 일을 안 하는 것" 이므로 그 일의 수를 직접 센다.
//
// 스레드 경계. `begin/end/add_*` 는 ImGui 프레임을 도는 Presentation 스레드
// 전용이고, `read_panel_costs`/`reset_panel_costs` 는 게임 스레드의 CLI 가
// 부른다. 그래서 프레임 누적치는 잠금 없이 쌓고 프레임 끝에서 한 번만
// 게시한다(`publish_panel_costs`) — `publish_viewport_demand` 와 같은 규약이다.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::windows
{
    enum class panel_cost_slot : std::uint8_t
    {
        hierarchy = 0,
        browser_tree,
        browser_files,
        count
    };

    inline constexpr std::size_t kPanelCostSlotCount =
        static_cast<std::size_t>(panel_cost_slot::count);

    /// 슬롯 이름. CLI 출력과 게이트 단정이 이 철자를 쓴다.
    const char* panel_cost_slot_name(panel_cost_slot slot) noexcept;

    struct panel_cost_sample
    {
        std::uint64_t frames{};      ///< 이 슬롯이 실제로 그려진 프레임 수
        double lastMs{};
        double avgMs{};              ///< 링에 남은 표본의 평균
        double p95Ms{};              ///< 링에 남은 표본의 95 백분위
        double maxMs{};              ///< 리셋 이후 최댓값
        std::uint64_t lastUnits{};   ///< 이번 프레임에 그린 행/항목 수
        std::uint64_t lastScans{};   ///< 이번 프레임의 디렉터리 스캔 수
        /// W7-5: 이번 프레임에 **스캔이 아닌** 디스크 접촉 수. `scans` 가 0 인데
        /// 이 수가 크면 캐시를 경유하지 않는 호출이 남아 있다는 뜻이다.
        std::uint64_t lastProbes{};
        std::uint64_t totalUnits{};
        std::uint64_t totalScans{};
        std::uint64_t totalProbes{};
        std::uint32_t samples{};     ///< 링에 담긴 표본 수
    };

    struct panel_cost_snapshot
    {
        std::array<panel_cost_sample, kPanelCostSlotCount> slots{};
        std::uint64_t publishedFrames{};  ///< 게시된 UI 프레임 수(리셋 이후)
    };

    // ── Presentation 스레드 전용 ────────────────────────────────────────────
    void begin_panel_cost(panel_cost_slot slot) noexcept;
    void end_panel_cost(panel_cost_slot slot) noexcept;
    void add_panel_units(panel_cost_slot slot, std::uint64_t units) noexcept;
    void add_panel_scans(panel_cost_slot slot, std::uint64_t scans) noexcept;
    /// W7-5: 스캔이 아닌 디스크 접촉(`browser_cache_stats::probes` 의 구간 델타).
    void add_panel_probes(panel_cost_slot slot, std::uint64_t probes) noexcept;

    /// 구간 누적 RAII. 같은 슬롯을 한 프레임에 여러 번 열어도 누적된다.
    struct panel_cost_scope
    {
        explicit panel_cost_scope(panel_cost_slot slot) noexcept : m_slot(slot)
        {
            begin_panel_cost(m_slot);
        }
        ~panel_cost_scope() { end_panel_cost(m_slot); }
        panel_cost_scope(const panel_cost_scope&) = delete;
        panel_cost_scope& operator=(const panel_cost_scope&) = delete;

    private:
        panel_cost_slot m_slot;
    };

    /// 프레임 끝에서 한 번. 누적치를 스냅샷으로 옮기고 프레임 누적을 비운다.
    void publish_panel_costs();

    // ── 게임 스레드(CLI) ────────────────────────────────────────────────────
    panel_cost_snapshot read_panel_costs();
    void reset_panel_costs();

    // ══ PHASE 21 W2-4 — 창 단위 비용과 프레임 총계 ═════════════════════════
    //
    // 위의 슬롯은 W7 이 자기 축을 재려고 세운 것이라 셋뿐이다(hierarchy ·
    // browser_tree · browser_files). 그래서 chrome 한 프레임이 1.1 ms 인데 그중
    // 0.015 ms 만 설명됐다(실측 2026-09-15). §8.2 는 *"p95 CPU 가 기준선 대비
    // 악화되면 **원인을 기록하고** 최적화 또는 rollback 한다"* 고 적었는데, 원인을
    // 기록할 수단이 없었다 — 판정문에 자가 없는 그 계통이다.
    //
    // ★ 슬롯과 **층위가 다르다.** 슬롯은 창 하나 **안**의 구간이고
    //   (`browser_tree` 와 `browser_files` 는 같은 창이다) 이쪽은 창 하나를
    //   그리는 전체다. 둘을 한 표에 더하면 같은 시간이 두 번 잡힌다. CLI 가
    //   두 표를 따로 낸다.
    //
    // 창은 셸이 한 자리에서 그린다(`EditorWindowHost`) — 창마다 계측을 흩지
    // 않고 그 루프 한 곳에 건다. 키는 선언 표의 색인이라 매 프레임 문자열을
    // 비교하지 않는다. 안정 식별자는 처음 한 번만 적는다.

    /// 창 하나의 구간. `index` 는 선언 표의 색인이고 `stable_id` 는 처음 한 번만
    /// 쓰인다. 같은 창을 한 프레임에 여러 번 열면 누적한다.
    void begin_window_cost(std::size_t index, const char* stable_id) noexcept;
    void end_window_cost(std::size_t index) noexcept;

    struct window_cost_scope
    {
        window_cost_scope(std::size_t index, const char* stable_id) noexcept
            : m_index(index)
        {
            begin_window_cost(m_index, stable_id);
        }
        ~window_cost_scope() { end_window_cost(m_index); }
        window_cost_scope(const window_cost_scope&) = delete;
        window_cost_scope& operator=(const window_cost_scope&) = delete;

    private:
        std::size_t m_index;
    };

    /// UI 프레임 전체에 든 CPU 시간. `BeginRender` 부터 `EndFrame` 뒤까지이고
    /// 창 비용의 합을 **담는 쪽**이다. 매 프레임 한 번 부른다.
    ///
    /// ★ 자기 링에 바로 넣는다. 이 값이 확정되는 자리가 `publish_panel_costs`
    ///   **뒤**라서(`ImGui::Render()` 가 끝나야 한다) 창 게시에 얹을 수 없다.
    void record_ui_frame_ms(double ms) noexcept;

    struct window_cost_sample
    {
        std::string   id;
        std::uint64_t frames{};
        double        lastMs{};
        double        avgMs{};
        double        p95Ms{};
        double        maxMs{};
        std::uint32_t samples{};
    };

    struct window_cost_snapshot
    {
        std::vector<window_cost_sample> windows;
        /// 프레임 총계. `id` 는 "(frame)" 이다 — 창 식별자와 겹치지 않는다.
        window_cost_sample frame;
    };

    // ── 셸과 ImGui 자체의 구간 ────────────────────────────────────────────
    //
    // 창 다섯을 전부 계측해도 설명된 것이 0.085 ms 뿐이고 0.797 ms 가 남았다
    // (실측 2026-09-15 — 프레임의 90%). 비용의 대부분은 창 본문이 아니라 셸과
    // ImGui 자체다. 그것을 열지 않으면 게이트는 "악화됐는데 어디인지 모른다" 를
    // 되풀이한다.
    //
    // ★ 창 표와 **겹치지 않게** 나눈다. 겹치면 합이 프레임 총계를 넘어 잔차가
    //   음수가 되고, 음수 잔차는 아무것도 말해 주지 않는다. 그래서 창을 그리는
    //   구간(`draw_windows`)은 이 열거에 없다 — 그 안은 창 표가 센다.
    enum class shell_cost_section : std::uint8_t
    {
        /// 호스트의 `BeginFrame`. ★ ImGui 의 `NewFrame` 만이 아니다 — 안에
        /// `m_renderer->Resize` 와 `m_renderer->NewFrame()` 이 있어 **RHI 프레임
        /// 자원 획득**(펜스 대기가 될 수 있다)이 함께 잡힌다. 이름을
        /// `imgui.*` 로 두면 다음 사람이 순수 UI CPU 로 읽는다.
        /// `BeforeFrame` 과 배율 적용 — 호스트 `BeginFrame` **앞**의 셸 준비.
        shell_beforeframe = 0,
        host_beginframe,
        shell_dockspace,      ///< 도크스페이스·메뉴바·툴바(BeginRender 의 나머지)
        shell_prewindows,     ///< Render() 에서 창 그리기 앞
        shell_postwindows,    ///< 창 그리기 뒤(워크스페이스 대화상자 등)
        shell_endrender,      ///< 장부 게시와 스냅샷 캡처
        /// 호스트의 `EndFrame` — `ImGui::Render()` 와 **`RenderAndPresent`**.
        /// ★ 프레임 총계 **밖**이다. 총계는 W0 이 기준선을 뜬 자(`ui_cpu_ms`)와
        ///   같은 구간이라 제출·Present 앞에서 끊긴다. 잔차 계산에서 빼고 따로
        ///   낸다 — 넣으면 "UI CPU" 가 GPU 제출을 포함하게 된다.
        present,
        count
    };

    /// `present` 줄의 식별자. CLI 가 잔차에서 이 줄을 가려낼 때 쓴다.
    inline constexpr const char* kPresentRowId = "(present)";

    void begin_shell_cost(shell_cost_section section) noexcept;
    void end_shell_cost(shell_cost_section section) noexcept;

    struct shell_cost_scope
    {
        explicit shell_cost_scope(shell_cost_section section) noexcept : m_section(section)
        {
            begin_shell_cost(m_section);
        }
        ~shell_cost_scope() { end_shell_cost(m_section); }
        shell_cost_scope(const shell_cost_scope&) = delete;
        shell_cost_scope& operator=(const shell_cost_scope&) = delete;

    private:
        shell_cost_section m_section;
    };

    window_cost_snapshot read_window_costs();
    void reset_window_costs();
}
