#include "IImGuiHost.h"
#include "ImGuiWin32Cursor.h"
#include "DX12/ImGuiDx12Shell.h"
#include "Vulkan/ImGuiVulkanShell.h"
#include "GlobalImGuiContext.h"
#include "LogSystem.h"
#include "RuntimeSettings.h"
#include "../../../Engine/Utility_Framework/PathFinder.h"
#include "../../../Engine/Utility_Framework/WarmupLedger.h"

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
            ImGuiWin32Cursor::Reset(hwnd);

            // `IMGUI_CHECKVERSION()` 은 **출하 구성에서 힘이 0 이다.** 그 안은
            // `IM_ASSERT` 로만 말하는데 그것이 `assert()` 이고 Release 는 NDEBUG 라
            // 통째로 사라진다. 게다가 매크로는 bool 을 돌려주므로 값을 버리면
            // 거짓을 받아도 아무도 보지 않는다([[assert-as-policy-has-no-force]]).
            //
            // 이 검사가 막는 것은 추상적인 사고가 아니다 — 이 기계에는 imgui
            // 설치본이 둘이고 판이 서로 다르다. 헤더를 A 에서, 라이브러리를 B
            // 에서 가져오면 `ImGuiIO` 의 배치가 갈린 채로 컴파일이 통과하고
            // 기동에서야 엉뚱하게 죽는다([[two-vcpkg-installs-wrong-header]]).
            if (!IMGUI_CHECKVERSION())
            {
                throw std::runtime_error(
                    "ImGui header/library mismatch: compiled against " IMGUI_VERSION
                    ", linked library reports a different version or data layout");
            }
            GlobalImGuiContext::GetInstance()->SetContext(ImGui::CreateContext());
            ImGuiIO& io = ImGui::GetIO();
            ImGui::GetAllocatorFunctions(
                &GlobalImGuiContext::GetInstance()->p_alloc_func,
                &GlobalImGuiContext::GetInstance()->p_free_func,
                &GlobalImGuiContext::GetInstance()->p_user_data);

            // 커서 모양은 ImGui가 결정하지만 SetCursor는 HWND 소유 스레드에서
            // 적용한다. Win32 backend의 PresentationThread 직접 호출을 막는다.
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            io.ConfigDpiScaleFonts = true;
            static const std::string kIniPath =
                PathFinder::ConfigPath("imgui.ini").string();
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
                Debug::PrintLog(spdlog::level::err, std::string("[ImGui] ") + m_renderer->GetName() +
                    " 백엔드 초기화 실패: " + outError);
                return false;
            }

            std::printf("[ImGui] 렌더러 백엔드: %s\n", m_renderer->GetName());
            Debug::PrintLog(spdlog::level::debug, std::string("[ImGui] 렌더러 백엔드: ") +
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

        float GetWindowDpiScale() const override
        {
            const UINT dpi = ::GetDpiForWindow(static_cast<HWND>(m_windowHandle));
            return dpi ? static_cast<float>(dpi) / 96.f : 1.f;
        }

        bool IsPerMonitorDpiAware() const override
        {
            return ::AreDpiAwarenessContextsEqual(
                ::GetWindowDpiAwarenessContext(static_cast<HWND>(m_windowHandle)),
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
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

            // Win32 backend가 클라이언트 픽셀 크기와 framebuffer 배율을 소유한다.
            // PMv2의 client/mouse/render 좌표는 물리 픽셀이므로 DPI를 framebuffer
            // 배율로 다시 곱하지 않는다. DPI는 FontScaleDpi와 UI geometry에 적용한다.

            m_renderer->Resize(static_cast<uint32_t>((std::max)(0.0f, newSize.x)),
                static_cast<uint32_t>((std::max)(0.0f, newSize.y)));
            m_renderer->NewFrame();

            ImGui_ImplWin32_NewFrame();
            // `WantCapture*`/`WantTextInput` 을 여기서 true 로 덮어쓰던 줄이 있었다
            // (PHASE 21 W5 선행 2 에서 걷음). 그 셋은 ImGui 가 **출력**하는 값이고
            // 바로 아래 `ImGui::NewFrame` 이 무조건 다시 계산하므로 대입은 죽은
            // 줄이었다 — 걷기 전후로 게시본(`editor.viewport`)이 같은 값을 냈다
            // (mouse=false · keyboard=true(NavActive) · text=false). 계획서 §1.8 은
            // 이 줄이 입력 소유 신호를 지운다고 봤는데, 지운 것이 아니라 아무
            // 일도 하지 않고 있었다. 신호를 읽는 자리는 ViewportHost 의 게시본이다.
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
            // 예열 장부: UI 가 한 프레임을 온전히 끝낸 때. 창이 보이는 것과 다른
            // 사건이다 — 그 사이에 첫 프레임의 셰이더·파이프라인 비용이 있다.
            engine::warmup::mark(engine::warmup::stage::first_ui_frame);
            ImGuiWin32Cursor::PublishFrameCursor(static_cast<HWND>(m_windowHandle));

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

        bool IsTextureReady(Texture* texture) const override
        {
            return m_renderer && m_renderer->IsTextureReady(texture);
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
            ImGuiWin32Cursor::Reset(static_cast<HWND>(m_windowHandle));
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
