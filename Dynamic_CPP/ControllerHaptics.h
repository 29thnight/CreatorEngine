// HapticsPattern.h
#pragma once
#include <deque>
#include "InputManager.h"

struct HapticSegment
{
    float duration;        // seconds
    float left;            // 0~1
    float right;           // 0~1
    float lowHz;           // optional (0 or >0)
    float highHz;          // optional
};

class ControllerHaptics
{
public:
    void PlayHeartbeat(DWORD index);
    void PlayHeartbeatStrong(DWORD index, float ampMul = 1.0f, int repeat = 1);
   
    void Stop()
    {
        m_segments.clear();
        m_active = false;
        // 정지
        InputManagement->SetControllerVibration(m_index, 0.f, 0.f, 0.f, 0.f);
        InputManagement->SetControllerVibrationTime(m_index, 0.f);
    }

    // 매 프레임 호출(당신의 UpdateControllerVibration에서 호출)
    void Update(float tick);

private:
    DWORD m_index = 0;
    bool  m_active = false;
    float m_timeLeftInSeg = 0.f;
    std::deque<HapticSegment> m_segments;
};
