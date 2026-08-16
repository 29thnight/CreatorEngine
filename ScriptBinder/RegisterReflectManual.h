#pragma once
// PHASE 18 CT5 - 등록 정본 (구 RegisterReflect.def 승계).
// 생성기의 어노테이션 스캔이 아니라 이 명시 리스트가 등록의 유일한 정본이다.
// 타입 추가 시 여기 두 줄(include + 등록)을 같이 늘린다 - 누락은 K1-b
// 기동 검사(씬에 있는데 등록 안 됨)가 잡는다. def는 CT7에서 은퇴한다.
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

inline void RegisterReflectManual()
{
    AUTO_REGISTER_CLASS(MeshRenderer);
    AUTO_REGISTER_CLASS(BoxColliderComponent);
    AUTO_REGISTER_CLASS(LightMapping);
    AUTO_REGISTER_CLASS(ShadowMapPassSetting);
    AUTO_REGISTER_CLASS(AAPassSetting);
    AUTO_REGISTER_CLASS(ActionMap);
    AUTO_REGISTER_CLASS(AniTransition);
    AUTO_REGISTER_CLASS(Animation);
    AUTO_REGISTER_CLASS(AnimationController);
    AUTO_REGISTER_CLASS(AnimationState);
    AUTO_REGISTER_CLASS(Animator);
    AUTO_REGISTER_CLASS(AssetBundle);
    AUTO_REGISTER_CLASS(AssetEntry);
    AUTO_REGISTER_CLASS(AvatarMask);
    AUTO_REGISTER_CLASS(BTBuildGraph);
    AUTO_REGISTER_CLASS(BTBuildNode);
    AUTO_REGISTER_CLASS(BehaviorTreeComponent);
    AUTO_REGISTER_CLASS(BitMaskPassSetting);
    AUTO_REGISTER_CLASS(BlackBoardValue);
    AUTO_REGISTER_CLASS(BloomPassSetting);
    AUTO_REGISTER_CLASS(BoneMask);
    AUTO_REGISTER_CLASS(Camera);
    AUTO_REGISTER_CLASS(CameraComponent);
    AUTO_REGISTER_CLASS(Canvas);
    AUTO_REGISTER_CLASS(CapsuleColliderComponent);
    AUTO_REGISTER_CLASS(CharacterControllerComponent);
    AUTO_REGISTER_CLASS(ColorGradingPassSetting);
    AUTO_REGISTER_CLASS(Component);
    AUTO_REGISTER_CLASS(ConditionParameter);
    AUTO_REGISTER_CLASS(CurvePoint);
    AUTO_REGISTER_CLASS(DecalComponent);
    AUTO_REGISTER_CLASS(DeferredPassSetting);
    AUTO_REGISTER_CLASS(FoliageComponent);
    AUTO_REGISTER_CLASS(FoliageInstance);
    AUTO_REGISTER_CLASS(FoliageType);
    AUTO_REGISTER_CLASS(GameObject);
    AUTO_REGISTER_CLASS(ImageComponent);
    AUTO_REGISTER_CLASS(InputAction);
    AUTO_REGISTER_CLASS(InvalidScriptComponent);
    AUTO_REGISTER_CLASS(KeyFrameEvent);
    AUTO_REGISTER_CLASS(LightComponent);
    AUTO_REGISTER_CLASS(Material);
    AUTO_REGISTER_CLASS(MaterialFlowInformation);
    AUTO_REGISTER_CLASS(MaterialInfomation);
    AUTO_REGISTER_CLASS(Mesh);
    AUTO_REGISTER_CLASS(MeshColliderComponent);
    AUTO_REGISTER_CLASS(Navigation);
    AUTO_REGISTER_CLASS(Object);
    AUTO_REGISTER_CLASS(PlayerInputComponent);
    AUTO_REGISTER_CLASS(Prefab);
    AUTO_REGISTER_CLASS(PrefabOverride);
    AUTO_REGISTER_CLASS(RagdollComponent);
    AUTO_REGISTER_CLASS(RectTransformComponent);
    AUTO_REGISTER_CLASS(RenderPassSettings);
    AUTO_REGISTER_CLASS(RigidBodyComponent);
    AUTO_REGISTER_CLASS(SSAOPassSetting);
    AUTO_REGISTER_CLASS(SSGIPassSetting);
    AUTO_REGISTER_CLASS(Scene);
    AUTO_REGISTER_CLASS(ScriptComponent);
    AUTO_REGISTER_CLASS(Skeleton);
    AUTO_REGISTER_CLASS(SoundComponent);
    AUTO_REGISTER_CLASS(SphereColliderComponent);
    AUTO_REGISTER_CLASS(SpriteRenderer);
    AUTO_REGISTER_CLASS(SpriteSheetComponent);
    AUTO_REGISTER_CLASS(StateMachineComponent);
    AUTO_REGISTER_CLASS(TerrainColliderComponent);
    AUTO_REGISTER_CLASS(TerrainComponent);
    AUTO_REGISTER_CLASS(TextComponent);
    AUTO_REGISTER_CLASS(ToneMapPassSetting);
    AUTO_REGISTER_CLASS(TransCondition);
    AUTO_REGISTER_CLASS(Transform);
    AUTO_REGISTER_CLASS(UIButton);
    AUTO_REGISTER_CLASS(VignettePassSetting);
    AUTO_REGISTER_CLASS(VolumeComponent);
    AUTO_REGISTER_CLASS(VolumeProfile);
    AUTO_REGISTER_CLASS(VolumetricFogPassSetting);

    // CT6-a: typed 직렬화 브리지 — 타입당 함수 포인터 2개.
    Meta::Typed::RegisterOps<MeshRenderer>();
    Meta::Typed::RegisterOps<BoxColliderComponent>();
    Meta::Typed::RegisterOps<LightMapping>();
    Meta::Typed::RegisterOps<ShadowMapPassSetting>();
    Meta::Typed::RegisterOps<AAPassSetting>();
    Meta::Typed::RegisterOps<ActionMap>();
    Meta::Typed::RegisterOps<AniTransition>();
    Meta::Typed::RegisterOps<Animation>();
    Meta::Typed::RegisterOps<AnimationController>();
    Meta::Typed::RegisterOps<AnimationState>();
    Meta::Typed::RegisterOps<Animator>();
    Meta::Typed::RegisterOps<AssetBundle>();
    Meta::Typed::RegisterOps<AssetEntry>();
    Meta::Typed::RegisterOps<AvatarMask>();
    Meta::Typed::RegisterOps<BTBuildGraph>();
    Meta::Typed::RegisterOps<BTBuildNode>();
    Meta::Typed::RegisterOps<BehaviorTreeComponent>();
    Meta::Typed::RegisterOps<BitMaskPassSetting>();
    Meta::Typed::RegisterOps<BlackBoardValue>();
    Meta::Typed::RegisterOps<BloomPassSetting>();
    Meta::Typed::RegisterOps<BoneMask>();
    Meta::Typed::RegisterOps<Camera>();
    Meta::Typed::RegisterOps<CameraComponent>();
    Meta::Typed::RegisterOps<Canvas>();
    Meta::Typed::RegisterOps<CapsuleColliderComponent>();
    Meta::Typed::RegisterOps<CharacterControllerComponent>();
    Meta::Typed::RegisterOps<ColorGradingPassSetting>();
    Meta::Typed::RegisterOps<Component>();
    Meta::Typed::RegisterOps<ConditionParameter>();
    Meta::Typed::RegisterOps<CurvePoint>();
    Meta::Typed::RegisterOps<DecalComponent>();
    Meta::Typed::RegisterOps<DeferredPassSetting>();
    Meta::Typed::RegisterOps<FoliageComponent>();
    Meta::Typed::RegisterOps<FoliageInstance>();
    Meta::Typed::RegisterOps<FoliageType>();
    Meta::Typed::RegisterOps<GameObject>();
    Meta::Typed::RegisterOps<ImageComponent>();
    Meta::Typed::RegisterOps<InputAction>();
    Meta::Typed::RegisterOps<InvalidScriptComponent>();
    Meta::Typed::RegisterOps<KeyFrameEvent>();
    Meta::Typed::RegisterOps<LightComponent>();
    Meta::Typed::RegisterOps<Material>();
    Meta::Typed::RegisterOps<MaterialFlowInformation>();
    Meta::Typed::RegisterOps<MaterialInfomation>();
    Meta::Typed::RegisterOps<Mesh>();
    Meta::Typed::RegisterOps<MeshColliderComponent>();
    Meta::Typed::RegisterOps<Navigation>();
    Meta::Typed::RegisterOps<Object>();
    Meta::Typed::RegisterOps<PlayerInputComponent>();
    Meta::Typed::RegisterOps<Prefab>();
    Meta::Typed::RegisterOps<PrefabOverride>();
    Meta::Typed::RegisterOps<RagdollComponent>();
    Meta::Typed::RegisterOps<RectTransformComponent>();
    Meta::Typed::RegisterOps<RenderPassSettings>();
    Meta::Typed::RegisterOps<RigidBodyComponent>();
    Meta::Typed::RegisterOps<SSAOPassSetting>();
    Meta::Typed::RegisterOps<SSGIPassSetting>();
    Meta::Typed::RegisterOps<Scene>();
    Meta::Typed::RegisterOps<ScriptComponent>();
    Meta::Typed::RegisterOps<Skeleton>();
    Meta::Typed::RegisterOps<SoundComponent>();
    Meta::Typed::RegisterOps<SphereColliderComponent>();
    Meta::Typed::RegisterOps<SpriteRenderer>();
    Meta::Typed::RegisterOps<SpriteSheetComponent>();
    Meta::Typed::RegisterOps<StateMachineComponent>();
    Meta::Typed::RegisterOps<TerrainColliderComponent>();
    Meta::Typed::RegisterOps<TerrainComponent>();
    Meta::Typed::RegisterOps<TextComponent>();
    Meta::Typed::RegisterOps<ToneMapPassSetting>();
    Meta::Typed::RegisterOps<TransCondition>();
    Meta::Typed::RegisterOps<Transform>();
    Meta::Typed::RegisterOps<UIButton>();
    Meta::Typed::RegisterOps<VignettePassSetting>();
    Meta::Typed::RegisterOps<VolumeComponent>();
    Meta::Typed::RegisterOps<VolumeProfile>();
    Meta::Typed::RegisterOps<VolumetricFogPassSetting>();
}
