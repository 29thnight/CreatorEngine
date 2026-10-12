#pragma once
#include <cstdint>
#include <memory>
#include <string>

class RHIEncoder;

// 표시 이름은 합쳐질 수 있지만 스케줄 비용은 선언된 패스 한 개에 속한다.
// 서명은 해시가 아닌 정규화한 선언 원문이다. 길이 접두 필드와 전체 비교로
// 이름 구분자/해시 충돌을 피하고, 공유 소유권으로 지연 수집까지 원문을 보존한다.
struct GpuPassTimingIdentity
{
    std::shared_ptr<const std::string> graphSignature;
    uint32_t passIndex{UINT32_MAX};

    bool IsValid() const { return graphSignature && passIndex != UINT32_MAX; }
    bool operator==(const GpuPassTimingIdentity& other) const
    {
        return passIndex == other.passIndex &&
            (graphSignature == other.graphSignature ||
                (graphSignature && other.graphSignature && *graphSignature == *other.graphSignature));
    }
};

/// 한 GPU 제출을 가리키는 표(§3.3).
///
/// Render publication and GPU submission are not 1:1. Scene and Game views can
/// submit separately from one publication. engineFrameId retains that render
/// publication identity; sourceEngineFrame is the distinct CPU-profiler owner.
///
/// ★ `ringSlot` 을 **수집 키**로 쓴다. 이것이 없었을 때 수집은 "지금 기록 중인
///   슬롯" 을 읽었고, 실측에서 수집의 83% 가 남의 제출을 읽고 있었다(§0.5.10).
struct GpuFrameToken
{
    static constexpr uint32_t kInvalidRingSlot = 0xFFFFFFFFu;

    uint64_t engineFrameId{ 0 };

    /// 이 제출을 **연 순간**의 CPU 시각(QPC). EngineDiagnostics 의 profile_tick 과
    /// 같은 축이다 — 둘 다 QueryPerformanceCounter 의 원시 값이다.
    ///
    /// ★ 이것이 GPU 구간을 검산하는 자다. 변환한 GPU 시작은 이 시각보다 뒤여야
    ///   하고, 변환한 GPU 끝은 수집한 시각보다 앞이어야 한다. 그 사이를 벗어나면
    ///   두 시계가 정렬되지 않은 것이고, 그때 통합 축은 그럴듯한 거짓말이 된다.
    uint64_t cpuSubmitTick{ 0 };
    uint64_t submissionId{ 0 };
    uint64_t fenceValue{ 0 };
    uint64_t renderViewId{ 0 };
    uint32_t ringSlot{ kInvalidRingSlot };
    uint8_t  queueId{ 0 };

    // Capture admission belongs to this submission, never the collection frame.
    // Zero means diagnostic timing only; it must not enter a later capture.
    uint64_t captureGeneration{ 0 };

    // Copied from the originating host packet, never sampled at GPU completion.
    // Missing host ownership must not be guessed from engineFrameId.
    uint32_t sourceEngineFrame{ 0 };
    bool sourceEngineFrameAvailable{ false };

    bool IsValid() const { return kInvalidRingSlot != ringSlot; }
};

/// RenderGraph가 아는 GPU pass timing의 최소 계약(G-3).
/// query heap/pool, timestamp 주파수와 readback은 backend 구현에 남는다.
class IRHIGpuProfiler
{
public:
    static constexpr uint32_t kInvalidSlot = 0xFFFFFFFFu;

    virtual ~IRHIGpuProfiler() = default;

    virtual uint32_t BeginPass(RHIEncoder& encoder, const std::string& name,
        const GpuPassTimingIdentity& identity = {}) = 0;
    virtual void EndPass(RHIEncoder& encoder, uint32_t slot) = 0;
};

