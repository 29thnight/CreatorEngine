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
    void PlayHeartbeat(DWORD index)
    {
        m_index = index;
        m_segments.clear();

        // 1) ���� ù �ڵ� (��)
        m_segments.push_back({ 0.06f, 0.95f, 0.40f, 140.f, 110.f }); // ������ ����

        // 2) ª�� ����
        m_segments.push_back({ 0.08f, 0.0f, 0.0f, 0.f, 0.f });

        // 3) ª�� ���� �� ��° �ڵ� (��)
        m_segments.push_back({ 0.05f, 0.75f, 0.25f, 150.f, 180.f });

        m_timeLeftInSeg = 0.f;
        m_active = true;
    }

    void PlayHeartbeatStrong(DWORD index, float ampMul = 1.0f, int repeat = 1)
    {
        m_index = index;
        m_segments.clear();

        // ���� Ŭ����
        auto A = [&](float v) { return std::clamp(v * ampMul, 0.0f, 1.0f); };

        for (int i = 0; i < repeat; ++i)
        {
            // 1) ���� ù �ڵ�: 'ű' ����
            //    - ������(����) ���� Ǯ, ������(������) ���� ª�� ������ũ�� Ÿ�ݰ� �߰�
            m_segments.push_back({ 0.040f, A(1.00f), A(0.30f), 135.f, 10.f }); // 20ms
            m_segments.push_back({ 0.050f, A(0.85f), A(0.15f), 135.f, 20.f }); // ���� 30ms
            // ª�� ����
            m_segments.push_back({ 0.050f, 0.0f, 0.0f, 0.f, 0.f });

            // 2) �� ��° �ڵ�: ���� ���ϰ� ª��
            m_segments.push_back({ 0.028f, A(0.80f), A(0.20f), 150.f, 20.f });
            m_segments.push_back({ 0.042f, A(0.65f), A(0.10f), 150.f, 10.f });

            // (����) ���� ���� ����
            if (i + 1 < repeat)
                m_segments.push_back({ 0.10f, 0.0f, 0.0f, 0.f, 0.f });
        }

        m_timeLeftInSeg = 0.f;
        m_active = true;
    }

    void Stop()
    {
        m_segments.clear();
        m_active = false;
        // ����
        InputManagement->SetControllerVibration(m_index, 0.f, 0.f, 0.f, 0.f);
        InputManagement->SetControllerVibrationTime(m_index, 0.f);
    }

    // �� ������ ȣ��(����� UpdateControllerVibration���� ȣ��)
    void Update(float tick)
    {
        if (!m_active) return;

        if (m_timeLeftInSeg <= 0.f)
        {
            if (m_segments.empty())
            {
                // ���� ����
                Stop();
                return;
            }
            // ���� ���׸�Ʈ ����
            const auto seg = m_segments.front();
            m_segments.pop_front();

            InputManagement->SetControllerVibration(m_index, seg.left, seg.right, seg.lowHz, seg.highHz);
            InputManagement->SetControllerVibrationTime(m_index, seg.duration);
            m_timeLeftInSeg = seg.duration;
        }

        m_timeLeftInSeg -= tick;
        if (m_timeLeftInSeg <= 0.f)
        {
            // ���׸�Ʈ ���� ���� ���� 0���� ����(���� ������ ���׸�Ʈ�� �־����Ƿ� ���⼭ 0���� ���� ����)
            InputManagement->SetControllerVibration(m_index, 0.f, 0.f, 0.f, 0.f);
            InputManagement->SetControllerVibrationTime(m_index, 0.f);
        }
    }

private:
    DWORD m_index = 0;
    bool  m_active = false;
    float m_timeLeftInSeg = 0.f;
    std::deque<HapticSegment> m_segments;
};
