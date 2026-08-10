#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiDx12Host.h"
#include "ImGuiDx12Shell.h"
#include "GlobalImGuiContext.h"
#include "EngineSetting.h"
#include "LogSystem.h"
#include "../../../Utility_Framework/PathFinder.h"

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

    // ── 도크 레이아웃 파일을 실행 파일 옆에 못박는다 ──
    //
    // ★ ImGui 기본값 "imgui.ini" 는 **작업 디렉터리 상대**다(imgui.h 가 그렇게
    //   적어 두었다: "important: default is relative to current working dir!").
    //   그래서 어디서 띄우느냐에 따라 레이아웃 파일이 갈렸다 —
    //
    //     · Visual Studio 로 디버그    → CWD = 프로젝트 폴더  → <저장소>/imgui.ini
    //     · 실행 파일을 직접 실행      → CWD = 출력 폴더      → x64/Debug/imgui.ini
    //     · 스크립트로 자동 실행       → CWD = 부른 쪽        → 아무 데나
    //
    //   그런데 "기본 레이아웃을 세울까"를 판정하는 EditorRenderer 는 처음부터
    //   **실행 파일 상대**를 봤다. 둘이 어긋나면 최악의 조합이 나온다 —
    //   판정은 "파일이 있다"고 보아 기본 레이아웃을 안 세우는데, 정작 읽은
    //   파일은 비어 있어서 **창이 전부 도킹되지 않은 채로 뜨고**, 종료할 때
    //   그 빈 상태가 저장되어 원래 레이아웃을 덮는다.
    //
    //   판정 쪽에 맞춰 읽기·쓰기도 실행 파일 상대로 고정한다. 이제 어떻게
    //   띄우든 같은 파일이다.
    static const std::string kIniPath =
        PathFinder::RelativeToExecutable("imgui.ini").string();
    io.IniFilename = kIniPath.c_str();

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
