#pragma once
#include "Core.Minimal.h"
#include "CurvePoint.h"
#include "../Fmod/inc/fmod.hpp"
#include "../Fmod/inc/fmod_errors.h"

enum class ChannelType
{
    BGM = 0,
    SFX,
    PLAYER,
    MONSTER,
    UI,
    MaxChannel
};
AUTO_REGISTER_ENUM(ChannelType)

enum class StealPolicy
{
    Oldest,        // ���� ���� ����� ä�� ����
    Quietest,      // getAudibility()�� ���� ���� ä�� ����
    LowestPriority // priority�� ���� ���� ä�� ���� (����Ƽ AudioSource.priority ����)
};
AUTO_REGISTER_ENUM(StealPolicy)

enum class Rolloff 
{ 
    Linear, 
    Inverse, 
    Custom /*TODO*/ 
};
AUTO_REGISTER_ENUM(Rolloff)

struct GroupConfig
{
    int         maxVoices = 0;         // 0�̸� ������
    StealPolicy policy = StealPolicy::Oldest;
    bool        preemptSameClip = false; // ���� Ŭ�� �ߺ��� ���� �� �켱 ��ƿ
};

// === ��ƿ ===
static inline float dbToLinear(float db) { return powf(10.0f, db / 20.0f); }
static inline float sliderToDb(int percent)
{
    const float minDb = -60.0f;
    if (percent <= 0) return -80.0f;
    return minDb + (0.0f - minDb) * (percent / 100.0f);
}
static inline void ComputeEqualPower(float t, float& w2D, float& w3D) {
    t = std::clamp(t, 0.0f, 1.0f);
    w2D = cosf(t * (3.14159265f * 0.5f));
    w3D = sinf(t * (3.14159265f * 0.5f));
}
