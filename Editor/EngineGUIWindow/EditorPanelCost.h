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
        std::uint64_t totalUnits{};
        std::uint64_t totalScans{};
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
}
