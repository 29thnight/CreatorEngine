#pragma once
#include "IImGuiRendererBackend.h"
#include <cstdint>
#include <string>

class Texture;

// ImGui 표시 호스트의 백엔드 중립 계약 (EditorRenderer 재작성, 2026-08-10).
//
// ── 왜 경계인가 ──
//
// 구 ImGuiRenderer는 두 층의 일을 한 몸에 겸직했다: 백엔드 가동(컨텍스트·
// 플랫폼 백엔드·GPU renderer backend·프레젠트)과 에디터 오케스트레이션(독스페이스·
// 창 펌프·스케일·폰트). 그 겸직의 값은 Player가 치렀다 — 에디터 창이 하나도
// 없는데 화면에 픽셀을 내려면 ImGuiRenderer를 통째로 들어야 했고, 그 바람에
// 에디터 독스페이스 빌더와 ImGuiRegister 펌프가 Player에서도 매 프레임 돌았다.
//
// 이 계약이 절단면이다. 백엔드 일은 전부 이 인터페이스 뒤(RHI)에 살고,
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
    /// 실패 뒤 프레임 호출은 안전하지만 호출자는 부팅을 중단해야 한다. 명시한
    /// backend 대신 다른 renderer를 만드는 fallback은 이 계약에 없다.
    virtual bool Initialize(void* windowHandle, std::string& outError) = 0;
    virtual bool IsActive() const = 0;
    virtual ImGuiRendererBackendKind GetBackendKind() const = 0;
    virtual const char* GetBackendName() const = 0;

    /// 창 크기 추적 → 백엔드 리사이즈 → 백엔드/플랫폼 NewFrame → ImGui::NewFrame.
    virtual void BeginFrame() = 0;

    /// ImGui::Render → 백버퍼 드로우 → Present → 멀티 뷰포트 플랫폼 창.
    virtual void EndFrame() = 0;

    /// 폰트 아틀라스 재빌드. 소비자가 io.Fonts를 바꾼 뒤 부른다 — 백엔드의
    /// 디바이스 오브젝트 재생성이 필요해서 계약에 있다(폰트 텍스처는 백엔드
    /// 소유물이다).
    virtual void RebuildFontAtlas() = 0;

    /// ImGui::Image가 소비할 백엔드별 텍스처 ID. 상위 에디터 코드는 더 이상
    /// DX12 descriptor handle 또는 Vulkan descriptor set을 구분하지 않는다.
    /// ID를 영구 캐시하지 말고 실제 표시 프레임마다 호출해 수명 표식을 남긴다.
    virtual uint64_t RegisterTexture(Texture* texture) = 0;
    virtual uint64_t OpenSharedTexture(void* sharedHandle) = 0;
    virtual void SubmitCpuRgbaFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch) = 0;
    virtual uint64_t GetCpuFrameTextureId(uint64_t key) = 0;
    virtual uint64_t GetFallbackTextureId() const = 0;

    /// 최종 정리. 렌더 스레드가 멈춘 뒤에만 부른다.
    virtual void Shutdown() = 0;
};

/// 부팅 때 선택된 DX12/Vulkan 렌더러 백엔드를 감싼 공통 Win32 호스트.
IImGuiHost& GetImGuiHost();
