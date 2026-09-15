#pragma once

// PHASE 21 W8-3 — 검증 레이어가 말한 것을 **셈으로** 남긴다.
//
// 계획서 W8 의 판정은 *"현재 범위인 DX12에서 검증 레이어 오류/비정상 종료 0"* 이다.
// 그런데 그 수를 출하 구성에서 **읽을 수단이 없었다.**
//
//   · `CREATOR_DX12_VALIDATION=basic` 은 Release 에서도 디버그 레이어를 켠다
//     (실측: `[DX12 검증] DebugLayer=on` 두 줄 — 셸과 렌더러가 각각 디바이스를 만든다).
//   · 그런데 `DX12DeviceResources::DrainDebugMessages` 는 통째로 `#if defined(_DEBUG)`
//     였고 호출부들도 같은 가드 안에 있었다. **레이어를 켜 놓고 아무도 안 읽었다.**
//
// 그 상태의 "오류 0" 은 빈 집합을 성공으로 읽는 것이다. W8-1 의
// `IMGUI_CHECKVERSION()` 과 같은 모양 — 출하 구성에서 힘이 0 인 검사다.
//
// ── 왜 장부가 따로 서는가 ──────────────────────────────────────────────────
//
// 큐를 비우는 자리는 하나가 아니다. 에디터 셸의 디바이스와 라이브 렌더러의
// 디바이스가 **서로 다른 InfoQueue** 를 갖고, 오프라인 PBR 캡처도 같은 함수를
// 부른다. 각자 세면 밖에서 합칠 수 없고, 어느 자리가 안 세는지도 알 수 없다.
// 그래서 세는 자리를 `DrainDebugMessages` **안** 한 곳으로 모은다 — 큐를 비우는
// 길이 그 함수 하나뿐이므로, 여기로 들어오지 않는 메시지는 애초에 없다.
//
// ── 무엇을 담는가 ─────────────────────────────────────────────────────────
//
// 수만 담으면 "몇 건" 만 알고 **무엇인지** 모른다. 처음 몇 건의 문구를 함께 남긴다
// (골든이 이름을 전부 찍어야 하는 것과 같은 이유다). 뒤쪽은 버린다 — 한 번
// 터지면 같은 메시지가 프레임마다 쌓이므로 전부 담으면 장부가 로그가 된다.
//
// ── 스레드 ────────────────────────────────────────────────────────────────
//
// 기록은 렌더/표시 스레드가, 읽기는 게임 스레드의 CLI 가 한다. 잠금 하나로 가른다.
// 프레임마다 도는 자리지만 레이어가 꺼져 있으면 `DrainDebugMessages` 가 큐 포인터
// 하나를 보고 즉시 돌아오므로 이 잠금까지 오지 않는다.

#include <cstdint>
#include <string>
#include <vector>

namespace rhi::validation
{
    /// 장부가 담아 두는 문구의 최대 건수.
    inline constexpr std::size_t kRetainedMessages = 16;

    struct ledger_view
    {
        /// 디버그 레이어를 **켜고 디바이스를 만든** 적이 있는가. 이 값이 거짓이면
        /// `problems == 0` 은 아무것도 증명하지 않는다 — 판정하는 쪽이 이것을
        /// 먼저 봐야 눈먼 초록이 되지 않는다.
        bool layerEnabled{ false };

        /// 마지막으로 선언된 모드 문자열(off · basic · gpu).
        std::string mode{ "off" };

        /// 디바이스를 몇 개나 그 모드로 세웠는가. 에디터는 둘이다(셸·렌더러).
        std::uint64_t devices{ 0 };

        std::uint64_t drains{ 0 };     ///< 큐를 비운 횟수(메시지가 0 건이어도 센다)
        std::uint64_t messages{ 0 };   ///< 비우면서 본 메시지 총수(INFO 포함)
        std::uint64_t problems{ 0 };   ///< 그중 CORRUPTION · ERROR · WARNING

        std::vector<std::string> retained;  ///< 처음 kRetainedMessages 건의 문구
        std::uint64_t droppedMessages{ 0 }; ///< 자리가 없어 버린 문구 수
    };

    /// 디바이스를 세운 쪽이 레이어 상태를 선언한다. `enabled` 가 참인 선언이
    /// 한 번이라도 있으면 장부의 `layerEnabled` 가 참으로 남는다 — 하나는 켜고
    /// 하나는 끈 실행을 "켰다" 로 읽는 것이 맞다. 그 편이 판정에 안전하다.
    void declare_layer(bool enabled, const char* mode);

    /// `DrainDebugMessages` 가 부른다. `text` 는 이번에 비운 메시지 전문이다.
    void record_drain(std::uint32_t problems, std::uint32_t messages, const std::string& text);

    /// CLI 가 읽는다.
    ledger_view read();

    /// 수만 비운다 — 레이어 선언(`layerEnabled` · `mode` · `devices`)은 남긴다.
    /// 그것은 이 실행의 성질이고 구간마다 달라지는 값이 아니다.
    void reset_counts();
}
