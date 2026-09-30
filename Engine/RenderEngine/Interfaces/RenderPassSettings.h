#pragma once
#include <mathematics/vector3.hpp>
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "ShadowMapPassSetting.h"
#include "DeferredPassSetting.h"
#include "BloomSetting.h"
#include "SSGIPassSetting.h"
#include "VignettePassSetting.h"
#include "ColorGradingPassSetting.h"
#include "ToneMapPassSetting.h"
#include "AAPassSetting.h"
#include "SSAOPassSetting.h"
#include "VolumetricFogPassSetting.h"
#include "BitMaskPassSetting.h"

struct [[reflgen::reflect]] RenderPassSettings
{
   public:
    RenderPassSettings() = default;

    AAPassSetting           aa{};
    SSAOPassSetting         ssao{};
    ShadowMapPassSetting    shadow{};
    DeferredPassSetting     deferred{};
    BloomPassSetting        bloom{};
    SSGIPassSetting         ssgi{};
    VignettePassSetting     vignette{};
    ColorGradingPassSetting colorGrading{};
    ToneMapPassSetting      toneMap{};
	VolumetricFogPassSetting volumetricFog{};
    BitMaskPassSetting      bitMask{};
    std::string             skyboxTextureName{ "forest.ceibl" };
	bool                    m_isSkyboxEnabled{ false };
    math::vector3		    m_windDirection{ 1.f,0.f,0.f };
	float                   m_windStrength{ 0.1f };
    float				    m_windSpeed{ 1.f };
    float 				    m_windWaveFrequency{ 1.f };
};
