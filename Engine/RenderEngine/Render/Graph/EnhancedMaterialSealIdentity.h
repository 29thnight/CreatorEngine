#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <string_view>

// W8 — 한 프레임 안에서 material/texture/sampler/descriptor/PSO 세대가 섞이지
// 않게 하려면 먼저 "이 스냅샷이 무엇인가"를 값으로 적을 수 있어야 한다.
//
// ★ 포인터 신원은 값이 아니다. sealing의 중복 제거 키가 legacy `Material*`
//   하나였기 때문에, 같은 머테리얼 자산을 공유하면서 인스턴스 override가 다른
//   두 draw가 같은 주소를 주고 먼저 밀봉된 쪽의 스냅샷을 함께 썼다.
//   `DataSystem::Materials`가 이름으로 캐시한 같은 객체를 돌려주므로 이 조합은
//   특별한 것이 아니라 기본값이다. 값 digest가 그 자리를 대신한다.
struct EnhancedSealDigest
{
    static constexpr std::uint64_t Offset = 1469598103934665603ull;
    static constexpr std::uint64_t Prime = 1099511628211ull;

    std::uint64_t state{ Offset };

    void Bytes(const void* data, std::size_t size) noexcept
    {
        const auto* cursor = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i)
        {
            state ^= static_cast<std::uint64_t>(cursor[i]);
            state *= Prime;
        }
    }

    void U64(std::uint64_t value) noexcept { Bytes(&value, sizeof(value)); }
    void U32(std::uint32_t value) noexcept { Bytes(&value, sizeof(value)); }
    void Text(std::string_view text) noexcept { Bytes(text.data(), text.size()); }

    // -0.0과 +0.0은 같은 값이고, NaN은 비교 자체가 성립하지 않는다. 둘 다
    // 정규화하지 않으면 같은 재질이 프레임마다 다른 digest를 받을 수 있다.
    void F32(float value) noexcept
    {
        if (std::isnan(value)) { U32(0x7FC00000u); return; }
        if (0.f == value) value = 0.f;
        std::uint32_t bits{};
        std::memcpy(&bits, &value, sizeof(bits));
        U32(bits);
    }

    // 0은 "digest 없음"의 뜻으로 예약한다 — 빈 상태가 둘이 되지 않게 한다.
    [[nodiscard]] std::uint64_t Value() const noexcept
    {
        return 0ull == state ? Offset : state;
    }
};

// 밀봉한 쪽이 적고, 그리는 쪽이 대조하는 신원.
//
// `sealHash`는 스냅샷 값 전체의 digest이고 프레임과 무관하다 — 같은 값이면
// 프레임이 달라도 같다. `frameId`/`sceneEpoch`는 그 값이 **언제** 밀봉됐는지를
// 따로 적는다. 둘을 한 수에 섞으면 "값이 같은가"와 "이번 프레임 것인가"를
// 나눠 물을 수 없다.
struct EnhancedMaterialSealIdentity
{
    std::uint64_t sealHash{};
    std::uint64_t authoredDigest{};
    std::uint64_t authoredRevision{};
    std::uint64_t modelGeneration{};
    std::uint64_t sceneEpoch{};
    std::uint64_t frameId{};

    // 제품 sealing만 frameId를 적는다. 0은 "밀봉 출처가 제품 프레임이 아니다"
    // 이며 격리 fixture가 직접 지은 스냅샷이 여기 해당한다. 그 구분을 지우면
    // fixture가 제품 신원 검사를 조용히 통과시킨다.
    [[nodiscard]] bool IsStamped() const noexcept
    {
        return 0ull != sealHash && 0ull != frameId;
    }

    [[nodiscard]] bool IsFromFrame(std::uint64_t id, std::uint64_t epoch) const noexcept
    {
        return frameId == id && sceneEpoch == epoch;
    }

    bool operator==(const EnhancedMaterialSealIdentity& other) const noexcept
    {
        return sealHash == other.sealHash
            && authoredDigest == other.authoredDigest
            && authoredRevision == other.authoredRevision
            && modelGeneration == other.modelGeneration
            && sceneEpoch == other.sceneEpoch
            && frameId == other.frameId;
    }

    bool operator!=(const EnhancedMaterialSealIdentity& other) const noexcept
    {
        return !(*this == other);
    }
};
