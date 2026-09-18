#pragma once

// PHASE 21 W2-V0 후속 — 예열이 **어디까지 갔는지**를 밖에서 읽는다.
//
// ── 왜 필요한가 ───────────────────────────────────────────────────────────
//
// 에디터는 창이 뜬 뒤에도 한참 동안 쓸 수 없다. 2026-09-14 실측(Release,
// 2880x1665, 셰이더 캐시 warm)으로 창은 1.7s 에 뜨고 씬뷰 첫 그림은 18.5s 다.
// 그 사이 렌더 스레드의 `BuildPipeline` 이 `displayLifetimeMutex` 를 구축 내내
// 쥐고, 씬뷰 본문이 같은 락을 잡으려다 게임 스레드까지 멈춘다 — 메시지 펌프가
// 죽어 운영체제가 '응답 없음' 을 그린다.
//
// 그 구간에 대해 지금 답할 수 있는 것은 **"끝났는가"** 뿐이었다
// (`render.live.wait`). 어디까지 갔는지, 지금 무엇을 기다리는지, 어느 단계가
// 오래 걸렸는지는 임시 계측을 붙여야만 보였다(scratchpad `instrumentation.patch`
// 의 `BootTimeline`). 붙였다 떼는 계측은 다음 사람에게 남지 않는다.
//
// ── 왜 원자만 쓰는가 ──────────────────────────────────────────────────────
//
// 이 장부를 읽는 쪽은 HTTP 서비스의 **수신 스레드**다. 게임 스레드는 위 구간에서
// 멈춰 있으므로, 큐를 타는 창구(`POST /command`)로는 예열을 볼 수 없다 — 볼 수
// 있게 되는 시점은 이미 예열이 끝난 뒤다. `ICommandGateway::Health` 가 "게임
// 스레드가 멈춰 있어도 답해야 한다 · 구현은 원자 변수만 읽어야 한다" 를 계약으로
// 적은 것과 같은 이유이고, 이 장부도 같은 규율을 따른다. 잠금이 없으므로 기록도
// 렌더 스레드에서 그대로 부를 수 있다.
//
// ── 단계는 왜 고정 배열인가 ───────────────────────────────────────────────
//
// 이름을 런타임에 만들면 읽는 쪽이 무엇을 기다려야 하는지 **미리 알 수 없다.**
// 아직 도달하지 않은 단계도 목록에 있어야 "무엇이 남았는가" 를 답할 수 있으므로,
// 단계는 컴파일 시점에 닫힌 집합이다.

#include <cstdint>
#include <cstddef>

namespace engine::warmup
{
    /// 예열 단계. 순서대로 도달하는 것이 정상이지만 **강제하지 않는다** —
    /// 건너뛴 단계가 생기면 그 사실 자체가 읽는 쪽에 보여야 한다.
    enum class stage : std::uint32_t
    {
        process,            ///< 장부가 선 시각. 늘 0 이고 나머지의 기준이다.
        window_shown,       ///< 운영체제 창이 보인 때
        first_ui_frame,     ///< 첫 UI 프레임을 끝낸 때
        render_pipeline,    ///< 렌더 파이프라인 구축이 끝난 때
        shader_meta,        ///< 셰이더 반영·재질 밀봉이 끝난 때
        first_live_frame,   ///< 라이브 프레임 하나가 **끝난** 때
        display_texture,    ///< 표시 텍스처가 처음 유효해진 때
        scene_canvas,       ///< 씬뷰 캔버스가 처음 유효해진 때
        count
    };

    inline constexpr std::size_t kStageCount = static_cast<std::size_t>(stage::count);

    /// 단계 이름. 읽는 쪽(HTTP·CLI)이 그대로 쓴다.
    const char* stage_name(stage s) noexcept;

    struct stage_view
    {
        const char*   name{ "" };
        bool          reached{ false };
        double        atMs{ 0.0 };    ///< `process` 로부터의 경과. 미도달이면 0.
    };

    struct ledger_view
    {
        stage_view    stages[kStageCount]{};
        std::size_t   reachedCount{ 0 };
        double        elapsedMs{ 0.0 };   ///< 지금까지의 총 경과
        /// 마지막으로 도달한 단계. 아무것도 없으면 `process`.
        stage         last{ stage::process };
        /// 그 단계에 도달한 뒤 흐른 시간. 예열이 멈춰 있으면 이 수가 자란다 —
        /// "지금 무엇을 기다리는가" 에 답하는 값이다.
        double        sinceLastMs{ 0.0 };
        bool          complete{ false };  ///< 모든 단계에 도달했다
    };

    /// 단계에 처음 도달했음을 남긴다. 같은 단계를 두 번 불러도 **첫 번째만**
    /// 남는다 — 예열은 한 번 지나가는 길이고, 뒤의 호출은 재입장이거나
    /// 리사이즈 같은 다른 사건이다.
    void mark(stage s) noexcept;

    /// 읽는다. 원자 읽기만 한다.
    ledger_view read() noexcept;
}
