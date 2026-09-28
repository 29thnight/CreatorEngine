// reflgen 전환 동등성 증명 — Tools/migration/reflgen_codemod.py 가 옮기기 전의 레시피에서 썼다.
//
// 옮긴 타입마다 다리(ReflgenBridge.h)가 만든 엔진 스키마가 옛 레시피와 필드 이름·순서·속성 타입·속성 값·
// 메서드·파라미터 이름까지 같은지 컴파일 때 단정한다. 엔진 소비자는 스키마 타입에 대한 template 이라 같으면
// 동작도 같다. 이 표는 옛 레시피의 기록이다 — 필드를 더하거나 빼면 여기 줄도 고친다.
#include "ReflgenParity.h"
#include "AssetBundle.h"
#include "AssetEntry.h"
#include "Camera.h"
#include "Interfaces/BitMaskPassSetting.h"
#include "Interfaces/BloomSetting.h"
#include "Interfaces/ColorGradingPassSetting.h"
#include "Interfaces/DeferredPassSetting.h"
#include "Interfaces/FoliageInstance.h"
#include "Interfaces/FoliageType.h"
#include "Interfaces/Navigation.h"
#include "Interfaces/RenderPassSettings.h"
#include "Interfaces/SSAOPassSetting.h"
#include "Interfaces/SSGIPassSetting.h"
#include "Interfaces/ToneMapPassSetting.h"
#include "Interfaces/VignettePassSetting.h"
#include "Interfaces/VolumetricFogPassSetting.h"
#include "KeyFrameEvent.h"
#include "Material.h"
#include "MaterialFlowInformation.h"
#include "MaterialInfomation.h"
#include "MaterialPropertyValue.h"
#include "Mesh.h"
#include "VolumeProfile.h"

namespace
{
    static_assert(parity::matches<AssetEntry>(
        parity::field("assetTypeID"),
        parity::field("assetName")));
    static_assert(parity::matches<AssetBundle>(
        parity::field("name"),
        parity::field("path"),
        parity::field("assets")));
    static_assert(parity::matches<Camera>(
        parity::field("rotate"),
        parity::field("m_nearPlane"),
        parity::field("m_farPlane"),
        parity::field("m_fov")));
    static_assert(parity::matches<KeyFrameEvent>(
        parity::field("m_eventName"),
        parity::field("m_scriptName"),
        parity::field("m_funName"),
        parity::field("key"),
        parity::field("frameKey")));
    static_assert(parity::matches<MaterialInfomation>(
        parity::field("m_baseColor"),
        parity::field("m_metallic"),
        parity::field("m_roughness"),
        parity::field("m_IOR")));
    static_assert(parity::matches<MaterialFlowInformation>(
        parity::field("m_windVector"),
        parity::field("m_uvScroll")));
    static_assert(parity::matches<MaterialPropertyValue>(
        parity::field("m_name"),
        parity::field("m_numericValue"),
        parity::field("m_integerValue"),
        parity::field("m_boolValue"),
        parity::field("m_textureGuid"),
        parity::field("m_textureUvSet"),
        parity::field("m_textureUvOffset"),
        parity::field("m_textureUvScale"),
        parity::field("m_textureUvRotation")));
    static_assert(parity::matches<Material>(
        parity::field("m_name"),
        parity::field("m_baseColorTexName"),
        parity::field("m_normalTexName"),
        parity::field("m_ORM_TexName"),
        parity::field("m_AO_TexName"),
        parity::field("m_EmissiveTexName"),
        parity::field("m_materialInfo"),
        parity::field("m_flowInfo"),
        parity::field("m_shaderMetaGuid"),
        parity::field("m_propertyValues"),
        parity::field("m_keywordSelections"),
        parity::field("m_fileGuid"),
        parity::field("m_renderingMode"),
        parity::field("m_doubleSided")));
    static_assert(parity::matches<Mesh>(
        parity::field("m_name"),
        parity::field("m_materialIndex"),
        parity::field("m_LODThresholds")));
    static_assert(parity::matches<DeferredPassSetting>(
        parity::field("useAmbientOcclusion"),
        parity::field("useEnvironmentMap"),
        parity::field("useLightWithShadows"),
        parity::field("envMapIntensity")));
    static_assert(parity::matches<BloomPassSetting>(
        parity::field("applyBloom"),
        parity::field("threshold"),
        parity::field("knee"),
        parity::field("coefficient"),
        parity::field("blurRadius"),
        parity::field("blurSigma")));
    static_assert(parity::matches<SSGIPassSetting>(
        parity::field("isOn"),
        parity::field("useOnlySSGI"),
        parity::field("useDualFilteringStep"),
        parity::field("radius"),
        parity::field("thickness"),
        parity::field("intensity"),
        parity::field("ssratio")));
    static_assert(parity::matches<VignettePassSetting>(
        parity::field("isOn"),
        parity::field("radius"),
        parity::field("softness")));
    static_assert(parity::matches<ColorGradingPassSetting>(
        parity::field("isOn"),
        parity::field("lerp"),
        parity::field("textureFilePath")));
    static_assert(parity::matches<ToneMapPassSetting>(
        parity::field("isAbleAutoExposure"),
        parity::field("isAbleToneMap"),
        parity::field("fNumber"),
        parity::field("shutterTime"),
        parity::field("ISO"),
        parity::field("exposureCompensation"),
        parity::field("speedBrightness"),
        parity::field("speedDarkness"),
        parity::field("toneMapType"),
        parity::field("filmSlope"),
        parity::field("filmToe"),
        parity::field("filmShoulder"),
        parity::field("filmBlackClip"),
        parity::field("filmWhiteClip"),
        parity::field("toneMapExposure")));
    static_assert(parity::matches<SSAOPassSetting>(
        parity::field("radius"),
        parity::field("thickness")));
    static_assert(parity::matches<VolumetricFogPassSetting>(
        parity::field("mAnisotropy"),
        parity::field("mDensity"),
        parity::field("mStrength"),
        parity::field("mThicknessFactor"),
        parity::field("mBlendingWithSceneColorFactor"),
        parity::field("mPreviousFrameBlendFactor"),
        parity::field("mCustomNearPlane"),
        parity::field("mCustomFarPlane"),
        parity::field("isOn")));
    static_assert(parity::matches<BitMaskPassSetting>(
        parity::field("isOn"),
        parity::field("blurOutline"),
        parity::field("outlineVelocity"),
        parity::field("m_color1"),
        parity::field("m_color2"),
        parity::field("m_color3"),
        parity::field("m_color4"),
        parity::field("m_color5"),
        parity::field("m_color6"),
        parity::field("m_color7"),
        parity::field("m_color8")));
    static_assert(parity::matches<RenderPassSettings>(
        parity::field("aa"),
        parity::field("ssao"),
        parity::field("shadow"),
        parity::field("deferred"),
        parity::field("bloom"),
        parity::field("ssgi"),
        parity::field("vignette"),
        parity::field("colorGrading"),
        parity::field("toneMap"),
        parity::field("volumetricFog"),
        parity::field("bitMask"),
        parity::field("skyboxTextureName"),
        parity::field("m_isSkyboxEnabled"),
        parity::field("m_windDirection"),
        parity::field("m_windStrength"),
        parity::field("m_windSpeed"),
        parity::field("m_windWaveFrequency")));
    static_assert(parity::matches<VolumeProfile>(
        parity::field("settings")));
    static_assert(parity::matches<FoliageType>(
        parity::field("m_castShadow"),
        parity::field("m_isShadowRecive"),
        parity::field("m_modelName")));
    static_assert(parity::matches<FoliageInstance>(
        parity::field("m_position"),
        parity::field("m_rotation"),
        parity::field("m_scale"),
        parity::field("m_foliageTypeID")));
    static_assert(parity::matches<Navigation>(
        parity::field("mode"),
        parity::field("parentHops"),
        parity::field("childOrdinals")));
}
