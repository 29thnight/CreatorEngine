#pragma once

#include <cstdint>

// 라이브 표시 결과를 presentation 계층에 여는 백엔드 중립 계약 (E4-6a).
//
// Core(라이브 렌더러)는 표시 텍스처의 생성·폐기·수명 락 오케스트레이션만
// 소유하고, 그 결과를 화면에 올릴 수단(현재 ImGui 셸)은 Host가 이 sink로
// 설치한다 — Core는 ImGui를 모른다. 예전에는 Core가 GetImGuiHost()를 네
// 곳에서 직접 불렀고, 그 방향이 §4.5("ImGui 사용 == Editor 전용으로 일괄
// 분류하지 않는다"의 이면인 "RenderCore가 ImGui backend를 소유하지 않는다")
// 을 어겼다.
//
// 구현은 Host별 소형 어댑터(GetImGuiHost 위임)다. 과도기의 Editor/Player
// 중복 ~20줄은 E4-6b(HostImGuiPresentation 프로젝트)가 흡수한다.
struct IDisplayPresentationSink
{
    virtual ~IDisplayPresentationSink() = default;

    /// presentation 백엔드가 살아 있는가. 죽어 있으면 표시 ID는 0이다.
    virtual bool IsActive() const = 0;

    /// 진단 문자열용 백엔드 이름. 살아 있지 않으면 "none" 류를 돌려준다.
    virtual const char* GetName() const = 0;

    /// 공유 핸들 텍스처(DX12 표시 슬롯)를 열어 presentation 텍스처 ID를
    /// 돌려준다. 호출자는 표시 수명 락 아래에서 부른다 — 핸들 retire와
    /// 직렬화되는 것은 Core의 몫이고, 구현은 열기만 한다.
    virtual uint64_t OpenSharedTexture(void* sharedHandle) = 0;

    /// 리드백 프레임(CPU RGBA — Vulkan 표시 브리지)을 키로 게시한다.
    /// RenderThread에서 불린다 — 구현은 그 스레드에서 안전해야 한다
    /// (기존 ImGui 셸의 SubmitCpuRgbaFrame과 같은 계약).
    virtual void SubmitCpuFrame(uint64_t key, uint32_t width, uint32_t height,
        const void* rgba, uint32_t rowPitch) = 0;

    /// 게시된 CPU 프레임의 presentation 텍스처 ID.
    virtual uint64_t GetCpuFrameTextureId(uint64_t key) = 0;
};
