#pragma once
// PHASE 18 CT5 - 등록 정본 (구 RegisterReflect.def 승계).
// 이 명시 리스트가 등록의 유일한 정본이다. 타입 추가 시 include와
// REFLECT_TYPE_LIST 항목을 같이 늘린다 - 누락은 K1-b 기동 검사가 잡는다.
//
// CT6-c: 목록을 X-매크로로 공유화 - 런타임 등록(여기)과 에디터 typed
// Draw 등록(InspectorWindow.cpp)이 같은 목록을 소비한다. 두 벌 유지 금지.
#include "Reflection.hpp"
#include "ReflectionTypedYml.h" // CT6-a: typed 직렬화 썽크 등록
#include "MeshRenderer.h"
#include "BoxColliderComponent.h"
#include "LightMapping.h"
#include "ShadowMapPassSetting.h"
#include "BLackBoardValue.h"
#include "AssetEntry.h"
#include "Animation.h"
#include "DecalComponent.h"
#include "TerrainCollider.h"
#include "CameraComponent.h"
#include "SpriteRenderer.h"
#include "AssetBundle.h"
#include "BloomSetting.h"
#include "AAPassSetting.h"
#include "MaterialFlowInformation.h"
#include "SSGIPassSetting.h"
#include "Camera.h"
#include "RigidBodyComponent.h"
#include "ColorGradingPassSetting.h"
#include "MeshCollider.h"
#include "RagdollComponent.h"
#include "FoliageComponent.h"
#include "BitMaskPassSetting.h"
#include "DeferredPassSetting.h"
#include "FoliageInstance.h"
#include "SoundComponent.h"
#include "Scene.h"
#include "VolumeProfile.h"
#include "FoliageType.h"
#include "Navigation.h"
#include "RenderPassSettings.h"
#include "StateMachineComponent.h"
#include "AniTransition.h"
#include "SSAOPassSetting.h"
#include "ToneMapPassSetting.h"
#include "VolumetricFogPassSetting.h"
#include "VignettePassSetting.h"
#include "Prefab.h"
#include "PlayerInput.h"
#include "KeyFrameEvent.h"
#include "Component.h"
#include "Material.h"
#include "MaterialInfomation.h"
#include "Mesh.h"
#include "InvalidScriptComponent.h"
#include "Skeleton.h"
#include "ActionMap.h"
#include "AnimationController.h"
#include "AnimationState.h"
#include "Animator.h"
#include "AvatarMask.h"
#include "BehaviorTreeComponent.h"
#include "BoneComponent.h"
#include "BoneMask.h"
#include "BTBuildGraph.h"
#include "SphereColliderComponent.h"
#include "PrefabOverride.h"
#include "Canvas.h"
#include "BTBuildNode.h"
#include "CharacterControllerComponent.h"
#include "CapsuleColliderComponent.h"
#include "ImageComponent.h"
#include "ConditionParameter.h"
#include "SpriteSheetComponent.h"
#include "LightComponent.h"
#include "CurvePoint.h"
#include "GameObject.h"
#include "InputAction.h"
#include "Object.h"
#include "RectTransformComponent.h"
#include "ScriptComponent.h"
#include "Terrain.h"
#include "TextComponent.h"
#include "TransCondition.h"
#include "Transform.h"
#include "UIButton.h"
#include "VolumeComponent.h"

#define REFLECT_TYPE_LIST(X) \
    X(MeshRenderer) \
    X(BoxColliderComponent) \
    X(LightMapping) \
    X(ShadowMapPassSetting) \
    X(AAPassSetting) \
    X(ActionMap) \
    X(AniTransition) \
    X(Animation) \
    X(AnimationController) \
    X(AnimationState) \
    X(Animator) \
    X(AssetBundle) \
    X(AssetEntry) \
    X(AvatarMask) \
    X(BTBuildGraph) \
    X(BTBuildNode) \
    X(BehaviorTreeComponent) \
    X(BitMaskPassSetting) \
    X(BlackBoardValue) \
    X(BloomPassSetting) \
    X(BoneComponent) \
    X(BoneMask) \
    X(Camera) \
    X(CameraComponent) \
    X(Canvas) \
    X(CapsuleColliderComponent) \
    X(CharacterControllerComponent) \
    X(ColorGradingPassSetting) \
    X(Component) \
    X(ConditionParameter) \
    X(CurvePoint) \
    X(DecalComponent) \
    X(DeferredPassSetting) \
    X(FoliageComponent) \
    X(FoliageInstance) \
    X(FoliageType) \
    X(GameObject) \
    X(ImageComponent) \
    X(InputAction) \
    X(InvalidScriptComponent) \
    X(KeyFrameEvent) \
    X(LightComponent) \
    X(Material) \
    X(MaterialFlowInformation) \
    X(MaterialInfomation) \
    X(Mesh) \
    X(MeshColliderComponent) \
    X(Navigation) \
    X(Object) \
    X(PlayerInputComponent) \
    X(Prefab) \
    X(PrefabOverride) \
    X(RagdollComponent) \
    X(RectTransformComponent) \
    X(RenderPassSettings) \
    X(RigidBodyComponent) \
    X(SSAOPassSetting) \
    X(SSGIPassSetting) \
    X(Scene) \
    X(ScriptComponent) \
    X(Skeleton) \
    X(SoundComponent) \
    X(SphereColliderComponent) \
    X(SpriteRenderer) \
    X(SpriteSheetComponent) \
    X(StateMachineComponent) \
    X(TerrainColliderComponent) \
    X(TerrainComponent) \
    X(TextComponent) \
    X(ToneMapPassSetting) \
    X(TransCondition) \
    X(Transform) \
    X(UIButton) \
    X(VignettePassSetting) \
    X(VolumeComponent) \
    X(VolumeProfile) \
    X(VolumetricFogPassSetting) \

inline void RegisterReflectManual()
{
#define REFLECT_REGISTER_ONE(T) \
    Meta::Register<T>(); \
    Meta::Typed::RegisterOps<T>();

    REFLECT_TYPE_LIST(REFLECT_REGISTER_ONE)
#undef REFLECT_REGISTER_ONE
}
