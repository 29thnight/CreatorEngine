#pragma once
#include <cstdint>
#include <string>

class RHIEncoder;

/// 한 GPU 제출을 가리키는 표(§3.3).
///
/// ★ 엔진 프레임과 제출은 1:1 이 아니다. 씬뷰와 게임뷰가 함께 있으면 한
///   `engineFrameId` 에 제출이 둘 생긴다. 그래서 둘을 가른다 — 프레임 그래프와
///   CPU Hierarchy 는 `engineFrameId` 를 기준으로 서고, GPU 구간은 `submissionId` 를
///   기준으로 선다.
///
/// ★ `ringSlot` 을 **수집 키**로 쓴다. 이것이 없었을 때 수집은 "지금 기록 중인
///   슬롯" 을 읽었고, 실측에서 수집의 83% 가 남의 제출을 읽고 있었다(§0.5.10).
struct GpuFrameToken
{
    static constexpr uint32_t kInvalidRingSlot = 0xFFFFFFFFu;

    uint64_t engineFrameId{ 0 };
    uint64_t submissionId{ 0 };
    uint64_t fenceValue{ 0 };
    uint64_t renderViewId{ 0 };
    uint32_t ringSlot{ kInvalidRingSlot };
    uint8_t  queueId{ 0 };

    bool IsValid() const { return kInvalidRingSlot != ringSlot; }
};

/// RenderGraph가 아는 GPU pass timing의 최소 계약(G-3).
/// query heap/pool, timestamp 주파수와 readback은 backend 구현에 남는다.
class IRHIGpuProfiler
{
public:
    static constexpr uint32_t kInvalidSlot = 0xFFFFFFFFu;

    virtual ~IRHIGpuProfiler() = default;

    virtual uint32_t BeginPass(RHIEncoder& encoder, const std::string& name) = 0;
    virtual void EndPass(RHIEncoder& encoder, uint32_t slot) = 0;
};

