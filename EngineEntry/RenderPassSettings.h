#pragma once
#include "Core.Minimal.h"
#include "ReflectionMecro.h"
#include "../RenderEngine/ToneMapPass.h"

struct AAPassSetting
{
    ReflectionField(AAPassSetting)
    {
        PropertyField
        ({
            meta_property(isApply)
            meta_property(bias)
            meta_property(biasMin)
            meta_property(spanMax)
        });
        FieldEnd(AAPassSetting, PropertyOnly);
    }
    [[Serializable]]
    AAPassSetting() = default;

    bool isApply{ true };
    float bias{ 0.688f };
    float biasMin{ 0.021f };
    float spanMax{ 8.0f };
};

struct SSAOPassSetting
{
    ReflectionField(SSAOPassSetting)
    {
        PropertyField
        ({
            meta_property(radius)
            meta_property(thickness)
        });
        FieldEnd(SSAOPassSetting, PropertyOnly);
    }
    [[Serializable]]
    SSAOPassSetting() = default;

    float radius{ 0.1f };
    float thickness{ 0.1f };
};

struct ShadowMapPassSetting
{
    ReflectionField(ShadowMapPassSetting)
    {
        PropertyField
        ({
            meta_property(useCascade)
            meta_property(isCloudOn)
            meta_property(cloudSize)
            meta_property(cloudDirection)
            meta_property(cloudMoveSpeed)
            meta_property(epsilon)
        });
        FieldEnd(ShadowMapPassSetting, PropertyOnly);
    }
    [[Serializable]]
    ShadowMapPassSetting() = default;

    bool useCascade{ true };
    bool isCloudOn{ true };
    Mathf::Vector2 cloudSize{ 4.f, 4.f };
    Mathf::Vector2 cloudDirection{ 1.f, 1.f };
    float cloudMoveSpeed{ 0.0003f };
    float epsilon{ 0.001f };
};

struct DeferredPassSetting
{
    ReflectionField(DeferredPassSetting)
    {
        PropertyField
        ({
            meta_property(useAmbientOcclusion)
            meta_property(useEnvironmentMap)
            meta_property(useLightWithShadows)
            meta_property(envMapIntensity)
        });
        FieldEnd(DeferredPassSetting, PropertyOnly);
    }
    [[Serializable]]
    DeferredPassSetting() = default;

    bool useAmbientOcclusion{ true };
    bool useEnvironmentMap{ true };
    bool useLightWithShadows{ true };
    float envMapIntensity{ 1.f };
};

struct PostProcessingPassSetting
{
    ReflectionField(PostProcessingPassSetting)
    {
        PropertyField
        ({
            meta_property(applyBloom)
            meta_property(threshold)
            meta_property(knee)
            meta_property(coefficient)
        });
        FieldEnd(PostProcessingPassSetting, PropertyOnly);
    }
    [[Serializable]]
    PostProcessingPassSetting() = default;

    bool applyBloom{ true };
    float threshold{ 0.3f };
    float knee{ 0.5f };
    float coefficient{ 0.3f };
};

struct SSGIPassSetting
{
    ReflectionField(SSGIPassSetting)
    {
        PropertyField
        ({
            meta_property(isOn)
            meta_property(useOnlySSGI)
            meta_property(useDualFilteringStep)
            meta_property(radius)
            meta_property(thickness)
            meta_property(intensity)
            meta_property(ssratio)
        });
        FieldEnd(SSGIPassSetting, PropertyOnly);
    }
    [[Serializable]]
    SSGIPassSetting() = default;

    bool isOn{ true };
    bool useOnlySSGI{ false };
    int useDualFilteringStep{ 2 };
    float radius{ 4.f };
    float thickness{ 0.5f };
    float intensity{ 1.f };
    int ssratio{ 4 };
};

struct VignettePassSetting
{
    ReflectionField(VignettePassSetting)
    {
        PropertyField
        ({
            meta_property(isOn)
            meta_property(radius)
            meta_property(softness)
        });
        FieldEnd(VignettePassSetting, PropertyOnly);
    }
    [[Serializable]]
    VignettePassSetting() = default;

    bool isOn{ true };
    float radius{ 0.75f };
    float softness{ 0.5f };
};

struct ColorGradingPassSetting
{
    ReflectionField(ColorGradingPassSetting)
    {
        PropertyField
        ({
            meta_property(isOn)
            meta_property(lerp)
        });
        FieldEnd(ColorGradingPassSetting, PropertyOnly);
    }
    [[Serializable]]
    ColorGradingPassSetting() = default;

    bool isOn{ true };
    float lerp{ 0.f };
};

struct ToneMapPassSetting
{
    ReflectionField(ToneMapPassSetting)
    {
        PropertyField
        ({
            meta_property(isAbleAutoExposure)
            meta_property(isAbleToneMap)
            meta_property(fNumber)
            meta_property(shutterTime)
            meta_property(ISO)
            meta_property(exposureCompensation)
            meta_property(speedBrightness)
            meta_property(speedDarkness)
            meta_property(toneMapType)
            meta_property(filmSlope)
            meta_property(filmToe)
            meta_property(filmShoulder)
            meta_property(filmBlackClip)
            meta_property(filmWhiteClip)
            meta_property(toneMapExposure)
        });
        FieldEnd(ToneMapPassSetting, PropertyOnly);
    }
    [[Serializable]]
    ToneMapPassSetting() = default;

    bool isAbleAutoExposure{ true };
    bool isAbleToneMap{ true };
    float fNumber{ 8.f };
    float shutterTime{ 16.f };
    float ISO{ 100.f };
    float exposureCompensation{ 0.f };
    float speedBrightness{ 1.5f };
    float speedDarkness{ 0.7f };
    ToneMapType toneMapType{ ToneMapType::ACES };
    float filmSlope{ 0.88f };
    float filmToe{ 0.55f };
    float filmShoulder{ 0.26f };
    float filmBlackClip{ 0.f };
    float filmWhiteClip{ 0.04f };
    float toneMapExposure{ 1.f };
};

struct RenderPassSettings
{
    ReflectionField(RenderPassSettings)
    {
        PropertyField
        ({
            meta_property(aa)
            meta_property(ssao)
            meta_property(shadow)
            meta_property(deferred)
            meta_property(post)
            meta_property(ssgi)
            meta_property(vignette)
            meta_property(colorGrading)
            meta_property(toneMap)
        });
        FieldEnd(RenderPassSettings, PropertyOnly);
    }
    [[Serializable]]
    RenderPassSettings() = default;

    AAPassSetting aa{};
    SSAOPassSetting ssao{};
    ShadowMapPassSetting shadow{};
    DeferredPassSetting deferred{};
    PostProcessingPassSetting post{};
    SSGIPassSetting ssgi{};
    VignettePassSetting vignette{};
    ColorGradingPassSetting colorGrading{};
    ToneMapPassSetting toneMap{};
};

