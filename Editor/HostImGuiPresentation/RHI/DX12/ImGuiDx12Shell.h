#pragma once
#if defined(CE_PLAYER)
#error "Editor ImGui presentation is unavailable to Player; use native RHI presentation."
#endif

#include "RHI/RHIDisplayFrame.h"
#include <functional>
#include <memory>
#include <cstdint>
#include <string>

class Texture;

// 사용자 텍스처 descriptor는 마지막 표시 이후 충분히 오래 사용되지 않을 때
// 은퇴시킨다. 실제 descriptor 재사용은 추가로 GPU 완료점을 기다린다.
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

// Editor 전용 ImGui DX12 셸.
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
// 것이다. 생산 펜스가 완료된 슬롯만 열고, 열린 슬롯은 CPU 기록부터
// 셸 GPU 펜스 완료까지 consumer lease로 붙들어 생산 측 덮어쓰기를 막는다.
// 서로 다른 디바이스의 COM 참조만으로는 이 재사용 동기화가 성립하지 않는다.
// 완전 통합(디바이스 하나)은 교체 완주 시점의 몫이다.
//
// ── Editor의 고정 표시 정책 ──
//
// Editor Scene/Game/material preview와 ImGui 표시는 모두 DX12다. 런타임
// 백엔드 선택이나 대체 셸은 없다. Player의 DX12/Vulkan 선택은 native RHI
// 표시 경로가 소유하며, 이 타입이나 ImGui에 의존하지 않는다.
//
// ── 스레드 ──
//
// Initialize는 메인(게임) 스레드(ImGuiHost::Initialize와 같은 자리),
// NewFrame/RenderAndPresent는 CE 스레드(기존 ImGui 흐름 그대로),
// RegisterTexture/OpenSharedTexture는 ImGui 빌드 중(CE 스레드) 호출.
// 큐 제출은 라이브(게임 스레드)와 다른 큐라 겹칠 일도 없다.
class ImGuiDx12Shell final
{
public:
    ImGuiDx12Shell();
    ~ImGuiDx12Shell();

    const char* GetName() const { return "DX12"; }

    bool Initialize(void* windowHandle, uint32_t width, uint32_t height,
        std::string& outError);
    bool IsActive() const;
    // 장치가 제거돼 다시는 표시할 수 없는 상태. 셸은 장치를 되살리지 않는다.
    bool IsDeviceLost() const;

    /// ImGui_ImplDX12_NewFrame. BeginRender의 DX11 NewFrame 자리에 온다.
    void NewFrame();

    /// 대기 업로드 처리 → 백버퍼에 드로우 데이터 렌더 → Present.
    /// EndRender의 DX11 RenderDrawData + DX11 Present 자리를 합쳐 맡는다.
    bool RenderAndPresent(std::string& outError,
        const std::function<void()>& onRecorded);

    /// 창 크기 변경. CE 스레드(BeginRender의 크기 감지 자리)에서 부른다.
    void Resize(uint32_t width, uint32_t height);
    void RebuildFontAtlas();

    /// Texture 자산을 ImGui가 표시할 수 있는 ImTextureID(64비트)로.
    ///
    /// 내용은 DX12로 업로드(DX12TextureCache — dx12.skyscene이 증명한 운반
    /// 경로)되고 셸 SRV 힙에 슬롯을 받는다. 열린 ImGui 프레임에서 즉시
    /// 기록하며, 프레임 밖 호출은 null SRV 슬롯만 예약한다. 실제 표시 프레임은
    /// 매번 다시 호출해 last-used를 갱신해야 한다.
    uint64_t RegisterTexture(Texture* texture);

    /// 그 텍스처의 SRV가 실제로 기록됐는가. 위 주석의 "프레임 밖 호출은 null
    /// SRV 슬롯만 예약한다" 를 밖에서 구분할 수 있게 하는 창구다(W7 썸네일).
    bool IsTextureReady(Texture* texture) const;

    /// 라이브 러너의 공유 텍스처(NT 핸들)를 열어 ImTextureID로. 핸들별 캐시.
    uint64_t OpenSharedTexture(void* sharedHandle,
        std::shared_ptr<RHIDisplayConsumerLease> consumerLease);

    /// 완성된 RGBA8 프레임을 셸 디바이스로 넘기는 CPU 전달 경로.
    ///
    /// Submit은 렌더 스레드, 업로드 기록과 조회는 CE 스레드다.
    /// key별 최신 프레임 하나만 보관하므로 생산자가 셸보다 빨라도 오래된
    /// 리드백이 줄을 서지 않는다. 행은 width * 4 바이트 이상이어야 한다.
    void SubmitCpuRgbaFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch, const RHIDisplayFrameMetadata& frame);

    /// UI 생성 전에 업로드 기록한 픽셀과 메타데이터. 준비 전에는 빈 값을 돌려준다.
    RHIDisplayTexture GetCpuFrameTexture(uint64_t key);

    /// 표시할 것이 없을 때 쓰는 폴백(null 디스크립터 — 0을 읽는 합법 SRV).
    ///
    /// ★ 0을 돌려주면 안 된다. DX12에서 ImTextureID 0은 무효 디스크립터라
    ///   커맨드 리스트를 그 자리에서 오염시키고, 그 뒤 기록이 전부 사라진다
    ///   (에디터 창이 통째로 안 보이는 증상으로 겪었다).
    uint64_t GetFallbackTextureId() const;

    /// 최종 정리. CE 스레드가 멈춘 뒤(Dx11Main::Finalize)에만 부른다.
    void Shutdown();

private:
    ImGuiDx12Shell(const ImGuiDx12Shell&) = delete;
    ImGuiDx12Shell& operator=(const ImGuiDx12Shell&) = delete;

    struct Impl;
    Impl* m_impl;   // 소멸 순서를 Shutdown이 소유한다(정적 소멸에 안 맡긴다)
};
