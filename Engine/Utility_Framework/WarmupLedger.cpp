#include "WarmupLedger.h"

#include <atomic>
#include <chrono>

namespace engine::warmup
{
    namespace
    {
        using clock = std::chrono::steady_clock;

        /// 기준 시각. **정적 초기화 때** 선다.
        ///
        /// 프로세스 생성 시각(`GetProcessTimes`)이 아니다. 둘의 차이는 로더가
        /// 이미지와 의존 DLL 을 올리는 동안이고, 그 구간은 이 코드가 아직 없어서
        /// 어차피 단계를 찍을 수 없다. 기준을 여기 두면 모든 값이 "장부가 선
        /// 뒤로 얼마" 라는 한 뜻을 갖는다.
        const clock::time_point g_origin = clock::now();

        /// 단계마다 도달 시각(마이크로초). -1 은 미도달.
        ///
        /// 마이크로초 정수로 담는 이유는 `atomic<double>` 이 플랫폼에 따라 락을
        /// 쓸 수 있기 때문이다 — 이 장부는 렌더 스레드가 멈춘 구간에도 기록·조회가
        /// 되어야 하므로 락이 섞이면 안 된다.
        std::atomic<std::int64_t> g_reached[kStageCount];

        struct Init
        {
            Init() noexcept
            {
                for (std::size_t i = 0; i < kStageCount; ++i)
                {
                    g_reached[i].store(-1, std::memory_order_relaxed);
                }
                // `process` 는 장부가 선 그 순간이다.
                g_reached[static_cast<std::size_t>(stage::process)].store(0, std::memory_order_release);
            }
        };
        const Init g_init{};

        std::int64_t now_micros() noexcept
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - g_origin).count();
        }

        constexpr const char* kNames[kStageCount]{
            "process",
            "window_shown",
            "first_ui_frame",
            "render_pipeline",
            "shader_meta",
            "first_live_frame",
            "display_texture",
            "scene_canvas",
        };
    }

    const char* stage_name(stage s) noexcept
    {
        const auto index = static_cast<std::size_t>(s);
        return index < kStageCount ? kNames[index] : "";
    }

    void mark(stage s) noexcept
    {
        const auto index = static_cast<std::size_t>(s);
        if (index >= kStageCount) return;

        std::int64_t expected = -1;
        // 첫 도달만 남긴다. 경합에서 진 쪽은 아무것도 하지 않는다 — 두 스레드가
        // 같은 단계를 동시에 찍으면 **먼저 쓴 값**이 그 단계의 시각이다.
        g_reached[index].compare_exchange_strong(expected, now_micros(),
            std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    ledger_view read() noexcept
    {
        ledger_view view{};
        const double elapsed = static_cast<double>(now_micros()) / 1000.0;
        view.elapsedMs = elapsed;

        double lastAt = 0.0;
        for (std::size_t i = 0; i < kStageCount; ++i)
        {
            const std::int64_t at = g_reached[i].load(std::memory_order_acquire);
            view.stages[i].name = kNames[i];
            view.stages[i].reached = at >= 0;
            view.stages[i].atMs = at >= 0 ? static_cast<double>(at) / 1000.0 : 0.0;
            if (at >= 0)
            {
                ++view.reachedCount;
                // 마지막으로 **도달한** 단계는 번호가 가장 큰 것이 아니라 시각이
                // 가장 늦은 것이다. 단계를 건너뛰고 뒤엣것이 먼저 찍히는 실행에서
                // 번호로 고르면 "지금 무엇을 기다리는가" 가 거꾸로 나온다.
                if (view.stages[i].atMs >= lastAt)
                {
                    lastAt = view.stages[i].atMs;
                    view.last = static_cast<stage>(i);
                }
            }
        }
        view.sinceLastMs = elapsed - lastAt;
        view.complete = view.reachedCount == kStageCount;
        return view;
    }
}
