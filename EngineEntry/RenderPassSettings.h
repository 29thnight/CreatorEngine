#pragma once
#include "AAPassSetting.h"
#include "SSAOPassSetting.h"
#include "ShadowMapPassSetting.h"
#include "DeferredPassSetting.h"
#include "BloomSetting.h"
#include "SSGIPassSetting.h"
#include "VignettePassSetting.h"
#include "ColorGradingPassSetting.h"
#include "ToneMapPassSetting.h"
#include "RenderPassSettings.generated.h"

struct RenderPassSettings
{
   ReflectRenderPassSettings
    [[Serializable]]
    RenderPassSettings() = default;

	[[Property]]
    AAPassSetting aa{};
	[[Property]]
    SSAOPassSetting ssao{};
	[[Property]]
    ShadowMapPassSetting shadow{};
	[[Property]]
    DeferredPassSetting deferred{};
	[[Property]]
    BloomPassSetting bloom{};
	[[Property]]
    SSGIPassSetting ssgi{};
	[[Property]]
    VignettePassSetting vignette{};
	[[Property]]
    ColorGradingPassSetting colorGrading{};
	[[Property]]
    ToneMapPassSetting toneMap{};
};

