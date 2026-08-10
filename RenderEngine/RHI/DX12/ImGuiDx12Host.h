#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../IImGuiHost.h"

// IImGuiHost의 DX12 구현 (EditorRenderer 재작성, 2026-08-10).
//
// 구 ImGuiRenderer에서 백엔드 몫만 가라앉힌 것이다: ImGui 컨텍스트 생성과
// DLL 경계 할당자 공유(GlobalImGuiContext), Win32 플랫폼 백엔드, 그리고
// ImGuiDx12Shell 구동 — 셸이 자기 DX12DeviceResources(디바이스·스왑체인·
// 프레임 펜스)를 소유하므로, 이 호스트가 서면 ImGui 표시 경로 전체가
// DX12DeviceResources 위에서 돈다. DX11 DeviceResources는 더 이상 이 경로에
// 없다 — 구 ImGuiRenderer가 그것을 들던 이유는 HWND 하나였다.
//
// 에디터 몫(독스페이스·창 펌프·스케일·폰트)은 여기 없다. EngineGUIWindow/
// EditorRenderer가 IImGuiHost 경계 너머에서 얹는다.
class ImGuiDx12Host final : public IImGuiHost
{
public:
    bool Initialize(void* windowHandle, std::string& outError) override;
    bool IsActive() const override;

    void BeginFrame() override;
    void EndFrame() override;

    void RebuildFontAtlas() override;

    void Shutdown() override;

private:
    void* m_windowHandle{ nullptr };
};

#endif
