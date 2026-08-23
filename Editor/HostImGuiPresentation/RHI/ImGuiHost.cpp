#include "IImGuiHost.h"
#include "DX12/ImGuiDx12Shell.h"
#include "Vulkan/ImGuiVulkanShell.h"
#include "GlobalImGuiContext.h"
#include "LogSystem.h"
#include "RuntimeSettings.h"
#include "../../../Engine/Utility_Framework/PathFinder.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <memory>

namespace
{
    class ImGuiHost final : public IImGuiHost
    {
    public:
        bool Initialize(void* windowHandle, std::string& outError) override
        {
            if (nullptr != m_windowHandle) return IsActive();
            m_windowHandle = windowHandle;
            HWND hwnd = static_cast<HWND>(windowHandle);

            IMGUI_CHECKVERSION();
            GlobalImGuiContext::GetInstance()->SetContext(ImGui::CreateContext());
            ImGuiIO& io = ImGui::GetIO();
            ImGui::GetAllocatorFunctions(
                &GlobalImGuiContext::GetInstance()->p_alloc_func,
                &GlobalImGuiContext::GetInstance()->p_free_func,
                &GlobalImGuiContext::GetInstance()->p_user_data);

            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            static const std::string kIniPath =
                PathFinder::RuntimeDataPath("imgui.ini").string();
            io.IniFilename = kIniPath.c_str();

            if (!ImGui_ImplWin32_Init(hwnd))
            {
                outError = "ImGui Win32 플랫폼 백엔드 초기화 실패";
                return false;
            }

            // Scene renderer와 같은 active backend를 소비한다. 선택 값에는 setter가
            // 없고, 초기화 실패 시 다른 backend를 만들지 않는다(Slice 8-c).
            if (RenderBackend::DX12 ==
                RuntimeSettings::Get().GetRenderBackend())
                m_renderer = std::make_unique<ImGuiDx12Shell>();
            else
                m_renderer = std::make_unique<ImGuiVulkanShell>();

            RECT clientRect{};
            GetClientRect(hwnd, &clientRect);
            const uint32_t width = static_cast<uint32_t>(
                (clientRect.right > clientRect.left) ? clientRect.right - clientRect.left : 1);
            const uint32_t height = static_cast<uint32_t>(
                (clientRect.bottom > clientRect.top) ? clientRect.bottom - clientRect.top : 1);

            if (!m_renderer->Initialize(hwnd, width, height, outError))
            {
                std::printf("[ImGui] %s 백엔드 초기화 실패 - UI가 표시되지 않는다: %s\n",
                    m_renderer->GetName(), outError.c_str());
                Debug->LogError(std::string("[ImGui] ") + m_renderer->GetName() +
                    " 백엔드 초기화 실패: " + outError);
                return false;
            }

            std::printf("[ImGui] 렌더러 백엔드: %s\n", m_renderer->GetName());
            Debug->LogDebug(std::string("[ImGui] 렌더러 백엔드: ") +
                m_renderer->GetName());
            return true;
        }

        bool IsActive() const override
        {
            return m_renderer && m_renderer->IsActive();
        }

        ImGuiRendererBackendKind GetBackendKind() const override
        {
            return m_renderer ? m_renderer->GetKind() : ImGuiRendererBackendKind::DX12;
        }

        const char* GetBackendName() const override
        {
            return m_renderer ? m_renderer->GetName() : "none";
        }

        void BeginFrame() override
        {
            if (!m_renderer) return;

            ImGuiIO& io = ImGui::GetIO();
            HWND hwnd = static_cast<HWND>(m_windowHandle);
            RECT rect{};
            GetClientRect(hwnd, &rect);
            const ImVec2 newSize(static_cast<float>(rect.right - rect.left),
                static_cast<float>(rect.bottom - rect.top));

            if (io.DisplaySize != newSize && newSize != ImVec2(0, 0) &&
                io.DisplaySize != ImVec2(0, 0))
            {
                io.DisplaySize = newSize;
                io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
            }

            m_renderer->Resize(static_cast<uint32_t>((std::max)(0.0f, newSize.x)),
                static_cast<uint32_t>((std::max)(0.0f, newSize.y)));
            m_renderer->NewFrame();

            ImGui_ImplWin32_NewFrame();
            io.WantCaptureKeyboard = io.WantCaptureMouse = io.WantTextInput = true;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports |
                ImGuiBackendFlags_HasMouseCursors;
            ImGui::NewFrame();
        }

        void EndFrame() override
        {
            if (!m_renderer) return;
            ImGui::Render();

            std::string presentError;
            if (!m_renderer->RenderAndPresent(presentError))
            {
                std::printf("[ImGui] %s 렌더 실패: %s\n",
                    m_renderer->GetName(), presentError.c_str());
            }

            ImGuiIO& io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }

        void RebuildFontAtlas() override
        {
            if (m_renderer) m_renderer->RebuildFontAtlas();
        }

        uint64_t RegisterTexture(Texture* texture) override
        {
            return m_renderer ? m_renderer->RegisterTexture(texture) : 0;
        }

        uint64_t OpenSharedTexture(void* sharedHandle) override
        {
            return m_renderer ? m_renderer->OpenSharedTexture(sharedHandle) : 0;
        }

        void SubmitCpuRgbaFrame(uint64_t key, uint32_t width, uint32_t height,
            const void* rgba, uint32_t rowPitch) override
        {
            if (m_renderer)
                m_renderer->SubmitCpuRgbaFrame(key, width, height, rgba, rowPitch);
        }

        uint64_t GetCpuFrameTextureId(uint64_t key) override
        {
            return m_renderer ? m_renderer->GetCpuFrameTextureId(key) : 0;
        }

        uint64_t GetFallbackTextureId() const override
        {
            return m_renderer ? m_renderer->GetFallbackTextureId() : 0;
        }

        void Shutdown() override
        {
            if (nullptr == m_windowHandle) return;
            if (m_renderer) m_renderer->Shutdown();
            m_renderer.reset();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            m_windowHandle = nullptr;
        }

    private:
        void* m_windowHandle{ nullptr };
        std::unique_ptr<IImGuiRendererBackend> m_renderer;
    };
}

IImGuiHost& GetImGuiHost()
{
    static ImGuiHost host;
    return host;
}
