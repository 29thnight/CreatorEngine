#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>
#include <Windows.h>

class Texture;

// ImGui의 DX12 셸 (3-9 임시 승격 · PHASE 8-2 선취).
//
// ── 무엇인가 ──
//
// ImGui 출력 경로를 DX11 스왑체인에서 DX12 스왑체인으로 옮긴다. 자체
// DX12 디바이스(DX11과 같은 어댑터)와 스왑체인, ImGui 전용 SRV 힙을
// 소유하고, ImGui_ImplDX12_* 백엔드를 초기화·구동한다.
//
// ── 두 디바이스 설계(의도적) ──
//
// 상시 러너(EnhancedSceneRenderer Live)의 디바이스와 셸의 디바이스는
// 별개다. 라이브의 프레임 구조(슬롯·펜스·풀)를 건드리지 않기 위해서다.
// 씬 뷰는 라이브가 이미 만드는 공유 핸들을 이쪽 디바이스에서
// OpenSharedHandle로 열어 표시한다 — DX11이 하던 일을 DX12가 이어받는
// 것뿐이라 동기화 계약(펜스 완료 슬롯만 표시)도 그대로 성립한다.
// 완전 통합(디바이스 하나)은 교체 완주 시점의 몫이다.
//
// ── 백엔드 선택은 부팅 시 고정 ──
//
// ImGui 백엔드는 런타임 핫스왑이 성립하지 않는다(폰트·버퍼·플랫폼 창이
// 백엔드에 묶인다). EngineSetting의 renderBackendDx12가 부팅에 이 셸을
// 켜고, render.backend 런타임 토글은 씬 경로만 오간다 — 셸까지 되돌리려면
// 설정을 끄고 재시작한다.
//
// ── 스레드 ──
//
// Initialize는 메인(게임) 스레드(ImGuiRenderer 생성자와 같은 자리),
// NewFrame/RenderAndPresent는 CE 스레드(기존 ImGui 흐름 그대로),
// RegisterTexture/OpenSharedTexture는 ImGui 빌드 중(CE 스레드) 호출.
// 큐 제출은 라이브(게임 스레드)와 다른 큐라 겹칠 일도 없다.
class ImGuiDx12Shell
{
public:
    static ImGuiDx12Shell& Get();

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height, std::string& outError);
    bool IsActive() const;

    /// ImGui_ImplDX12_NewFrame. BeginRender의 DX11 NewFrame 자리에 온다.
    void NewFrame();

    /// 대기 업로드 처리 → 백버퍼에 드로우 데이터 렌더 → Present.
    /// EndRender의 DX11 RenderDrawData + DX11 Present 자리를 합쳐 맡는다.
    bool RenderAndPresent(std::string& outError);

    /// 창 크기 변경. CE 스레드(BeginRender의 크기 감지 자리)에서 부른다.
    void Resize(uint32_t width, uint32_t height);

    /// DX11 Texture를 ImGui가 표시할 수 있는 ImTextureID(64비트)로.
    ///
    /// 내용은 DX12로 업로드(DX12TextureCache — dx12.skyscene이 증명한 운반
    /// 경로)되고 셸 SRV 힙에 슬롯을 받는다. 업로드는 다음 RenderAndPresent의
    /// 프레임 안에서 처리되므로 첫 호출 프레임은 0(빈 표시)을 돌려줄 수 있다.
    uint64_t RegisterTexture(Texture* texture);

    /// 라이브 러너의 공유 텍스처(NT 핸들)를 열어 ImTextureID로. 핸들별 캐시.
    uint64_t OpenSharedTexture(HANDLE sharedHandle);

    /// 최종 정리. CE 스레드가 멈춘 뒤(Dx11Main::Finalize)에만 부른다.
    void Shutdown();

private:
    ImGuiDx12Shell();
    ~ImGuiDx12Shell();
    ImGuiDx12Shell(const ImGuiDx12Shell&) = delete;
    ImGuiDx12Shell& operator=(const ImGuiDx12Shell&) = delete;

    struct Impl;
    Impl* m_impl;   // 소멸 순서를 Shutdown이 소유한다(정적 소멸에 안 맡긴다)
};

#endif
