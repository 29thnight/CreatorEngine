#pragma once
#include "Core.Minimal.h"
#include "MaterialInfomation.generated.h"

constexpr bool32 USE_NORMAL_MAP = 1;
constexpr bool32 USE_BUMP_MAP = 2;

cbuffer MaterialInfomation
{
    const static UINT  USE_SHADOW_RECIVE = 256u;

    [[Property]]
    Mathf::Color4 m_baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    [[Property]]
    float		  m_metallic{ 0.0f };
    [[Property]]
    float		  m_roughness{ 1.0f };
    bool32		  m_useBaseColor{};
    bool32		  m_useOccRoughMetal{};
    bool32		  m_useAOMap{};
    bool32		  m_useEmissive{};
    bool32		  m_useNormalMap{};
    bool32		  m_convertToLinearSpace{ false };
    [[Property]]
    UINT          m_bitflag{};
    [[Property]]
    float         m_IOR{ 1.5f };

   ReflectMaterialInfomation
    [[Serializable]]
    MaterialInfomation() = default;
    ~MaterialInfomation() = default;
};
