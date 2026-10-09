#pragma once

#include "../FrameCameraSnapshot.h"
#include "../Render/Temporal/TemporalReconstruction.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

// CPU 표시 브리지도 픽셀과 카메라를 같은 값 묶음으로 운반한다. Host가
// 업로드한 프레임의 신원을 돌려주어야 최신 RT 완료 결과와 섞이지 않는다.
struct RHIDisplayFrameMetadata
{
    uint64_t m_frameId{ 0 };
    uint64_t m_sceneEpoch{ 0 };
    uint64_t m_resizeGeneration{ 0 };
    uint64_t m_viewId{ 0 };
    uint64_t m_historyRevision{ 0 };
    FrameCameraSnapshot m_camera{};
    // 같은 프로세스의 steady_clock 나노초. 진단용이며 완료점이 아니다.
    uint64_t m_sourceCaptureNanoseconds{ 0 };
};

struct RHIDisplayTexture
{
    uint64_t m_textureId{ 0 };
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    RHIDisplayFrameMetadata m_frame{};
};

// 생산자 표시 토큰이 고정 소유권 하나를 가진다. 획득에 성공한 소비자는
// 자신의 GPU 사용이 모두 끝날 때까지 별도 참조를 보관한다.
// 획득과 생산자 재사용 선택은 같은 표시 수명 뮤텍스로 직렬화한다.
// shared_ptr 참조 수만으로는 두 결정을 직렬화할 수 없다.
// 향후 present 제공자도 마지막 사용이 끝날 때까지 같은 lease를 유지해야 한다.
struct RHIDisplayConsumerLease
{
    // 포기된 소비자는 장치 해체 중에도 완료를 증명할 수 없다.
    // 해당 표시 슬롯을 영구 사용 불가로 남겨 덮어쓰기를 막는다.
    std::atomic<bool> m_completionLost{ false };
};

// One completed real-frame export. Shared handles are process-local transport
// identities, not native graphics API objects. The producer publishes only after
// its copy fence completes. Every image is a dedicated, non-aliased allocation;
// the common lease prevents ALL four images from reuse until SDK final consumption.
struct RHITemporalDisplayPacket
{
    TemporalFrame frame;
    // HUD-less post-tone-map RGBA8, premultiplied RGBA8 UI, R32F depth, RG16F motion.
    std::array<void*, 4> sharedHandles{};
    std::shared_ptr<RHIDisplayConsumerLease> consumerLease;
    std::shared_ptr<const void> lifetimeToken;
    bool valid{ false };
    bool nativeGateActive{ false }; // Immutable capture/quality-gate provenance.
};

struct RHITemporalDisplayResources
{
    RHITextureHandle hudlessColor;
    RHITextureHandle uiColor;
    RHITextureHandle depth;
    RHITextureHandle motionVectors;
};

enum class RHITemporalLatencyMarker : uint8_t
{
    InputSample, SimulationStart, SimulationEnd, RenderSubmitStart, RenderSubmitEnd
};
