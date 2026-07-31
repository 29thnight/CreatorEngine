#ifndef DYNAMICCPP_EXPORTS
#include "RHI.h"

#include <cassert>

namespace
{
    // 단일 exe에 정적 링크되므로 평범한 전역이면 된다(DLLAcrossSingleton은
    // Dynamic_CPP 경계를 넘는 것들의 장치인데, RHI는 렌더 계층 밖으로 나가지 않는다).
    std::unique_ptr<RHIDevice> g_device;
}

namespace RHI
{
    void Initialize(std::unique_ptr<RHIDevice> device)
    {
        assert(nullptr == g_device && "RHI 재초기화 — 교체 스위치(3-9) 전에는 일어나면 안 된다");
        g_device = std::move(device);
    }

    void Shutdown()
    {
        g_device.reset();
    }

    bool IsInitialized()
    {
        return nullptr != g_device;
    }

    RHIDevice& Device()
    {
        assert(g_device && "RHI::Initialize 전에 Device()를 불렀다");
        return *g_device;
    }

    RHICommandContext& Immediate()
    {
        return Device().GetImmediateContext();
    }
}

#endif
