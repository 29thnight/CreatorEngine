#pragma once

#include "../IImGuiRendererBackend.h"

// ImGui renderer backend의 Vulkan 구현. 전용 VulkanDeviceResources를 소유해
// DX12 셸과 같은 방식으로 에디터 HWND의 스왑체인과 프레임 펜스를 격리한다.
class ImGuiVulkanShell final : public IImGuiRendererBackend
{
public:
    ImGuiVulkanShell();
    ~ImGuiVulkanShell() override;

    ImGuiRendererBackendKind GetKind() const override
    {
        return ImGuiRendererBackendKind::Vulkan;
    }
    const char* GetName() const override { return "Vulkan"; }

    bool Initialize(void* windowHandle, uint32_t width, uint32_t height,
        std::string& outError) override;
    bool IsActive() const override;

    void Resize(uint32_t width, uint32_t height) override;
    void NewFrame() override;
    bool RenderAndPresent(std::string& outError) override;
    void RebuildFontAtlas() override;

    uint64_t RegisterTexture(Texture* texture) override;
    uint64_t OpenSharedTexture(void* sharedHandle) override;
    void SubmitCpuRgbaFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch) override;
    uint64_t GetCpuFrameTextureId(uint64_t key) override;
    uint64_t GetFallbackTextureId() const override;

    void Shutdown() override;

private:
    ImGuiVulkanShell(const ImGuiVulkanShell&) = delete;
    ImGuiVulkanShell& operator=(const ImGuiVulkanShell&) = delete;

    struct Impl;
    Impl* m_impl;
};
