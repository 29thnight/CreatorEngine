#pragma once
#include "RHIDevice.h"
#include "RHICommandContext.h"
#include <memory>

// RHI 전역 접근점(PHASE 3-1).
//
// 전역인 이유: 현재 렌더 코드 전체가 전역 DeviceStates를 전제하고 있어, 첫
// 이관 단계에서 인자 배관을 동시에 깔 수 없다. 이 전역은 DeviceStates를
// "타입이 있는 경계" 하나로 좁히는 중간 계단이고, 렌더그래프(3-5)가 패스에
// 컨텍스트를 인자로 넘기기 시작하면 Immediate() 호출부터 말라 죽는다.
namespace RHI
{
    // SceneRenderer 초기화에서 백엔드가 자신을 등록한다. 재등록은 교체 스위치
    // (3-9) 전까지는 일어나지 않는다.
    void Initialize(std::unique_ptr<RHIDevice> device);
    void Shutdown();

    bool IsInitialized();
    RHIDevice& Device();
    RHICommandContext& Immediate();
}
