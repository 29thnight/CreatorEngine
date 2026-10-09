#pragma once

#include <cstdint>
#include "RHIDisplayFrame.h"

// 라이브 표시 결과를 presentation 계층에 여는 백엔드 중립 계약.
// Core는 표시 텍스처와 생산 완료를 소유하며, Host는 실제 표시 방법을 선택한다.
// Editor는 DX12 ImGui 셸, Player는 ImGui 없는 DX12/Vulkan native presenter다.
// texture ID는 Host 안에서만 의미가 있는 불투명 값이며 UI 타입이 아니다.
// 이 경계는 SDK present/pacing을 규정하지 않는다. 최종 소비 완료까지 lease를
// 보유하는 의무는 native present 소유자를 교체할 때도 유지된다.
struct IDisplayPresentationSink
{
    virtual ~IDisplayPresentationSink() = default;

    /// presentation 백엔드가 살아 있는가. 죽어 있으면 표시 ID는 0이다.
    virtual bool IsActive() const = 0;

    /// 진단 문자열용 백엔드 이름. 살아 있지 않으면 "none" 류를 돌려준다.
    virtual const char* GetName() const = 0;

    /// 공유 핸들 텍스처(DX12 표시 슬롯)를 열어 presentation 텍스처 ID를
    /// 돌려준다. 호출자는 표시 수명 락 아래에서 부른다 — 핸들 retire와
    /// 직렬화되는 것은 Core의 몫이다. 성공한 구현은 consumerLease를 이번
    /// 프레임의 GPU 사용이 실제 완료될 때까지 보유한다. 캐시만으로는 부족하다.
    virtual uint64_t OpenSharedTexture(void* sharedHandle,
        std::shared_ptr<RHIDisplayConsumerLease> consumerLease) = 0;

    /// 리드백 프레임(CPU RGBA — Vulkan 표시 브리지)을 키로 게시한다.
    /// RenderThread에서 불린다 — 구현은 그 스레드에서 안전해야 한다
    /// (기존 ImGui 셸의 SubmitCpuRgbaFrame과 같은 계약).
    virtual void SubmitCpuFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch, const RHIDisplayFrameMetadata& frame) = 0;

    /// 이번 Host 프레임의 합성 전에 업로드 기록을 마친 픽셀과 신원.
    /// 합성 시작 뒤 도착한 Submit은 다음 Host 프레임까지 이 결과를 바꾸지 않는다.
    virtual RHIDisplayTexture GetCpuFrameTexture(uint64_t key) = 0;

    // 완료 표시 프레임을 게시한 뒤 수명 뮤텍스 밖에서 비차단 깨우기만 한다.
    // 생산자 스레드에서 렌더링하거나 UI에 접근하지 않는다.
    virtual void NotifyDisplayAvailable() {}
};
