module;
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/color.hpp>

export module Creator.Core.RenderPassSettings;
import std;

export {
struct [[reflgen::reflect]] AAPassSetting
{
   public:
    AAPassSetting() = default;
   ~AAPassSetting() = default;

    bool isApply{ true };
    float bias{ 0.688f };
    float biasMin{ 0.021f };
    float spanMax{ 8.0f };
};

struct [[reflgen::reflect]] SSAOPassSetting
{
   public:
    SSAOPassSetting() = default;

    float radius{ 0.1f };
    float thickness{ 0.1f };
};

struct [[reflgen::reflect]] ShadowMapPassSetting
{
   public:

    ShadowMapPassSetting() = default;

    bool useCascade{ true };
    bool isCloudOn{ true };
    math::vector2 cloudSize{ 7.f, 7.f };
    math::vector2 cloudDirection{ 1.f, 1.f };
    float cloudMoveSpeed{ 0.00006f };
	float cloudAlpha{ 1.f };
    float epsilon{ 0.001f };
};

struct [[reflgen::reflect]] DeferredPassSetting
{
   public:
    DeferredPassSetting() = default;

    bool useAmbientOcclusion{ true };
    bool useEnvironmentMap{ true };
    bool useLightWithShadows{ true };
    float envMapIntensity{ 1.f };
};

struct [[reflgen::reflect]] BloomPassSetting
{
   public:
    BloomPassSetting() = default;

    bool applyBloom{ true };
    float threshold{ 5.f };
    float knee{ 0.3f };
    float coefficient{ 0.05f };
    int blurRadius{ 3 };
    float blurSigma{ 2.f };
};

struct [[reflgen::reflect]] SSGIPassSetting
{
   public:
    SSGIPassSetting() = default;

    bool isOn{ true };
    bool useOnlySSGI{ false };
    int useDualFilteringStep{ 2 };
    float radius{ 4.f };
    float thickness{ 0.5f };
    float intensity{ 1.f };
    int ssratio{ 4 };
};

struct [[reflgen::reflect]] VignettePassSetting
{
   public:
    VignettePassSetting() = default;

    bool isOn{ true };
    float radius{ 0.75f };
    float softness{ 0.5f };
};

struct [[reflgen::reflect]] ColorGradingPassSetting
{
   public:
    ColorGradingPassSetting() = default;

    bool isOn{ true };
    float lerp{ 0.f };
    std::string textureFilePath{"None"};
};

struct [[reflgen::reflect]] ToneMapPassSetting
{
   public:
    ToneMapPassSetting() = default;

    bool isAbleAutoExposure{ true };
    bool isAbleToneMap{ true };
    float fNumber{ 4.85f };
    float shutterTime{ 16.f };
    float ISO{ 75.f };
    float exposureCompensation{ 0.2f };
    float speedBrightness{ 0.002f };
    float speedDarkness{ 0.002f };
    int toneMapType{ 1 };
    float filmSlope{ 0.88f };
    float filmToe{ 0.55f };
    float filmShoulder{ 0.26f };
    float filmBlackClip{ 0.f };
    float filmWhiteClip{ 0.04f };
    float toneMapExposure{ 1.f };
};

struct [[reflgen::reflect]] VolumetricFogPassSetting
{
   public:
	VolumetricFogPassSetting() = default;

	float mAnisotropy = 0.109f;
	float mDensity = 0.101f;
	float mStrength = 2.0f;
	float mThicknessFactor = 0.01f;
	float mBlendingWithSceneColorFactor = 0.851f;
	float mPreviousFrameBlendFactor = 0.95f;

	float mCustomNearPlane = 0.5f;
	float mCustomFarPlane = 1000.0f;

	bool isOn{ true };
};

struct [[reflgen::reflect]] BitMaskPassSetting
{
   public:
	BitMaskPassSetting() = default;

	bool isOn = true;
	bool blurOutline = false;
	float outlineVelocity = 3.f;
    math::color m_color1 = { 1.f, 0.f, 0.f, 3.f }; // red
    math::color m_color2 = { 0.f, 1.f, 0.f, 3.f }; // Green
    math::color m_color3 = { 0.f, 0.f, 1.f, 3.f }; // Blue
    math::color m_color4 = { 1.f, 1.f, 0.f, 3.f }; // Yellow
    math::color m_color5 = { 1.f, 0.f, 1.f, 3.f }; // Magenta
    math::color m_color6 = { 0.f, 1.f, 1.f, 3.f }; // Cyan
    math::color m_color7 = { 0.5f, 0.5f, 0.5f, 3.f }; // Gray
    math::color m_color8 = { 0.2f, 0.2f, 0.2f, 3.f }; // Dark Gray}
};

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

struct [[reflgen::reflect]] SceneRenderProfile
{
  public:
    SceneRenderProfile() = default;

    RenderPassSettings settings{};
};

}
