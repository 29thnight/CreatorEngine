#pragma once

#include "IImGuiHost.h"
#include "RHI/IDisplayPresentationSink.h"

// IImGuiHost를 Core의 표시 sink 계약 뒤로 위임하는 공용 어댑터(E4-6c).
// E4-6a 때 Editor/Player 각자에 있던 ~20줄 중복이 여기로 합쳐졌다 —
// 두 Host 모두 이 타입을 SetDisplayPresentationSink로 설치한다.
// 셸이 아직 Initialize 전이어도 위임은 안전하다(비활성 셸은 no-op/0).
struct ImGuiHostPresentationSink final : IDisplayPresentationSink
{
    bool IsActive() const override { return GetImGuiHost().IsActive(); }
    const char* GetName() const override
    {
        return GetImGuiHost().GetBackendName();
    }
    uint64_t OpenSharedTexture(void* sharedHandle) override
    {
        return GetImGuiHost().OpenSharedTexture(sharedHandle);
    }
    void SubmitCpuFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch) override
    {
        GetImGuiHost().SubmitCpuRgbaFrame(key, width, height, rgba, rowPitch);
    }
    uint64_t GetCpuFrameTextureId(uint64_t key) override
    {
        return GetImGuiHost().GetCpuFrameTextureId(key);
    }
};
