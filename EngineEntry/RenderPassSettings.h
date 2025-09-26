#pragma once
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
#include "RenderPassSettings.generated.h"

struct RenderPassSettings
{
   ReflectRenderPassSettings
    [[Serializable]]
    RenderPassSettings() = default;

	[[Property]]
    AAPassSetting           aa{};
	[[Property]]
    SSAOPassSetting         ssao{};
	[[Property]]
    ShadowMapPassSetting    shadow{};
	[[Property]]
    DeferredPassSetting     deferred{};
	[[Property]]
    BloomPassSetting        bloom{};
	[[Property]]
    SSGIPassSetting         ssgi{};
	[[Property]]
    VignettePassSetting     vignette{};
	[[Property]]
    ColorGradingPassSetting colorGrading{};
	[[Property]]
    ToneMapPassSetting      toneMap{};
    [[Property]]
	VolumetricFogPassSetting volumetricFog{};
    [[Property]]
    std::string             skyboxTextureName{ "rosendal_park_sunset_puresky_4k.hdr" };
    [[Property]]
	bool                    m_isSkyboxEnabled{ true };
    [[Property]]
    Mathf::Vector3		    m_windDirection{ 1.f,0.f,0.f };
    [[Property]]
	float                   m_windStrength{ 0.1f };
    [[Property]]
    float				    m_windSpeed{ 1.f };
    [[Property]]
    float 				    m_windWaveFrequency{ 1.f };
};
