#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <string>

// ImGui 표시 호스트의 백엔드 중립 계약 (EditorRenderer 재작성, 2026-08-10).
//
// ── 왜 경계인가 ──
//
// 구 ImGuiRenderer는 두 층의 일을 한 몸에 겸직했다: 백엔드 가동(컨텍스트·
// 플랫폼 백엔드·ImGui_ImplDX12·프레젠트)과 에디터 오케스트레이션(독스페이스·
// 창 펌프·스케일·폰트). 그 겸직의 값은 Player가 치렀다 — 에디터 창이 하나도
// 없는데 화면에 픽셀을 내려면 ImGuiRenderer를 통째로 들어야 했고, 그 바람에
// 에디터 독스페이스 빌더와 ImGuiRegister 펌프가 Player에서도 매 프레임 돌았다.
//
// 이 계약이 절단면이다. 백엔드 일은 전부 이 인터페이스 뒤(RHI/DX12)에 살고,
// 소비자 둘은 층이 다르다:
//   · Player         — BeginFrame/EndFrame만 부르는 표시 소비자
//   · EditorRenderer — 그 위에 에디터 오케스트레이션을 얹는다 (Academy_4Q,
//                      Editor 필터)
//
// R축이 패스에 한 처방과 같다: 상위 개념은 백엔드 폴더 밖, DX12는 경계 뒤.
class IImGuiHost
{
public:
    virtual ~IImGuiHost() = default;

    /// 컨텍스트·플랫폼 백엔드·렌더 백엔드 가동. windowHandle은 Win32 HWND다
    /// — void*인 이유는 이 헤더가 플랫폼·그래픽 헤더를 끌지 않기 위해서다
    /// (IRHIDeviceResources::AttachSwapChain과 같은 규약).
    ///
    /// 실패해도 프레임 호출은 안전하다(백엔드가 조용히 건너뛴다). 실패 로그는
    /// 구현이 남기고, 호출자는 반환값으로 추가 문맥만 얹는다.
    virtual bool Initialize(void* windowHandle, std::string& outError) = 0;
    virtual bool IsActive() const = 0;

    /// 창 크기 추적 → 백엔드 리사이즈 → 백엔드/플랫폼 NewFrame → ImGui::NewFrame.
    virtual void BeginFrame() = 0;

    /// ImGui::Render → 백버퍼 드로우 → Present → 멀티 뷰포트 플랫폼 창.
    virtual void EndFrame() = 0;

    /// 폰트 아틀라스 재빌드. 소비자가 io.Fonts를 바꾼 뒤 부른다 — 백엔드의
    /// 디바이스 오브젝트 재생성이 필요해서 계약에 있다(폰트 텍스처는 백엔드
    /// 소유물이다).
    virtual void RebuildFontAtlas() = 0;

    /// 최종 정리. 렌더 스레드가 멈춘 뒤에만 부른다.
    virtual void Shutdown() = 0;
};

/// 현재 백엔드의 호스트. 정의는 RHI/DX12/ImGuiDx12Host.cpp에 있다 —
/// 백엔드가 하나뿐이라 팩토리 대신 자유 함수다(두 번째 백엔드가 서면
/// 그때 선택이 생긴다).
IImGuiHost& GetImGuiHost();

#endif
