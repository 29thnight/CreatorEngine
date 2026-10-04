#pragma once

#include "../FrameCameraSnapshot.h"
#include <cstdint>

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
};

struct RHIDisplayTexture
{
    uint64_t m_textureId{ 0 };
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    RHIDisplayFrameMetadata m_frame{};
};
