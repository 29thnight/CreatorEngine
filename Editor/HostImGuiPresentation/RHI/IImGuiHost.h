#pragma once
#if defined(CE_PLAYER)
#error "Editor ImGui presentation is unavailable to Player; use native RHI presentation."
#endif

#include "RHI/RHIDisplayFrame.h"
#include <functional>
#include <memory>
#include <cstdint>
#include <string>

class Texture;

// Editor 전용 ImGui 표시 호스트. Win32 입력과 ImGui 컨텍스트를 소유하고,
// GPU 표시는 concrete ImGuiDx12Shell만 사용한다. Scene/Game/material preview도
// Editor 부팅 정책으로 DX12에 고정된다. Player는 이 계약을 소비하지 않고
// native RHI 스왑체인으로 표시한다.
class IImGuiHost
{
public:
    virtual ~IImGuiHost() = default;

    /// 컨텍스트·플랫폼 백엔드·렌더 백엔드 가동. windowHandle은 Win32 HWND다
    /// — void*인 이유는 이 헤더가 플랫폼·그래픽 헤더를 끌지 않기 위해서다
    /// (IRHIDeviceResources::AttachSwapChain과 같은 규약).
    ///
    /// 실패 뒤 프레임 호출은 안전하지만 호출자는 부팅을 중단해야 한다. 명시한
    /// backend 대신 다른 renderer를 만드는 fallback은 이 계약에 없다.
    virtual bool Initialize(void* windowHandle, std::string& outError) = 0;
    virtual bool IsActive() const = 0;
    virtual const char* GetBackendName() const = 0;

    /// OS 창의 contents scale. UI 배율을 NewFrame의 폰트 계산 전에 적용한다.
    virtual float GetWindowDpiScale() const = 0;
    virtual bool IsPerMonitorDpiAware() const = 0;

    /// 창 크기 추적 → 백엔드 리사이즈 → 백엔드/플랫폼 NewFrame → ImGui::NewFrame.
    virtual void BeginFrame() = 0;

    /// ImGui::Render → 백버퍼 드로우 → Present → 멀티 뷰포트 플랫폼 창.
    /// 네이티브 기록이 리소스 수명을 확보한 뒤 onRecorded로 장면 소유권을
    /// 놓을 수 있다. 제출 대기와 Present보다 먼저 호출한다.
    virtual void EndFrame(std::function<void()> onRecorded = {}) = 0;

    /// 폰트 아틀라스 재빌드. 소비자가 io.Fonts를 바꾼 뒤 부른다 — 백엔드의
    /// 디바이스 오브젝트 재생성이 필요해서 계약에 있다(폰트 텍스처는 백엔드
    /// 소유물이다).
    virtual void RebuildFontAtlas() = 0;

    /// ImGui::Image가 소비할 DX12 텍스처 ID. 상위 에디터 코드는 descriptor의
    /// 저장 방식에 의존하지 않는다.
    /// ID를 영구 캐시하지 말고 실제 표시 프레임마다 호출해 수명 표식을 남긴다.
    virtual uint64_t RegisterTexture(Texture* texture) = 0;

    /// 그 텍스처의 픽셀이 실제로 GPU에 올라가 있는가(PHASE 21 W7 비동기 썸네일).
    /// RegisterTexture의 반환값은 이것을 말하지 않는다 — 업로드 전에도 0이 아닌
    /// ID가 나온다. 부수 효과가 없으며, 올리는 일은 표시 프레임의
    /// RegisterTexture가 한다. 프레임 밖 등록은 null SRV 슬롯만 예약한다.
    virtual bool IsTextureReady(Texture* texture) const = 0;

    virtual uint64_t OpenSharedTexture(void* sharedHandle,
        std::shared_ptr<RHIDisplayConsumerLease> consumerLease) = 0;
    virtual void SubmitCpuRgbaFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch, const RHIDisplayFrameMetadata& frame) = 0;
    virtual RHIDisplayTexture GetCpuFrameTexture(uint64_t key) = 0;
    virtual uint64_t GetFallbackTextureId() const = 0;

    /// 최종 정리. 렌더 스레드가 멈춘 뒤에만 부른다.
    virtual void Shutdown() = 0;
};

/// DX12 렌더러 셸을 감싼 Editor 전용 Win32 호스트.
IImGuiHost& GetImGuiHost();
