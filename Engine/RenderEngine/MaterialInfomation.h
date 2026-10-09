#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include <mathematics/color.hpp>

// Runtime-only normal encoding, derived from the bound image format.
// 0: absent, 1: XYZ normal, 3: XY normal with positive-Z reconstruction (BC5).
constexpr bool32 kUseNormalMap = 1;
constexpr bool32 kNormalMapReconstructZ = 2;

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
