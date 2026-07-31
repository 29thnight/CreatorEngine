#pragma once
#include "RHIDefinitions.h"
#include <memory>

class RHICommandContext;

// 디바이스 인터페이스(PHASE 3-1).
//
// 리소스 생성(버퍼·텍스처·PSO)은 아직 여기 없다 — 기존 코드가 DeviceStates의
// 전역 디바이스로 직접 만들고 있고, 그 이관은 리소스 타입별로 패스 이식과 함께
// 진행한다. 지금 이 인터페이스의 역할은 두 가지다: 백엔드 식별(교체 스위치의
// 근거)과 커맨드 컨텍스트 공급(패스가 전역 대신 이것을 받는다).
class RHIDevice
{
public:
    virtual ~RHIDevice() = default;

    virtual RHIBackendKind GetBackend() const = 0;

    // 창 타이틀·로그·교체 스위치 UI에 그대로 노출되는 이름("Dx11" / "Dx12").
    virtual const char* GetName() const = 0;

    virtual RHICommandContext& GetImmediateContext() = 0;
};
