#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include <mathematics/color.hpp>

constexpr bool32 USE_NORMAL_MAP = 1;
constexpr bool32 USE_BUMP_MAP = 2;

cbuffer MaterialInfomation
{
   public:
   static consteval auto reflect()
   {
       using Self = MaterialInfomation;
       return meta::schema<Self>(
           meta::field<&Self::m_baseColor>,
           meta::field<&Self::m_metallic>,
           meta::field<&Self::m_roughness>,
           meta::field<&Self::m_IOR>);
   }
    const static UINT  USE_SHADOW_RECIVE = 256u;

    math::color   m_baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float		  m_metallic{ 0.0f };
    float		  m_roughness{ 1.0f };
    bool32		  m_useBaseColor{};
    bool32		  m_useOccRoughMetal{};
    bool32		  m_useAOMap{};
    bool32		  m_useEmissive{};
    bool32		  m_useNormalMap{};
    bool32		  m_convertToLinearSpace{ false };
    float         m_IOR{ 1.5f };

    MaterialInfomation() = default;
    ~MaterialInfomation() = default;
};
