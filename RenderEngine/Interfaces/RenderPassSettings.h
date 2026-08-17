#pragma once
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

struct RenderPassSettings
{
   public:
   static consteval auto reflect()
   {
       using Self = RenderPassSettings;
       return meta::schema<Self>(
           meta::field<&Self::aa>,
           meta::field<&Self::ssao>,
           meta::field<&Self::shadow>,
           meta::field<&Self::deferred>,
           meta::field<&Self::bloom>,
           meta::field<&Self::ssgi>,
           meta::field<&Self::vignette>,
           meta::field<&Self::colorGrading>,
           meta::field<&Self::toneMap>,
           meta::field<&Self::volumetricFog>,
           meta::field<&Self::bitMask>,
           meta::field<&Self::skyboxTextureName>,
           meta::field<&Self::m_isSkyboxEnabled>,
           meta::field<&Self::m_windDirection>,
           meta::field<&Self::m_windStrength>,
           meta::field<&Self::m_windSpeed>,
           meta::field<&Self::m_windWaveFrequency>);
   }
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
    std::string             skyboxTextureName{ "kloofendal_43d_clear_puresky_4k.hdr" };
	bool                    m_isSkyboxEnabled{ true };
    Mathf::Vector3		    m_windDirection{ 1.f,0.f,0.f };
	float                   m_windStrength{ 0.1f };
    float				    m_windSpeed{ 1.f };
    float 				    m_windWaveFrequency{ 1.f };
};
