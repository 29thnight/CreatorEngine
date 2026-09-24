#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "../../Utility_Framework/Uuid.h"

// wave — 오디오 배선의 새 계층. vendor 타입이 이 헤더를 넘지 않는다.
//
// ★ 왜 새로 쓰는가. 옛 배선은 "재생 중인 것이 무엇인가" 의 정본을 백엔드에 두고
//   매번 ChannelGroup 을 열거해 물었다. 그래서 cap·steal·정지·수명이 전부 O(n)
//   백엔드 왕복이 되고, 컴포넌트가 raw 채널 포인터를 들고 있어 재활용된 핸들로
//   남의 소리를 만질 수 있었다. 근거와 실측은
//   docs/analysis/Phase22AudioPreflight.md 에 있다.
//
// ★★ 이름은 새로 짓지 않았다. `VoiceHandle` · `PlayRequest` · `BusId` 는
//   docs/plans/AudioBackendModernizationPlan.md §3.1 이 이미 세운 이름이다.
//   네임스페이스가 `wave` 라 접두사 `Audio` 는 떼어 더듬지 않게 했다.
namespace wave
{
    // 재생 중인 논리 보이스 하나를 가리키는 값. index 는 슬롯, generation 은 그
    // 슬롯의 몇 번째 사용인지다.
    //
    // ★ generation 이 있는 이유. 옛 배선은 `FMOD::Channel*` 를 컴포넌트가 들고
    //   있었는데, 백엔드는 채널을 재활용한다. 정지된 뒤 같은 주소가 다른 소리에
    //   쓰이면 옛 주인의 `setVolume` 이 남의 소리를 바꾼다. 세대를 함께 들면
    //   "그 슬롯은 이미 다른 사용" 임을 값만 보고 거부할 수 있다.
    //
    // generation 0 은 언제나 무효다 — 값 초기화한 핸들이 유효해 보이면 안 된다.
    // AudioHost 안에서는 상위 비트를 Null fallback 영역으로 예약한다. 장치가
    // 복구돼 runtime 이 바뀌어도 이전 Null 핸들이 새 장치 보이스에 닿지 않는다.
    struct VoiceHandle final
    {
        std::uint32_t index{ 0 };
        std::uint32_t generation{ 0 };

        [[nodiscard]] constexpr bool IsValid() const noexcept { return 0 != generation; }
    };

    [[nodiscard]] constexpr bool operator==(VoiceHandle left, VoiceHandle right) noexcept
    {
        return left.index == right.index && left.generation == right.generation;
    }

    [[nodiscard]] constexpr bool operator!=(VoiceHandle left, VoiceHandle right) noexcept
    {
        return !(left == right);
    }

    // 믹스 그래프의 버스 하나.
    //
    // ★ 값을 wire 나 cooked identity 로 쓰지 않는다. 저작 파일에 저장되는 것은
    //   여전히 `ChannelType` 이고, 그것을 이 안정 id 로 사상하는 자는 서비스다.
    //   버스를 추가·재배치해도 저장된 씬이 흔들리지 않게 하려는 분리다.
    struct BusId final
    {
        std::uint16_t value{ 0 };

        [[nodiscard]] constexpr bool IsValid() const noexcept { return 0 != value; }
    };

    [[nodiscard]] constexpr bool operator==(BusId left, BusId right) noexcept
    {
        return left.value == right.value;
    }

    [[nodiscard]] constexpr bool operator!=(BusId left, BusId right) noexcept
    {
        return !(left == right);
    }

    // 클립을 가리키는 참조.
    //
    // AU2 이행 중에는 legacy filename stem과 cooked GUID를 구분해 보관한다.
    // 같은 글자라도 두 종류의 키는 같지 않다. 저작 씬 참조 전환은 AU7에서
    // 진행하며 cooked loader는 manifest GUID만 사용한다.
    class ClipKey final
    {
    public:
        ClipKey() = default;
        explicit ClipKey(std::string text) noexcept : m_text(std::move(text)) {}

        [[nodiscard]] static ClipKey FromGuid(const Uuid::Uuid16& guid)
        {
            ClipKey key;
            if (!guid.IsNil())
            {
                key.m_guid = guid;
                key.m_text = Uuid::ToString(guid);
                key.m_isGuid = true;
            }
            return key;
        }

        [[nodiscard]] const std::string& Text() const noexcept { return m_text; }
        [[nodiscard]] bool IsGuid() const noexcept { return m_isGuid; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_text.empty(); }

        [[nodiscard]] bool operator==(const ClipKey& other) const noexcept
        {
            return m_isGuid == other.m_isGuid && (m_isGuid
                ? m_guid == other.m_guid : m_text == other.m_text);
        }

        [[nodiscard]] bool operator!=(const ClipKey& other) const noexcept
        {
            return !(*this == other);
        }

    private:
        std::string m_text;
        Uuid::Uuid16 m_guid{};
        bool m_isGuid{ false };
    };

    // 거리 감쇠 곡선의 종류. 저작 값이라 순서를 바꾸지 않는다.
    enum class RolloffKind : std::uint8_t
    {
        Linear = 0,
        Inverse,
        Custom,
    };

    // 논리 보이스의 현재 상태.
    //
    // Virtual 은 "정책이 소리를 내주지 않기로 했지만 재생 위치는 유지한다" 는 뜻이다.
    // 골격 단계에서는 상태를 들고만 있고 가상화 전환은 아직 하지 않는다.
    enum class VoiceState : std::uint8_t
    {
        Free = 0,
        Pending,
        Physical,
        Virtual,
        Paused,
    };

    // 백엔드가 자기 내부 자원을 가리키려고 쓰는 불투명 값.
    //
    // ★ 여기에 포인터를 담아도 이 헤더는 vendor 타입을 모른다 — 그것이 요점이다.
    //   상위 계층은 이 값을 **비교·전달만** 하고 절대 역참조하지 않는다.
    struct BackendVoiceId final
    {
        std::uint64_t value{ 0 };

        [[nodiscard]] constexpr bool IsValid() const noexcept { return 0 != value; }
    };
}

template <>
struct std::hash<wave::ClipKey>
{
    [[nodiscard]] std::size_t operator()(const wave::ClipKey& key) const noexcept
    {
        const std::size_t value = std::hash<std::string>{}(key.Text());
        return value ^ (key.IsGuid() ? std::size_t{ 0x9e3779b9u } : 0u);
    }
};
