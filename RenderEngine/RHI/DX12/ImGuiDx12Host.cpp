#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiDx12Host.h"
#include "ImGuiDx12Shell.h"
#include "GlobalImGuiContext.h"
#include "EngineSetting.h"
#include "LogSystem.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <Windows.h>
#include <cstdio>

IImGuiHost& GetImGuiHost()
{
    // 소멸을 정적에 맡기지 않는다 — 정리는 Shutdown이 소유한다(셸과 같은 규약).
    static ImGuiDx12Host host;
    return host;
}

bool ImGuiDx12Host::Initialize(void* windowHandle, std::string& outError)
{
    m_windowHandle = windowHandle;
    HWND hwnd = static_cast<HWND>(windowHandle);

    IMGUI_CHECKVERSION();
    GlobalImGuiContext::GetInstance()->SetContext(ImGui::CreateContext());
    ImGuiIO& io = ImGui::GetIO();

    // Dynamic_CPP(DLL) 쪽이 같은 힙에서 ImGui 메모리를 다루도록 할당자를 공유.
    ImGui::GetAllocatorFunctions(
        &GlobalImGuiContext::GetInstance()->p_alloc_func,
        &GlobalImGuiContext::GetInstance()->p_free_func,
        &GlobalImGuiContext::GetInstance()->p_user_data);

    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // 폰트는 여기서 만들지 않는다 — 폰트는 내용물이라 소비자의 몫이다.
    // EditorRenderer가 Verdana+아이콘을 얹고, Player는 아무것도 안 얹어
    // ImGui 기본 폰트로 돈다(글자를 그리지 않으므로 상관없다).

    ImGui_ImplWin32_Init(hwnd);

    // 셸 가동 — 자체 DX12DeviceResources(디바이스·스왑체인·펜스)가 여기서 선다.
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    const uint32_t clientWidth =
        static_cast<uint32_t>(clientRect.right - clientRect.left);
    const uint32_t clientHeight =
        static_cast<uint32_t>(clientRect.bottom - clientRect.top);

    if (!ImGuiDx12Shell::Get().Initialize(hwnd, clientWidth, clientHeight, outError))
    {
        // 실패를 조용히 넘기지 않는다. 창이 검은 것과 초기화가 실패한 것이
        // 로그에서 구분되어야 한다. (DX11 폴백은 D4에서 걷혔다 — 셸이 없으면
        // 그림이 없다.)
        std::printf("[ImGui] DX12 셸 초기화 실패 - UI가 표시되지 않는다: %s\n",
            outError.c_str());
        Debug->LogError("[ImGui] DX12 셸 초기화 실패: " + outError);
        return false;
    }
    return true;
}

bool ImGuiDx12Host::IsActive() const
{
    return ImGuiDx12Shell::Get().IsActive();
}

void ImGuiDx12Host::BeginFrame()
{
    ImGuiIO& io = ImGui::GetIO();
    HWND hwnd = static_cast<HWND>(m_windowHandle);

    RECT rect{};
    GetClientRect(hwnd, &rect);
    const ImVec2 newSize(static_cast<float>(rect.right - rect.left),
        static_cast<float>(rect.bottom - rect.top));

    EngineSettingInstance->SetWindowSize({ newSize.x, newSize.y });

    if (io.DisplaySize != newSize
        && newSize != ImVec2(0, 0)
        && io.DisplaySize != ImVec2(0, 0))
    {
        io.DisplaySize = newSize;
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }

    ImGuiDx12Shell::Get().Resize(static_cast<uint32_t>(newSize.x),
        static_cast<uint32_t>(newSize.y));
    ImGuiDx12Shell::Get().NewFrame();

    ImGui_ImplWin32_NewFrame();
    io.WantCaptureKeyboard = io.WantCaptureMouse = io.WantTextInput = true;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports
        | ImGuiBackendFlags_HasMouseCursors;

    ImGui::NewFrame();
}

void ImGuiDx12Host::EndFrame()
{
    ImGui::Render();

    std::string presentError;
    if (!ImGuiDx12Shell::Get().RenderAndPresent(presentError))
    {
        std::printf("[ImGui] DX12 셸 렌더 실패: %s\n", presentError.c_str());
    }

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImGuiDx12Host::RebuildFontAtlas()
{
    // 폰트 텍스처는 백엔드 소유물이라 재빌드가 계약에 있다 — 소비자가 io.Fonts를
    // 바꾼 뒤 부른다. 셸이 안 섰으면 디바이스 오브젝트도 없으므로 건너뛴다.
    if (!ImGuiDx12Shell::Get().IsActive())
    {
        return;
    }
    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_CreateDeviceObjects();
}

void ImGuiDx12Host::Shutdown()
{
    if (nullptr == m_windowHandle)
    {
        return; // 이미 정리됐다 — Shutdown은 한 번만 실행된다.
    }
    ImGuiDx12Shell::Get().Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_windowHandle = nullptr;
}

#endif // !DYNAMICCPP_EXPORTS
