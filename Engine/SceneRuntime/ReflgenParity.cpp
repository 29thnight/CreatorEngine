// reflgen 전환 동등성 증명 — Tools/migration/reflgen_codemod.py 가 옮기기 전의 레시피에서 썼다.
//
// 옮긴 타입마다 다리(ReflgenBridge.h)가 만든 엔진 스키마가 옛 레시피와 필드 이름·순서·속성 타입·속성 값·
// 메서드·파라미터 이름까지 같은지 컴파일 때 단정한다. 엔진 소비자는 스키마 타입에 대한 template 이라 같으면
// 동작도 같다. 이 표는 옛 레시피의 기록이다 — 필드를 더하거나 빼면 여기 줄도 고친다.
#include "ReflgenParity.h"
#include "ActionMap.h"
#include "AniTransition.h"
#include "AnimationController.h"
#include "AnimationState.h"
#include "Animator.h"
#include "AvatarMask.h"
#include "BTBuildGraph.h"
#include "BTBuildNode.h"
#include "BehaviorTreeComponent.h"
#include "BlackBoardValue.h"
#include "BoneComponent.h"
#include "BoneMask.h"
#include "BoxColliderComponent.h"
#include "CameraComponent.h"
#include "Canvas.h"
#include "CapsuleColliderComponent.h"
#include "CharacterControllerComponent.h"
#include "Component.h"
#include "ConditionParameter.h"
#include "CurvePoint.h"
#include "DecalComponent.h"
#include "Entity.h"
#include "FoliageComponent.h"
#include "ImageComponent.h"
#include "InputAction.h"
#include "InvalidScriptComponent.h"
#include "LightComponent.h"
#include "MeshCollider.h"
#include "MeshRenderer.h"
#include "Object.h"
#include "PlayerInput.h"
#include "Prefab.h"
#include "PrefabOverride.h"
#include "RagdollComponent.h"
#include "RectTransformComponent.h"
#include "RigidBodyComponent.h"
#include "Scene.h"
#include "ScriptComponent.h"
#include "SoundComponent.h"
#include "SphereColliderComponent.h"
#include "SpriteRenderer.h"
#include "SpriteSheetComponent.h"
#include "StateMachineComponent.h"
#include "Terrain.h"
#include "TerrainCollider.h"
#include "TextComponent.h"
#include "TransCondition.h"
#include "Transform.h"
#include "UIButton.h"
#include "UIComponent.h"
#include "VolumeComponent.h"

namespace
{
    static_assert(parity::matches<InputAction>(
        parity::field("actionName"),
        parity::field("m_scriptName"),
        parity::field("funName")));
    static_assert(parity::matches<ActionMap>(
        parity::field("m_name"),
        parity::field("m_actions")));
    static_assert(parity::matches<Object>(
        parity::field("m_name", meta::readonly(), meta::debugOnly()),
        parity::field("m_instanceID", meta::readonly(), meta::debugOnly()),
        parity::field("m_isEnabled", meta::hidden())));
    static_assert(parity::matches<Component>(
        parity::field("m_FileID")));
    static_assert(parity::matches<Transform>(
        parity::field("position"),
        parity::field("rotation"),
        parity::field("scale"),
        parity::field("m_parentID")));
    static_assert(parity::matches<PrefabOverride>(
        parity::field("m_componentType"),
        parity::field("m_componentSlot"),
        parity::field("m_propertyName"),
        parity::field("m_valueYaml")));
    static_assert(parity::matches<Entity>(
        parity::field("m_attachedSoketID"),
        parity::field("m_index"),
        parity::field("m_collisionType"),
        parity::field("m_prefabFileGuid"),
        parity::field("m_prefabOverrides"),
        parity::field("m_tag"),
        parity::field("m_layer"),
        parity::field("m_editorIcon", meta::hidden()),
        parity::field("m_editorLocked", meta::hidden()),
        parity::field("m_components"),
        parity::field("m_isStatic")));
    static_assert(parity::matches<ConditionParameter>(
        parity::field("name"),
        parity::field("fValue"),
        parity::field("iValue"),
        parity::field("vType"),
        parity::field("bValue"),
        parity::field("tValue")));
    static_assert(parity::matches<TransCondition>(
        parity::field("valueName"),
        parity::field("CompareParameter"),
        parity::field("cType")));
    static_assert(parity::matches<AniTransition>(
        parity::field("conditions"),
        parity::field("m_name"),
        parity::field("curStateName"),
        parity::field("nextStateName"),
        parity::field("exitTime"),
        parity::field("blendTime"),
        parity::field("hasExitTime")));
    static_assert(parity::matches<AnimationState>(
        parity::field("m_name"),
        parity::field("behaviourName"),
        parity::field("Transitions"),
        parity::field("index"),
        parity::field("AnimationIndex"),
        parity::field("animationSpeed"),
        parity::field("multiplerAnimationSpeed"),
        parity::field("animationSpeedParameterName"),
        parity::field("m_isAny"),
        parity::field("useMultipler")));
    static_assert(parity::matches<BoneMask>(
        parity::field("boneName"),
        parity::field("isEnabled"),
        parity::field("weight")));
    static_assert(parity::matches<AvatarMask>(
        parity::field("m_BoneMasks"),
        parity::field("isHumanoid"),
        parity::field("useAll"),
        parity::field("useUpper"),
        parity::field("useLower")));
    static_assert(parity::matches<AnimationController>(
        parity::field("name"),
        parity::field("m_curState"),
        parity::field("StateVec"),
        parity::field("m_anyState"),
        parity::field("m_avatarMask"),
        parity::field("useController"),
        parity::field("useMask"),
        parity::field("m_additive")));
    static_assert(parity::matches<Animator>(
        parity::field("m_AnimIndexChosen", meta::hidden()),
        parity::field("m_AnimIndex", meta::hidden()),
        parity::field("m_Motion"),
        parity::field("m_QualityRadius", meta::hidden()),
        parity::field("m_LowDetailBoneCount", meta::hidden()),
        parity::field("m_ForceFullQuality", meta::hidden()),
        parity::field("m_TwoBoneIKConstraints", meta::hidden()),
        parity::field("m_BoneTransformConstraints", meta::hidden()),
        parity::field("m_animationControllers"),
        parity::field("Parameters"),
        parity::method<&Animator::UpdateAnimation>().hideInInspector()));
    static_assert(parity::matches<BlackBoardValue>(
        parity::field("Type"),
        parity::field("BoolValue"),
        parity::field("IntValue"),
        parity::field("FloatValue"),
        parity::field("StringValue"),
        parity::field("Vec2Value"),
        parity::field("Vec3Value"),
        parity::field("Vec4Value")));
    static_assert(parity::matches<BTBuildNode>(
        parity::field("ID"),
        parity::field("Type"),
        parity::field("Name"),
        parity::field("ParentID"),
        parity::field("IsRoot"),
        parity::field("HasScript"),
        parity::field("ScriptName"),
        parity::field("Policy"),
        parity::field("Children"),
        parity::field("ChildWeights"),
        parity::field("Position")));
    static_assert(parity::matches<BTBuildGraph>(
        parity::field("NodeList")));
    static_assert(parity::matches<BehaviorTreeComponent>(
        parity::field("name"),
        parity::field("blackBoardName"),
        parity::field("m_BehaviorTreeGuid"),
        parity::field("m_BlackBoardGuid")));
    static_assert(parity::matches<BoneComponent>(
        parity::field("m_bPinned"),
        parity::method<&BoneComponent::GetResolvedBoneIndex>().readOnlyInInspector()));
    static_assert(parity::matches<Scene>(
        parity::field("m_Entities"),
        parity::field("m_buildIndex"),
        parity::field("m_sceneName"),
        parity::field("m_requiredLoadAssetsBundle")));
    static_assert(parity::matches<BoxColliderComponent>(
        parity::field("m_boxExtent"),
        parity::field("m_posOffset"),
        parity::field("m_rotOffset"),
        parity::field("staticFriction", meta::range(0.0f, 1.0f)),
        parity::field("dynamicFriction", meta::range(0.0f, 1.0f)),
        parity::field("restitution", meta::range(0.0f, 1.0f)),
        parity::field("density")));
    static_assert(parity::matches<CameraComponent>(
        parity::field("m_Camera"),
        parity::field("m_isPrimary")));
    static_assert(parity::matches<Canvas>(
        parity::field("ScaleMode"),
        parity::field("ReferenceResolution"),
        parity::field("MatchWidthOrHeight"),
        parity::field("ScaleFactor"),
        parity::field("RenderMode"),
        parity::field("PlaneDistance"),
        parity::field("CanvasOrder"),
        parity::field("CanvasName")));
    static_assert(parity::matches<CapsuleColliderComponent>(
        parity::field("m_radius"),
        parity::field("m_posOffset"),
        parity::field("m_rotOffset"),
        parity::field("m_height"),
        parity::field("staticFriction"),
        parity::field("dynamicFriction"),
        parity::field("restitution"),
        parity::field("density")));
    static_assert(parity::matches<RigidBodyComponent>(
        parity::field("m_bodyType"),
        parity::field("LinearDamping"),
        parity::field("m_mass"),
        parity::field("maxLinearVelocity"),
        parity::field("maxAngularVelocity"),
        parity::field("maxContactImpulse"),
        parity::field("maxDepenetrationVelocity"),
        parity::field("m_useGravity"),
        parity::field("m_setTrigger"),
        parity::field("m_setKinematic"),
        parity::field("m_collisionEnabled")));
    static_assert(parity::matches<CharacterControllerComponent>(
        parity::field("m_posOffset"),
        parity::field("m_radius"),
        parity::field("m_rotOffset"),
        parity::field("m_height"),
        parity::field("maxSpeed"),
        parity::field("acceleration"),
        parity::field("staticFriction"),
        parity::field("dynamicFriction"),
        parity::field("jumpSpeed"),
        parity::field("gravityWeight"),
        parity::field("m_fBaseSpeed"),
        parity::field("m_fFinalMultiplierSpeed"),
        parity::field("m_rotationSpeed")));
    static_assert(parity::matches<CurvePoint>(
        parity::field("distance"),
        parity::field("gain")));
    static_assert(parity::matches<DecalComponent>(
        parity::field("m_diffusefileName"),
        parity::field("m_normalFileName"),
        parity::field("m_ormFileName"),
        parity::field("m_decalTexture"),
        parity::field("m_normalTexture"),
        parity::field("m_occluroughmetalTexture"),
        parity::field("sliceX"),
        parity::field("sliceY"),
        parity::field("sliceNumber"),
        parity::field("slicePerSeconds"),
        parity::field("useAnimation"),
        parity::field("isLoop")));
    static_assert(parity::matches<FoliageComponent>(
        parity::field("m_foliageAssetGuid")));
    static_assert(parity::matches<UIComponent>(
        parity::field("_layerorder"),
        parity::field("uiEffects"),
        parity::field("m_ownerCanvasName"),
        parity::field("navigations")));
    static_assert(parity::matches<ImageComponent>(
        parity::field("texturePaths"),
        parity::field("color"),
        parity::field("curindex"),
        parity::field("rotate"),
        parity::field("origin"),
        parity::field("unionScale"),
        parity::field("clipPercent"),
        parity::field("clipDirection"),
        parity::field("useNativeTextureSize"),
        parity::method<&ImageComponent::UpdateTexture>().hideInInspector()));
    static_assert(parity::matches<InvalidScriptComponent>(
        parity::field("m_errorMessage")));
    static_assert(parity::matches<LightComponent>(
        parity::field("m_color"),
        parity::field("m_lightIndex"),
        parity::field("m_constantAttenuation"),
        parity::field("m_linearAttenuation"),
        parity::field("m_quadraticAttenuation"),
        parity::field("m_spotLightAngle"),
        parity::field("m_intencity"),
        parity::field("m_range"),
        parity::field("m_lightType"),
        parity::field("m_lightStatus")));
    static_assert(parity::matches<MeshColliderComponent>(
        parity::field("m_posOffset"),
        parity::field("m_rotOffset")));
    static_assert(parity::matches<MeshRenderer>(
        parity::field("m_Material"),
        parity::field("m_LightMapping"),
        parity::field("m_bitflag"),
        parity::field("m_isSkinnedMesh"),
        parity::field("m_shadowRecive"),
        parity::field("m_shadowCast"),
        parity::field("m_isEnableLOD"),
        parity::field("m_modelGuid"),
        parity::field("m_meshAssetId")));
    static_assert(parity::matches<PlayerInputComponent>(
        parity::field("m_actionMapName"),
        parity::field("m_scriptName"),
        parity::field("controllerIndex")));
    static_assert(parity::matches<Prefab>(
        parity::field("m_fileGuid")));
    static_assert(parity::matches<RagdollComponent>(
        parity::field("m_bIsRagdoll")));
    static_assert(parity::matches<RectTransformComponent>(
        parity::field("m_anchorMin"),
        parity::field("m_anchorMax"),
        parity::field("m_anchoredPosition"),
        parity::field("m_sizeDelta"),
        parity::field("m_pivot")));
    static_assert(parity::matches<ScriptComponent>(
        parity::field("m_scriptType"),
        parity::field("m_fieldData")));
    static_assert(parity::matches<SoundComponent>(
        parity::field("clipKey"),
        parity::field("bus"),
        parity::field("volume"),
        parity::field("pitch"),
        parity::field("priority"),
        parity::field("spatialBlend"),
        parity::field("minDistance"),
        parity::field("maxDistance"),
        parity::field("reverbLevel"),
        parity::field("reverbIndex"),
        parity::field("rolloff"),
        parity::field("velocity"),
        parity::field("localRolloffCurve"),
        parity::field("loop"),
        parity::field("playOnStart"),
        parity::field("spatial"),
        parity::field("useReverbSend"),
        parity::method<&SoundComponent::Play>(),
        parity::method<&SoundComponent::Stop>(),
        parity::method<&SoundComponent::Pause>("pause"),
        parity::method<&SoundComponent::IsPlaying>().readOnlyInInspector(),
        parity::method<&SoundComponent::PlayOneShot>()));
    static_assert(parity::matches<SphereColliderComponent>(
        parity::field("radius"),
        parity::field("staticFriction"),
        parity::field("dynamicFriction"),
        parity::field("restitution"),
        parity::field("density"),
        parity::field("m_posOffset"),
        parity::field("m_rotOffset")));
    static_assert(parity::matches<SpriteRenderer>(
        parity::field("m_SpritePath"),
        parity::field("m_orderInLayer"),
        parity::field("m_billboardAxis"),
        parity::field("m_billboardType"),
        parity::field("m_enableDepth")));
    static_assert(parity::matches<SpriteSheetComponent>(
        parity::field("m_spriteSheetPath"),
        parity::field("m_frameDuration"),
        parity::field("clipPercent"),
        parity::field("clipDirection"),
        parity::field("m_isLoop"),
        parity::field("m_isPreview")));
    static_assert(parity::matches<StateMachineComponent>(
        parity::field("name")));
    static_assert(parity::matches<TerrainColliderComponent>(
        parity::field("m_posOffset")));
    static_assert(parity::matches<TerrainComponent>(
        parity::field("m_width"),
        parity::field("m_height"),
        parity::field("m_trrainAssetGuid")));
    static_assert(parity::matches<TextComponent>(
        parity::field("fontPath"),
        parity::field("message"),
        parity::field("relpos"),
        parity::field("color"),
        parity::field("manualRect"),
        parity::field("fontSize"),
        parity::field("horizontalAlignment"),
        parity::field("useManualRect"),
        parity::field("m_textMeasureSize")));
    static_assert(parity::matches<UIButton>(
        parity::method<&UIButton::Click>()));
    static_assert(parity::matches<VolumeComponent>(
        parity::field("m_volumeProfileName"),
        parity::field("m_volumeProfileGuid")));
    static_assert(parity::matches<TwoBoneIKConstraint>(
        parity::field("StartBone"),
        parity::field("MiddleBone"),
        parity::field("EndBone"),
        parity::field("TargetWorld"),
        parity::field("PoleWorld"),
        parity::field("Weight"),
        parity::field("Enabled"),
        parity::field("Required")));
    static_assert(parity::matches<BoneTransformConstraint>(
        parity::field("Bone"),
        parity::field("TranslationOffset"),
        parity::field("RotationOffset"),
        parity::field("ScaleMultiplier"),
        parity::field("Weight"),
        parity::field("Enabled"),
        parity::field("Required")));
}
