#pragma once

#include "EnhancedMaterialSealIdentity.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

// W8 — 한 프레임의 draw가 **무엇으로** 그려졌는지 적는 장부.
//
// 이 장부가 없기 전까지, 세대가 어긋난 draw는 조용히 사라졌다. 낡은 PSO
// 핸들이나 만료된 descriptor 버전을 걸면 인코더가 그냥 return 하고(DX12는
// 무음, Vulkan은 unimplemented 하나로 뭉뚱그린다), GBuffer의 Record 루프는
// 실패마다 continue 하므로 머테리얼 하나가 통째로 빠져도 프레임은 성공으로
// 보고됐다. 증상은 "가끔 검게 나온다"뿐이고 원인을 가리키는 수는 0이었다.
//
// 그래서 판정을 두 축으로 나눠 적는다.
//   ① 이 snapshot이 **이번 프레임** 것인가        → Accept
//   ② 같은 값이 한 프레임 안에서 **같은 배치**로 그려졌는가 → Observe
// 둘 다 위반이면 draw를 그리지 않고 이유를 남긴다. 부분 게시보다 빠진 그림이
// 낫다 — 빠진 것은 세지만 섞인 것은 못 센다.
// Record 단계에서 draw가 빠지는 이유. 기록은 병렬이라 문자열을 남길 수 없어
// 이유별 수만 원자적으로 센다 — 어느 자리에서 빠졌는지는 이 이름이 말한다.
enum class EnhancedDrawDropReason : std::uint32_t
{
    Pipeline,           // PSO 핸들이 무효(세대가 지난 핸들 포함)
    Geometry,           // 메시 바인딩이 없거나 무효
    MaterialConstants,  // b2 업로드 링 실패
    Coordinates,        // UV 좌표 테이블 업로드 실패
    TextureTable,       // 이 배치의 texture view 묶음이 없다
    Bindings,           // descriptor table 생성 실패(버전 만료 포함)
    Instances,          // 인스턴스 블록 할당 실패 — 조각 전체가 빠진다
    Count
};

inline const char* EnhancedDrawDropReasonName(EnhancedDrawDropReason reason) noexcept
{
    switch (reason)
    {
    case EnhancedDrawDropReason::Pipeline:          return "pipeline";
    case EnhancedDrawDropReason::Geometry:          return "geometry";
    case EnhancedDrawDropReason::MaterialConstants: return "materialConstants";
    case EnhancedDrawDropReason::Coordinates:       return "coordinates";
    case EnhancedDrawDropReason::TextureTable:      return "textureTable";
    case EnhancedDrawDropReason::Bindings:          return "bindings";
    case EnhancedDrawDropReason::Instances:         return "instances";
    default:                                        return "unknown";
    }
}

struct EnhancedDrawDropCounters
{
    static constexpr std::size_t kCount =
        static_cast<std::size_t>(EnhancedDrawDropReason::Count);

    std::array<std::atomic<std::uint32_t>, kCount> counts{};

    void Clear() noexcept
    {
        for (auto& count : counts) count.store(0, std::memory_order_relaxed);
    }

    void Note(EnhancedDrawDropReason reason, std::uint32_t amount = 1) noexcept
    {
        counts[static_cast<std::size_t>(reason)]
            .fetch_add(amount, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t Get(EnhancedDrawDropReason reason) const noexcept
    {
        return counts[static_cast<std::size_t>(reason)]
            .load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t Total() const noexcept
    {
        std::uint32_t total = 0;
        for (const auto& count : counts) total += count.load(std::memory_order_relaxed);
        return total;
    }

    [[nodiscard]] std::string Summary() const
    {
        std::string text;
        for (std::size_t i = 0; i < kCount; ++i)
        {
            const std::uint32_t value =
                counts[i].load(std::memory_order_relaxed);
            if (0 == value) continue;
            if (!text.empty()) text += " · ";
            text += EnhancedDrawDropReasonName(
                static_cast<EnhancedDrawDropReason>(i));
            text += " " + std::to_string(value);
        }
        return text;
    }
};

struct EnhancedDrawSealLedger
{
    struct Binding
    {
        std::uint64_t textureDigest{};
        std::uint64_t samplerIdentity{};
        std::uint32_t pipelineId{};

        /// W0 — 이 바인딩이 어느 transient descriptor 버전에서 잘려 나왔는가.
        /// `RHIDescriptorVersionHandle::ToToken()` 값이고 0 은 "버전 없음"이다.
        ///
        /// ★ 이 칸은 오래 **writer 가 0 이었다** — 선언만 있고 아무도 채우지
        ///   않았으며 방출되지도 않았다. 값을 꺼낼 어휘가 DX12 에만 있었기
        ///   때문이다(`IRenderDeviceServices::GetDescriptorVersionToken` 이 그것을
        ///   중립으로 올렸다). 폭도 uint32 였는데 토큰은 generation<<32|slot 이라
        ///   윗 32비트가 통째로 잘려 나갔을 자리다.
        ///
        /// ★ **아직 신원에 넣지 않는다.** 아래 `operator==` 는 이 값을 보지
        ///   않는다 — 한 프레임에 기록이 둘 이상이면 같은 밀봉이 서로 다른
        ///   버전에서 잘릴 수 있고, 그때 신원으로 쓰면 거짓 충돌이 된다.
        ///   먼저 캡처로 실제 분포를 재고, 프레임당 하나임이 확인되면 그때
        ///   신원으로 올린다. 재기 전에 판정하지 않는다.
        std::uint64_t descriptorVersion{};

        bool operator==(const Binding& other) const noexcept
        {
            return textureDigest == other.textureDigest
                && samplerIdentity == other.samplerIdentity
                && pipelineId == other.pipelineId;
        }
    };

    struct Counters
    {
        std::uint32_t stamped{};        // 제품 sealing이 도장을 찍은 draw
        std::uint32_t unstamped{};      // 격리 fixture가 직접 지은 snapshot
        std::uint32_t staleFrame{};     // 지난 프레임의 밀봉이 섞여 들어왔다
        std::uint32_t valueMismatch{};  // 밀봉 뒤 값이 바뀌었다(불변식 위반)
        std::uint32_t pipelineConflict{};
        std::uint32_t bindingConflict{};
        std::uint32_t skipped{};        // 위 이유로 그리지 않은 draw

        void Clear() noexcept { *this = Counters{}; }

        [[nodiscard]] std::uint32_t Violations() const noexcept
        {
            return staleFrame + valueMismatch + pipelineConflict + bindingConflict;
        }
    };

    void Begin(std::uint64_t id, std::uint64_t epoch)
    {
        frameId = id;
        sceneEpoch = epoch;
        bindings.clear();
        counters.Clear();
        recordDrops.Clear();
        lastReason.clear();
    }

    // 세대 위반과 기록 단계 누락을 합친 수. 게이트는 이 하나를 본다.
    [[nodiscard]] std::uint32_t Violations() const noexcept
    {
        return counters.Violations() + recordDrops.Total();
    }

    // ① 프레임 신원 대조. computedHash는 호출자가 지금 값에서 다시 계산한
    // digest다 — 밀봉할 때 적은 수와 다르면 그 사이에 값이 바뀐 것이다.
    bool Accept(const EnhancedMaterialSealIdentity& seal, std::uint64_t computedHash)
    {
        if (!seal.IsStamped())
        {
            // 제품 프레임이 아닌 출처(격리 fixture)는 staleness를 재지 않는다.
            // 재려면 잴 것이 있어야 한다.
            ++counters.unstamped;
            return true;
        }
        ++counters.stamped;
        if (seal.sealHash != computedHash)
        {
            ++counters.valueMismatch;
            ++counters.skipped;
            lastReason = "seal value mismatch (밀봉 뒤 값이 바뀌었다)";
            return false;
        }
        if (0ull != frameId && !seal.IsFromFrame(frameId, sceneEpoch))
        {
            ++counters.staleFrame;
            ++counters.skipped;
            lastReason = "seal frame mismatch (지난 프레임의 밀봉이다)";
            return false;
        }
        return true;
    }

    // ② 같은 값 신원이 한 프레임 안에서 두 배치로 갈리지 않았는지.
    bool Observe(std::uint64_t sealHash, const Binding& binding)
    {
        if (0ull == sealHash) return true;
        const auto found = bindings.find(sealHash);
        if (found == bindings.end())
        {
            bindings.emplace(sealHash, binding);
            return true;
        }
        if (found->second.pipelineId != binding.pipelineId)
        {
            ++counters.pipelineConflict;
            ++counters.skipped;
            lastReason = "같은 seal이 서로 다른 PSO로 그려졌다";
            return false;
        }
        if (!(found->second == binding))
        {
            ++counters.bindingConflict;
            ++counters.skipped;
            lastReason = "같은 seal이 서로 다른 texture/sampler 배치로 그려졌다";
            return false;
        }
        return true;
    }

    // Record 단계의 누락. 병렬 기록에서 불리므로 원자 계수만 한다.
    void NoteDrop(EnhancedDrawDropReason reason, std::uint32_t amount = 1) noexcept
    {
        recordDrops.Note(reason, amount);
    }

    [[nodiscard]] std::string Summary() const
    {
        std::string text = "seal stamped " + std::to_string(counters.stamped)
            + " · unstamped " + std::to_string(counters.unstamped)
            + " · stale " + std::to_string(counters.staleFrame)
            + " · value " + std::to_string(counters.valueMismatch)
            + " · pso " + std::to_string(counters.pipelineConflict)
            + " · binding " + std::to_string(counters.bindingConflict)
            + " · skipped " + std::to_string(counters.skipped)
            + " · dropped " + std::to_string(recordDrops.Total());
        const std::string drops = recordDrops.Summary();
        if (!drops.empty()) text += " (" + drops + ")";
        if (!lastReason.empty()) text += " · last " + lastReason;
        return text;
    }

    std::uint64_t frameId{};
    std::uint64_t sceneEpoch{};
    Counters counters{};
    EnhancedDrawDropCounters recordDrops{};
    std::string lastReason{};
    std::unordered_map<std::uint64_t, Binding> bindings{};
};
