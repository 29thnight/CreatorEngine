#include "ControllerHaptics.h"

void ControllerHaptics::PlayHeartbeat(DWORD index)
{
    m_index = index;
    m_segments.clear();

    // 1) 강한 첫 박동 (쿵)
    m_segments.push_back({ 0.06f, 0.95f, 0.40f, 140.f, 110.f }); // 저주파 위주

    // 2) 짧은 휴지
    m_segments.push_back({ 0.08f, 0.0f, 0.0f, 0.f, 0.f });

    // 3) 짧고 약한 두 번째 박동 (쿵)
    m_segments.push_back({ 0.05f, 0.75f, 0.25f, 150.f, 180.f });

    m_timeLeftInSeg = 0.f;
    m_active = true;
}

void ControllerHaptics::PlayHeartbeatStrong(DWORD index, float ampMul, int repeat)
{
    m_index = index;
    m_segments.clear();

    // 안전 클램프
    auto A = [&](float v) { return std::clamp(v * ampMul, 0.0f, 1.0f); };

    for (int i = 0; i < repeat; ++i)
    {
        // 1) 강한 첫 박동: '킥' 느낌
        //    - 저주파(왼쪽) 거의 풀, 고주파(오른쪽) 아주 짧은 스파이크로 타격감 추가
        m_segments.push_back({ 0.040f, A(1.00f), A(0.30f), 135.f, 10.f }); // 20ms
        m_segments.push_back({ 0.050f, A(0.85f), A(0.15f), 135.f, 20.f }); // 유지 30ms
        // 짧은 휴지
        m_segments.push_back({ 0.050f, 0.0f, 0.0f, 0.f, 0.f });

        // 2) 두 번째 박동: 조금 약하고 짧게
        m_segments.push_back({ 0.028f, A(0.80f), A(0.20f), 150.f, 20.f });
        m_segments.push_back({ 0.042f, A(0.65f), A(0.10f), 150.f, 10.f });

        // (선택) 패턴 사이 휴지
        if (i + 1 < repeat)
            m_segments.push_back({ 0.10f, 0.0f, 0.0f, 0.f, 0.f });
    }

    m_timeLeftInSeg = 0.f;
    m_active = true;
}

void ControllerHaptics::Update(float tick)
{
    if (!m_active) return;

    if (m_timeLeftInSeg <= 0.f)
    {
        if (m_segments.empty())
        {
            // 패턴 종료
            Stop();
            return;
        }
        // 다음 세그먼트 적용
        const auto seg = m_segments.front();
        m_segments.pop_front();

        InputManagement->SetControllerVibration(m_index, seg.left, seg.right, seg.lowHz, seg.highHz);
        InputManagement->SetControllerVibrationTime(m_index, seg.duration);
        m_timeLeftInSeg = seg.duration;
    }

    m_timeLeftInSeg -= tick;
    if (m_timeLeftInSeg <= 0.f)
    {
        // 세그먼트 끝난 순간 진동 0으로 스냅(휴지 구간을 세그먼트로 넣었으므로 여기서 0으로 꺼도 안전)
        InputManagement->SetControllerVibration(m_index, 0.f, 0.f, 0.f, 0.f);
        InputManagement->SetControllerVibrationTime(m_index, 0.f);
    }
}
