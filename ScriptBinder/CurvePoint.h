#pragma once
#include <vector>
#include "CurvePoint.generated.h"

struct CurvePoint
{
    [[Property]]
    float distance = 0.f;
    [[Property]]
    float gain = 1.f;

   ReflectCurvePoint
    [[Serializable]]
    CurvePoint() = default;
    ~CurvePoint() = default;

    CurvePoint(float _dist, float _gain) : distance(_dist), gain(_gain)
    {
    }
};

static inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

static inline void BuildLinearCurve(std::vector<CurvePoint>& curve,
    float minD, float maxD)
{
    minD = std::max(0.0f, minD);
    maxD = std::max(minD + 0.01f, maxD);
    curve.clear();
    // 0~minD ������ 1 ����, minD~maxD ���� ����
    curve.push_back({ 0.f,   1.f });
    curve.push_back({ minD,  1.f });
    curve.push_back({ maxD,  0.f });
}

static inline void BuildInverseCurve(std::vector<CurvePoint>& curve,
    float minD, float maxD)
{
    minD = std::max(0.01f, minD);
    maxD = std::max(minD + 0.01f, maxD);
    curve.clear();
    curve.push_back({ 0.f,  1.f });
    curve.push_back({ minD, 1.f });

    // ���� ����Ʈ�� �ε巯�� ������(�뷫 minD/d) �ٻ�
    const int S = 16;
    for (int i = 1; i < S; i++) {
        float t = i / (float)S;
        float d = minD + (maxD - minD) * t;
        float g = clamp01(minD / d);          // ~ 1/r
        curve.push_back({ d, g });
    }
    curve.push_back({ maxD, clamp01(minD / maxD) }); // ���� 0~�Ҽ�
}
