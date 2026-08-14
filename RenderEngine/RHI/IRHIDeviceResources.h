#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <map>
#include <string>

// 디바이스·프레임·프레젠트 인터페이스 (PHASE 3-1 재정의, D1).
//
// ── 왜 이 층이 필요한가 ──
//
// DX12로 이관하면서 디바이스 소유가 둘로 갈렸다. 창과 스왑체인은 아직
// Utility_Framework/DeviceResources(DX11)가 쥐고 있고, 씬은 DX12DeviceResources가
// 오프스크린으로 그린다. 그 둘을 잇는 계약이 코드 어디에도 없어서 "누가 무엇을
// 소유하는가"가 호출 순서로만 존재했다.
//
// 이 인터페이스가 그 계약이다. DX11 쪽은 여기 들어오지 않는다 — 은퇴 대상이라
// 구현할 이유가 없고, 넣으면 아래 §설계 판단의 함정에 빠진다.
//
// ── 설계 판단: DX11을 공통 분모에 넣지 않는다 ──
//
// ★ 기존 RHI(PHASE 3-1 1차)가 죽은 이유가 정확히 그것이었다. DX11 즉시 컨텍스트
//   모델로 인터페이스를 잡아 놓으니 DX12가 들어갈 자리가 없었고, 결국 아무도
//   쓰지 않는 층이 됐다(Device()·Immediate() 호출 0곳).
//
//   그래서 이 인터페이스는 명시적 API 둘 — DX12와 (앞으로의) Vulkan — 만
//   기준으로 잡는다. 프레임 경계·펜스·스왑체인·커맨드 제출이 전부 명시적이라는
//   전제 위에 서 있고, 그 전제가 두 API에 공통이다:
//
//     BeginFrame/EndFrame  → 커맨드 리스트 열고 닫기 / 커맨드 버퍼 begin·end
//     펜스 값 질의          → ID3D12Fence / 타임라인 세마포어
//     AttachSwapChain      → IDXGISwapChain / VkSwapchainKHR
//     WaitForGpu           → 펜스 대기 / vkDeviceWaitIdle
//
//   DX11을 여기 끼우려면 이 넷을 전부 흐려야 한다. 그러면 Vulkan을 붙일 때
//   흐려진 그 부분이 그대로 걸림돌이 된다.
//
// ── 무엇이 여기 없는가, 그리고 왜 ──
//
//   · Initialize — 백엔드마다 인자가 다르다(DX12는 어댑터 LUID 일치와 화면
//     크기 추종, Vulkan은 인스턴스·확장 목록). 생성은 구현이 하고 이 인터페이스는
//     '생성된 뒤'를 다룬다. 억지로 공통 서명을 만들면 인자가 옵션 뭉치가 된다.
//
//   · 디바이스·큐·커맨드 리스트 핸들 — ID3D12Device* 같은 것들. 이것을 여기
//     노출하면 인터페이스가 곧 DX12가 된다. 패스가 프레임 안에서 쓰는 서비스는
//     별개 층(RenderFrameServices.h의 IRenderDeviceServices)이 담당하고,
//     그쪽은 아직 DX12 타입을 노출하되 R2·R3에서 중립화된다.
//
//   · 백버퍼 리소스·RTV 핸들 — 백엔드 타입이다. 이것을 쓰는 것은 셸뿐이고
//     셸은 백엔드 전용이라 구체 타입을 봐도 된다.
//
// ── 구현 ──
//
//   RHI/DX12/DX12DeviceResources   (지금)
//   RHI/Vulkan/VulkanDeviceResources (나중 — 이 인터페이스가 그 자리를 만든다)

/// 어댑터의 비디오 메모리 사용량. DXGI의 DXGI_QUERY_VIDEO_MEMORY_INFO를
/// 백엔드 타입 없이 옮긴 것이다 — 원래도 어댑터 질의라 백엔드와 무관했다.
struct RHIVideoMemoryInfo
{
    uint64_t usedMB{ 0 };
    uint64_t budgetMB{ 0 };
};

/// 디버그 레이어가 보고한 살아있는 GPU 객체 집계.
/// "지금 몇 개가 살아있는가"를 숫자로 남겨 리소스 누수 수정의 효과를
/// 전후 비교할 수 있게 하는 것이 목적이다.
struct RHIGpuObjectCensus
{
    bool     available{ false };            // 디버그 레이어가 없으면 false (VRAM만 유효)
    uint32_t totalObjects{ 0 };
    std::map<std::string, uint32_t> byType; // 예: "ID3D12Resource" -> 47
    uint64_t vramUsedMB{ 0 };
    uint64_t vramBudgetMB{ 0 };
};

class IRHIDeviceResources
{
public:
    virtual ~IRHIDeviceResources() = default;

    virtual bool IsInitialized() const = 0;
    virtual void Shutdown() = 0;

    // ── 크기 ──
    //
    // 창 크기 변경 시 크기에 딸린 리소스를 다시 만든다. 크기 추종 자체는
    // 백엔드 중립 버스(RHI/ScreenSizedResource.h)가 알리고 이쪽은 구독자다.
    virtual bool Resize(uint32_t width, uint32_t height, std::string& outError) = 0;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;

    // ── 프레임 경계 ──
    //
    // 명시적 API의 뼈대다. BeginFrame이 이번 프레임 슬롯의 펜스를 기다리고
    // 얼로케이터·링을 되감으며, EndFrame이 제출하고 펜스에 서명한다.
    virtual bool BeginFrame(std::string& outError) = 0;
    virtual bool EndFrame(std::string& outError) = 0;

    /// 아직 제출하지 않은 현재 recording을 버린다. 이미 제출된 앞 구간은
    /// 각 완료점까지 pending으로 남고, 현재 업로드 예약만 즉시 반환한다.
    virtual void AbortFrame() = 0;

    /// 프레임 중간 제출. 업로드가 워커 기록보다 먼저 실행되어야 할 때 쓴다.
    virtual bool FlushCommandList(std::string& outError) = 0;

    /// 모든 제출 완료까지 대기. 리드백 직전과 종료 때만 부른다 —
    /// 정상 프레임 경로에서 부르면 프레임을 겹칠 수 없다.
    virtual void WaitForGpu() = 0;

    // ── 완료 질의 (논블로킹) ──
    //
    // 상시 러너가 '이 프레임이 끝났는가'를 CPU 대기 없이 묻는 데 쓴다.
    // 이것이 있어서 표시 슬롯 승격이 WaitForGpu 없이 성립한다.
    virtual uint64_t GetLastSignaledFenceValue() const = 0;
    virtual uint64_t GetCompletedFenceValue() const = 0;

    // ── 프레젠트 ──
    //
    // windowHandle은 플랫폼 창 핸들이다(Win32에서는 HWND). void*로 두는 이유는
    // 이 헤더가 어떤 플랫폼·그래픽 헤더도 끌어오지 않기 위해서다 — 구 RHI의
    // RHIDefinitions.h가 세운 규약이고, 그 파일은 R5에서 걷혔지만 규약은
    // 여기서 이어진다.
    //
    // ★ 한 창에 스왑체인은 하나다(DXGI 제약). 그래서 이 인터페이스가
    //   '소유권'의 자리이기도 하다 — 누가 AttachSwapChain을 부르는가가
    //   곧 누가 화면을 쥐는가다. 지금은 DX11이 쥐고 있어 ImGui DX12 셸이
    //   기본 꺼짐이며, 그 이관(D2)이 이 계약 위에서 이루어진다.
    virtual bool AttachSwapChain(void* windowHandle, uint32_t width, uint32_t height,
        std::string& outError) = 0;
    virtual bool ResizeSwapChain(uint32_t width, uint32_t height, std::string& outError) = 0;
    virtual bool Present(std::string& outError) = 0;
    virtual bool HasSwapChain() const = 0;
    virtual uint32_t GetBackBufferIndex() const = 0;

    // ── 진단 ──
    //
    // 검증 레이어 메시지를 비우고 '실제 문제'(경고 이상)의 수를 돌려준다.
    // DX12는 ID3D12InfoQueue, Vulkan은 validation layer 콜백이 출처다.
    virtual uint32_t DrainDebugMessages(std::string& outMessages) = 0;

    /// 어댑터의 비디오 메모리. 가벼운 질의라 주기 갱신에 써도 된다.
    virtual RHIVideoMemoryInfo QueryVideoMemory() const = 0;

    /// 살아있는 GPU 객체를 타입별로 집계한다. 디버그 레이어가 없으면
    /// available=false인 채 VRAM 수치만 채워 돌아온다.
    ///
    /// ★ allowDeviceEnumeration은 "이후 렌더가 없다"는 호출자의 약속이다.
    ///   타입별 집계는 디버그 레이어에 디바이스 자식 객체를 전부 나열시키는데,
    ///   그 호출은 '디바이스를 파괴하기 직전에 한 번' 쓰라고 만들어진 것이다.
    ///   DX11에서 이것을 실행 중에 불렀다가 커맨드 리스트 재활용 경로가 망가져
    ///   워커 스레드가 죽는 것을 겪었다(재현 3/3). 렌더 스레드를 멈춰도 소용
    ///   없었다 — 순회 시점의 경합이 아니라 순회가 남긴 부작용이라 그 뒤 아무
    ///   때나 터졌다. DX12에서 같은 일이 난다는 증거는 아직 없지만, 같은 성격의
    ///   API라 같은 약속을 요구한다.
    virtual RHIGpuObjectCensus CaptureLiveObjectCensus(bool allowDeviceEnumeration) = 0;

    /// 살아있는 객체를 디버거 출력 창에 그대로 쏟는다. 집계(위)와 달리 문자열을
    /// 돌려주지 않고 파싱도 하지 않는다 — 종료 직전에 "무엇이 남았는가"를
    /// 사람이 읽는 용도다.
    ///
    /// ★ 집계가 비어 있어도 이쪽은 나온다. 집계는 진단 대상 디바이스가 살아
    ///   있어야 하는데 그것이 먼저 해체되는 반면, 이 보고는 프로세스 범위의
    ///   DXGI 디버그 계층이 함께 훑기 때문이다.
    virtual void ReportLiveObjectsToDebugOutput() = 0;
};

// ── 진단 대상 디바이스 ──
//
// GPU 진단(VRAM·라이브 객체)을 어느 디바이스에 물을지는 명시적으로 정한다.
//
// ★ 지금 DX12DeviceResources 인스턴스가 둘이다 — 상시 러너(라이브 렌더러)와
//   ImGui 셸. 어댑터가 같으므로 VRAM은 어느 쪽에 물어도 같지만, 라이브 객체
//   집계는 디바이스 단위라 답이 갈린다. 엔진 자산(텍스처·메시·타깃)이 전부
//   라이브 쪽에 올라가므로 누수도 거기서 나고, 셸이 드는 것은 ImGui 폰트·
//   아이콘뿐이라 누수 조사 대상이 아니다. 그래서 라이브가 자신을 등록한다.
//
//   '마지막에 만들어진 것이 활성'(구 DX11 DeviceResources의 s_active) 대신
//   명시 등록을 쓰는 이유가 이것이다 — 둘 중 하나를 고르는 판단이 생성 순서에
//   묻히면 안 된다.
//
// 주의: 정적 링크되는 모듈마다 별도 실체를 가지므로 엔진 실행 파일 안에서만 쓴다.
void SetDiagnosticsDeviceResources(IRHIDeviceResources* resources);

/// 아직 등록 전이거나 해제 후면 nullptr. 호출부는 반드시 널을 검사한다 —
/// 라이브 파이프라인이 서기 전(부팅)과 해체 후(종료)가 실제로 존재한다.
IRHIDeviceResources* GetDiagnosticsDeviceResources();

#endif
