#pragma once

#if defined(CE_PLAYER)
#error "Editor ImGui presentation is unavailable to Player; use native RHI presentation."
#endif

#include "IImGuiHost.h"
#include "RHI/IDisplayPresentationSink.h"
#include <functional>
#include <utility>

// Editor의 IImGuiHost를 Core의 표시 sink 계약 뒤로 위임한다.
// Player는 native RHI 표시 경로를 사용하며 이 어댑터를 설치하지 않는다.
// 셸이 아직 Initialize 전이어도 위임은 안전하다(비활성 셸은 no-op/0).
struct ImGuiHostPresentationSink final : IDisplayPresentationSink
{
    explicit ImGuiHostPresentationSink(std::function<void()> onDisplayAvailable = {})
        : m_onDisplayAvailable(std::move(onDisplayAvailable))
    {
    }

    bool IsActive() const override { return GetImGuiHost().IsActive(); }
    const char* GetName() const override
    {
        return GetImGuiHost().GetBackendName();
    }
    uint64_t OpenSharedTexture(void* sharedHandle,
        std::shared_ptr<RHIDisplayConsumerLease> consumerLease) override
    {
        const uint64_t textureId = GetImGuiHost().OpenSharedTexture(
            sharedHandle, std::move(consumerLease));
        // 폴백 검정 텍스처는 완료 장면의 픽셀이 아니므로 준비된 bundle로 게시하지 않는다.
        return textureId == GetImGuiHost().GetFallbackTextureId() ? 0 : textureId;
    }
    void SubmitCpuFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch, const RHIDisplayFrameMetadata& frame) override
    {
        GetImGuiHost().SubmitCpuRgbaFrame(key, width, height, rgba, rowPitch, frame);
    }
    RHIDisplayTexture GetCpuFrameTexture(uint64_t key) override
    {
        return GetImGuiHost().GetCpuFrameTexture(key);
    }
    void NotifyDisplayAvailable() override
    {
        if (m_onDisplayAvailable)
        {
            m_onDisplayAvailable();
        }
    }

private:
    std::function<void()> m_onDisplayAvailable;
};
