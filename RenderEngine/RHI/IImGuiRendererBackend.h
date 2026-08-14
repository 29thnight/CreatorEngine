#pragma once
#ifndef DYNAMICCPP_EXPORTS

#include <cstdint>
#include <string>

class Texture;

enum class ImGuiRendererBackendKind : uint8_t
{
    DX12,
    Vulkan,
};

// 사용자 텍스처 descriptor의 공통 수명 정책. 등록 호출은 해당 프레임의
// 사용 표식이고, 충분히 오래 다시 호출되지 않은 항목만 은퇴시킨다. 실제
// descriptor 재사용은 그 뒤 GPU 완료점까지 지나야 한다.
struct ImGuiTextureLifetimePolicy
{
    static constexpr uint64_t kRetireAfterFrames = 120;

    static constexpr bool ShouldRetire(uint64_t frameIndex, uint64_t lastUsedFrame)
    {
        return frameIndex >= lastUsedFrame + kRetireAfterFrames;
    }

};

static_assert(!ImGuiTextureLifetimePolicy::ShouldRetire(119, 0));
static_assert(ImGuiTextureLifetimePolicy::ShouldRetire(120, 0));

// ImGui의 GPU 렌더러 백엔드 계약.
//
// Win32 입력과 ImGui 컨텍스트는 ImGuiHost가 소유한다. 이 경계 아래에서는
// 스왑체인, 프레임 제출, 폰트/사용자 텍스처만 백엔드별로 갈린다. 백엔드는
// 부팅 때 하나를 고르고 Shutdown 전까지 바꾸지 않는다.
class IImGuiRendererBackend
{
public:
    virtual ~IImGuiRendererBackend() = default;

    virtual ImGuiRendererBackendKind GetKind() const = 0;
    virtual const char* GetName() const = 0;

    virtual bool Initialize(void* windowHandle, uint32_t width, uint32_t height,
        std::string& outError) = 0;
    virtual bool IsActive() const = 0;

    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual void NewFrame() = 0;
    virtual bool RenderAndPresent(std::string& outError) = 0;
    virtual void RebuildFontAtlas() = 0;

    // 반환 ID는 backend descriptor다. 표시하는 프레임마다 다시 호출해야
    // last-used가 갱신되며, 호출자는 프레임을 넘어 ID를 영구 저장하지 않는다.
    virtual uint64_t RegisterTexture(Texture* texture) = 0;
    virtual uint64_t OpenSharedTexture(void* sharedHandle) = 0;
    virtual void SubmitCpuRgbaFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch) = 0;
    virtual uint64_t GetCpuFrameTextureId(uint64_t key) = 0;
    virtual uint64_t GetFallbackTextureId() const = 0;

    virtual void Shutdown() = 0;
};

#endif
