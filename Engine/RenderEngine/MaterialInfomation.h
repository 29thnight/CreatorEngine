#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include <mathematics/color.hpp>

// 범프맵(높이맵 유사 노멀) 상태값 2는 은퇴했다 — 셰이더 소비자가 0이었고
// enhanced 경로는 스냅샷 빌드에서 0/1로 정규화한다. m_useNormalMap은 이제
// normalMap 텍스처 존재에서 유도되는 순수 0/1 상태다(저작·직렬화 없음).
constexpr bool32 kUseNormalMap = 1;

cbuffer [[reflgen::reflect]] MaterialInfomation
{
   public:
    const static UINT  kUseShadowReceive = 256u;

    math::color   m_baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float		  m_metallic{ 0.0f };
    float		  m_roughness{ 1.0f };

    [[reflgen::ignore]]
    bool32		  m_useBaseColor{};

    [[reflgen::ignore]]
    bool32		  m_useOccRoughMetal{};

    [[reflgen::ignore]]
    bool32		  m_useAOMap{};

    [[reflgen::ignore]]
    bool32		  m_useEmissive{};

    [[reflgen::ignore]]
    bool32		  m_useNormalMap{};

    [[reflgen::ignore]]
    bool32		  m_convertToLinearSpace{ false };

    float         m_IOR{ 1.5f };

    MaterialInfomation() = default;
    ~MaterialInfomation() = default;
};
