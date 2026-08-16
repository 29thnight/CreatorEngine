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
   static consteval auto describe()
   {
       return meta::describe<RenderPassSettings>(
           meta::member<&RenderPassSettings::aa>(),
           meta::member<&RenderPassSettings::ssao>(),
           meta::member<&RenderPassSettings::shadow>(),
           meta::member<&RenderPassSettings::deferred>(),
           meta::member<&RenderPassSettings::bloom>(),
           meta::member<&RenderPassSettings::ssgi>(),
           meta::member<&RenderPassSettings::vignette>(),
           meta::member<&RenderPassSettings::colorGrading>(),
           meta::member<&RenderPassSettings::toneMap>(),
           meta::member<&RenderPassSettings::volumetricFog>(),
           meta::member<&RenderPassSettings::bitMask>(),
           meta::member<&RenderPassSettings::skyboxTextureName>(),
           meta::member<&RenderPassSettings::m_isSkyboxEnabled>(),
           meta::member<&RenderPassSettings::m_windDirection>(),
           meta::member<&RenderPassSettings::m_windStrength>(),
           meta::member<&RenderPassSettings::m_windSpeed>(),
           meta::member<&RenderPassSettings::m_windWaveFrequency>());
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
