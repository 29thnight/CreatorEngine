#pragma once
#include <cstdint>
#include <string>

class RHIEncoder;

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

