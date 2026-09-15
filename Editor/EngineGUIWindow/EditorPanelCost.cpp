#include "EditorPanelCost.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    using namespace editor::windows;
    using clock_type = std::chrono::steady_clock;

    /// 링 길이. p95 를 낼 만큼 길고, 잠깐의 스파이크가 영원히 남지 않을 만큼 짧다.
    /// 60fps 기준 약 8.5 초다. 숫자를 게이트에 적지 않는다 — 게이트는 표본 수를
    /// 읽어서 판단한다.
    constexpr std::size_t kRingCapacity = 512;

    struct slot_state
    {
        // ── Presentation 스레드만 만진다 ──
        clock_type::time_point frameStart{};
        int depth{};                  ///< 중첩을 허용하되 가장 바깥만 잰다
        double frameMs{};
        std::uint64_t frameUnits{};
        std::uint64_t frameScans{};
        std::uint64_t frameProbes{};
        bool touchedThisFrame{};

        // ── 잠금 아래 ──
        std::vector<double> ring;
        std::size_t ringNext{};
        std::uint64_t frames{};
        double lastMs{};
        double maxMs{};
        std::uint64_t lastUnits{};
        std::uint64_t lastScans{};
        std::uint64_t lastProbes{};
        std::uint64_t totalUnits{};
        std::uint64_t totalScans{};
        std::uint64_t totalProbes{};
    };

    std::mutex& cost_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::array<slot_state, kPanelCostSlotCount>& slots()
    {
        static std::array<slot_state, kPanelCostSlotCount> state;
        return state;
    }

    std::uint64_t& published_frames()
    {
        static std::uint64_t frames = 0;
        return frames;
    }

    bool valid(panel_cost_slot slot) noexcept
    {
        return static_cast<std::size_t>(slot) < kPanelCostSlotCount;
    }

    slot_state& state_of(panel_cost_slot slot) noexcept
    {
        return slots()[static_cast<std::size_t>(slot)];
    }

    // ── W2-4: 창 단위 장부 ────────────────────────────────────────────────
    //
    // 창 수는 선언 표가 정하고 프로세스 수명 동안 늘기만 한다. 색인으로 접근해
    // 매 프레임 문자열을 비교하지 않는다.
    struct window_state
    {
        // ── Presentation 스레드만 만진다 ──
        clock_type::time_point frameStart{};
        int depth{};
        double frameMs{};
        bool touchedThisFrame{};

        // ── 잠금 아래 ──
        std::string id;
        std::vector<double> ring;
        std::size_t ringNext{};
        std::uint64_t frames{};
        double lastMs{};
        double maxMs{};
    };

    /// 셸 구간이 쓰는 색인의 시작. 창 색인은 선언 표의 자리(수십)이고 이쪽은
    /// 그 뒤에 둔다 — 한 표를 쓰면 읽는 쪽이 한 번만 훑고 잔차 계산이 한 자리에
    /// 모인다. 상한을 넉넉히 잡는 대신 빈 행은 읽을 때 걸러진다.
    constexpr std::size_t kShellCostBase = 256;

    const char* shell_section_name(shell_cost_section section) noexcept
    {
        switch (section)
        {
        case shell_cost_section::shell_beforeframe:return "(shell.beforeframe)";
        case shell_cost_section::host_beginframe:  return "(host.beginframe)";
        case shell_cost_section::shell_dockspace:  return "(shell.dockspace)";
        case shell_cost_section::shell_prewindows: return "(shell.prewindows)";
        case shell_cost_section::shell_postwindows:return "(shell.postwindows)";
        case shell_cost_section::shell_endrender:  return "(shell.endrender)";
        case shell_cost_section::present:          return kPresentRowId;
        case shell_cost_section::count:            break;
        }
        return "(shell.unknown)";
    }

    std::vector<window_state>& window_states()
    {
        static std::vector<window_state> state;
        return state;
    }

    /// 프레임 총계. 창 장부와 링 규약이 같지만 게시 자리가 달라 따로 둔다.
    window_state& ui_frame()
    {
        static window_state state;
        return state;
    }

    /// 링에 담긴 표본의 95 백분위. 표본이 비면 0 이다.
    double percentile95(const std::vector<double>& ring)
    {
        if (ring.empty()) return 0.0;
        std::vector<double> sorted = ring;
        // 95 백분위의 자리. 표본 하나면 그 값이다.
        const std::size_t index =
            std::min(sorted.size() - 1, static_cast<std::size_t>(sorted.size() * 0.95));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(index), sorted.end());
        return sorted[index];
    }
}

namespace editor::windows
{
    const char* panel_cost_slot_name(panel_cost_slot slot) noexcept
    {
        switch (slot)
        {
        case panel_cost_slot::hierarchy:     return "hierarchy";
        case panel_cost_slot::browser_tree:  return "browser_tree";
        case panel_cost_slot::browser_files: return "browser_files";
        case panel_cost_slot::count:         break;
        }
        return "unknown";
    }

    void begin_panel_cost(panel_cost_slot slot) noexcept
    {
        if (!valid(slot)) return;
        slot_state& state = state_of(slot);
        // 중첩은 가장 바깥만 잰다. 안쪽까지 재면 같은 시간을 두 번 더한다.
        if (state.depth++ == 0) state.frameStart = clock_type::now();
        state.touchedThisFrame = true;
    }

    void end_panel_cost(panel_cost_slot slot) noexcept
    {
        if (!valid(slot)) return;
        slot_state& state = state_of(slot);
        if (state.depth <= 0) return;   // 짝이 맞지 않는 end 는 버린다
        if (--state.depth != 0) return;
        const auto elapsed = clock_type::now() - state.frameStart;
        state.frameMs += std::chrono::duration<double, std::milli>(elapsed).count();
    }

    void add_panel_units(panel_cost_slot slot, std::uint64_t units) noexcept
    {
        if (!valid(slot)) return;
        slot_state& state = state_of(slot);
        state.frameUnits += units;
        state.touchedThisFrame = true;
    }

    void add_panel_scans(panel_cost_slot slot, std::uint64_t scans) noexcept
    {
        if (!valid(slot)) return;
        slot_state& state = state_of(slot);
        state.frameScans += scans;
        state.touchedThisFrame = true;
    }

    void add_panel_probes(panel_cost_slot slot, std::uint64_t probes) noexcept
    {
        if (!valid(slot)) return;
        slot_state& state = state_of(slot);
        state.frameProbes += probes;
        state.touchedThisFrame = true;
    }

    void publish_panel_costs()
    {
        std::lock_guard lock(cost_mutex());
        ++published_frames();
        for (auto& state : slots())
        {
            // 그리지 않은 패널(닫혀 있거나 접힌 창)은 표본을 남기지 않는다.
            // 0 을 쌓으면 평균과 p95 가 "닫혀 있던 프레임" 으로 희석된다.
            if (!state.touchedThisFrame)
            {
                state.depth = 0;
                continue;
            }
            ++state.frames;
            state.lastMs = state.frameMs;
            state.maxMs = (std::max)(state.maxMs, state.frameMs);
            state.lastUnits = state.frameUnits;
            state.lastScans = state.frameScans;
            state.lastProbes = state.frameProbes;
            state.totalUnits += state.frameUnits;
            state.totalScans += state.frameScans;
            state.totalProbes += state.frameProbes;
            if (state.ring.size() < kRingCapacity)
            {
                state.ring.push_back(state.frameMs);
                state.ringNext = state.ring.size() % kRingCapacity;
            }
            else
            {
                state.ring[state.ringNext] = state.frameMs;
                state.ringNext = (state.ringNext + 1) % kRingCapacity;
            }
            state.depth = 0;
            state.frameMs = 0.0;
            state.frameUnits = 0;
            state.frameScans = 0;
            state.frameProbes = 0;
            state.touchedThisFrame = false;
        }

        // W2-4: 창 장부도 같은 자리에서 게시한다. 이번 프레임의 모든 창 본문이
        // 끝났고, 읽는 쪽은 게임 스레드다.
        for (window_state& state : window_states())
        {
            if (!state.touchedThisFrame)
            {
                state.depth = 0;
                continue;
            }
            ++state.frames;
            state.lastMs = state.frameMs;
            state.maxMs = (std::max)(state.maxMs, state.frameMs);
            if (state.ring.size() < kRingCapacity)
            {
                state.ring.push_back(state.frameMs);
                state.ringNext = state.ring.size() % kRingCapacity;
            }
            else
            {
                state.ring[state.ringNext] = state.frameMs;
                state.ringNext = (state.ringNext + 1) % kRingCapacity;
            }
            state.depth = 0;
            state.frameMs = 0.0;
            state.touchedThisFrame = false;
        }
    }

    panel_cost_snapshot read_panel_costs()
    {
        std::lock_guard lock(cost_mutex());
        panel_cost_snapshot snapshot;
        snapshot.publishedFrames = published_frames();
        for (std::size_t i = 0; i < kPanelCostSlotCount; ++i)
        {
            const slot_state& state = slots()[i];
            panel_cost_sample& out = snapshot.slots[i];
            out.frames = state.frames;
            out.lastMs = state.lastMs;
            out.maxMs = state.maxMs;
            out.lastUnits = state.lastUnits;
            out.lastScans = state.lastScans;
            out.lastProbes = state.lastProbes;
            out.totalUnits = state.totalUnits;
            out.totalScans = state.totalScans;
            out.totalProbes = state.totalProbes;
            out.samples = static_cast<std::uint32_t>(state.ring.size());
            if (!state.ring.empty())
            {
                double sum = 0.0;
                for (double value : state.ring) sum += value;
                out.avgMs = sum / static_cast<double>(state.ring.size());
                out.p95Ms = percentile95(state.ring);
            }
        }
        return snapshot;
    }

    // ══ W2-4 — 창 단위 비용과 프레임 총계 ══════════════════════════════════

    void begin_window_cost(std::size_t index, const char* stable_id) noexcept
    {
        std::vector<window_state>& table = window_states();
        if (index >= table.size())
        {
            // 표를 키우는 것은 드문 일(창 선언은 시작에 한 번)이라 여기서 잠근다.
            // 읽는 쪽이 벡터 재할당 중에 훑으면 안 되기 때문이다.
            std::lock_guard lock(cost_mutex());
            table.resize(index + 1);
        }
        window_state& state = table[index];
        if (state.id.empty() && nullptr != stable_id)
        {
            std::lock_guard lock(cost_mutex());
            state.id = stable_id;
        }
        if (state.depth++ == 0) state.frameStart = clock_type::now();
        state.touchedThisFrame = true;
    }

    void end_window_cost(std::size_t index) noexcept
    {
        std::vector<window_state>& table = window_states();
        if (index >= table.size()) return;
        window_state& state = table[index];
        if (state.depth <= 0) return;
        if (--state.depth != 0) return;
        const auto elapsed = clock_type::now() - state.frameStart;
        state.frameMs += std::chrono::duration<double, std::milli>(elapsed).count();
    }

    void begin_shell_cost(shell_cost_section section) noexcept
    {
        if (section >= shell_cost_section::count) return;
        begin_window_cost(kShellCostBase + static_cast<std::size_t>(section),
                          shell_section_name(section));
    }

    void end_shell_cost(shell_cost_section section) noexcept
    {
        if (section >= shell_cost_section::count) return;
        end_window_cost(kShellCostBase + static_cast<std::size_t>(section));
    }

    void record_ui_frame_ms(double ms) noexcept
    {
        window_state& state = ui_frame();
        std::lock_guard lock(cost_mutex());
        if (state.id.empty()) state.id = "(frame)";
        ++state.frames;
        state.lastMs = ms;
        state.maxMs = (std::max)(state.maxMs, ms);
        if (state.ring.size() < kRingCapacity)
        {
            state.ring.push_back(ms);
            state.ringNext = state.ring.size() % kRingCapacity;
        }
        else
        {
            state.ring[state.ringNext] = ms;
            state.ringNext = (state.ringNext + 1) % kRingCapacity;
        }
    }

    window_cost_snapshot read_window_costs()
    {
        std::lock_guard lock(cost_mutex());
        window_cost_snapshot snapshot;
        const auto fill = [](const window_state& state, window_cost_sample& out)
        {
            out.id = state.id;
            out.frames = state.frames;
            out.lastMs = state.lastMs;
            out.maxMs = state.maxMs;
            out.samples = static_cast<std::uint32_t>(state.ring.size());
            if (!state.ring.empty())
            {
                double sum = 0.0;
                for (double value : state.ring) sum += value;
                out.avgMs = sum / static_cast<double>(state.ring.size());
                out.p95Ms = percentile95(state.ring);
            }
        };
        for (const window_state& state : window_states())
        {
            // 한 번도 그려지지 않은 창은 표에 내지 않는다 — 0 행은 "닫혀 있다"
            // 와 "계측이 안 걸렸다" 를 구별하지 못한다.
            if (state.id.empty() || 0 == state.frames) continue;
            window_cost_sample out;
            fill(state, out);
            snapshot.windows.push_back(std::move(out));
        }
        fill(ui_frame(), snapshot.frame);
        return snapshot;
    }

    void reset_window_costs()
    {
        std::lock_guard lock(cost_mutex());
        const auto clear = [](window_state& state)
        {
            state.ring.clear();
            state.ringNext = 0;
            state.frames = 0;
            state.lastMs = 0.0;
            state.maxMs = 0.0;
        };
        for (window_state& state : window_states()) clear(state);
        clear(ui_frame());
    }

    void reset_panel_costs()
    {
        std::lock_guard lock(cost_mutex());
        published_frames() = 0;
        for (auto& state : slots())
        {
            // 프레임 누적은 건드리지 않는다 — 지금 그리고 있는 프레임의 중간일
            // 수 있고, 그 프레임은 다음 게시에서 정상적으로 끝난다.
            state.ring.clear();
            state.ringNext = 0;
            state.frames = 0;
            state.lastMs = 0.0;
            state.maxMs = 0.0;
            state.lastUnits = 0;
            state.lastScans = 0;
            state.lastProbes = 0;
            state.totalUnits = 0;
            state.totalScans = 0;
            state.totalProbes = 0;
        }
    }
}
