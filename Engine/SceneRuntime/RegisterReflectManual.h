#pragma once
// PHASE 18 CT5 - 등록 정본 (구 RegisterReflect.def 승계).
//
// reflgen 도입 P5: 런타임 타입 표(서술자)의 등록은 생성된 등록 함수가 한다 — [[reflgen::reflect]] 를 단 클래스가
// 모듈마다 빠짐없이 들어간다(reflgen::generated::register_RenderEngine·register_SceneRuntime, Editor 모듈은 에디터
// 초기화). 아래 명시 리스트(REFLECT_TYPE_LIST)는 typed 직렬화 썽크(TypeOps)와 에디터 typed Draw 등록
// (InspectorWindow.cpp)이 소비한다. 타입 추가 시 include와 항목을 같이 늘린다 - 누락은 K1-b 기동 검사가 잡는다.
//
// CT6-c: 목록을 X-매크로로 공유화 - 런타임 등록(여기)과 에디터 typed
// Draw 등록(InspectorWindow.cpp)이 같은 목록을 소비한다. 두 벌 유지 금지.
#include "Reflection.hpp"
#include "ReflectionTypedYml.h" // CT6-a: typed 직렬화 썽크 등록
#include "ReflgenRuntime.h"     // 런타임 타입 등록소(Meta::Types)
#include "MeshRenderer.h"
#include "BoxColliderComponent.h"
#include "LightMapping.h"
#include "ShadowMapPassSetting.h"
#include "BlackBoardValue.h"
#include "AssetEntry.h"
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
#include "Entity.h"
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
    X(Entity) \
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

// 서술자의 id 와 엔진 typeID(IObject::GetTypeID·YAML 헤더)는 같은 값이어야 한다 — 등록소를 typeID 로 찾는다
// (Meta::Find(HashedGuid)). 둘 다 한정 이름의 FNV-1a 64 라 이름 표기가 같으면 같다. 표기가 갈리는 타입이 생기면
// 여기서 멈춘다(조용히 "미등록"이 되지 않게).
// 서술의 이름(생성기가 적는 한정 이름 — 등록소의 이름 키, YAML 헤더)도 엔진 이름과 같아야 한다.
// [[reflgen::reflect("다른 이름")]] 을 달면 여기서 멈춘다.
#define REFLECT_IDENTITY_ONE(T) \
    static_assert(reflgen::type_id_of<T>().value() == TypeTrait::MakeTypeID<T>().m_ID_Data, \
        #T ": reflgen type_id 와 엔진 typeID 가 다르다 - 타입 이름 표기가 갈렸다"); \
    static_assert(reflgen::schema_of<T>.name == TypeTrait::type_name<T>(), \
        #T ": 서술의 이름이 엔진 타입 이름과 다르다 - reflgen::reflect 에 이름을 주지 않는다");
REFLECT_TYPE_LIST(REFLECT_IDENTITY_ONE)
#undef REFLECT_IDENTITY_ONE

inline void RegisterReflectManual()
{
    reflgen::generated::register_RenderEngine(Meta::Types());
    reflgen::generated::register_SceneRuntime(Meta::Types());

#define REFLECT_REGISTER_ONE(T) \
    Meta::Typed::RegisterOps<T>();

    REFLECT_TYPE_LIST(REFLECT_REGISTER_ONE)
#undef REFLECT_REGISTER_ONE
}
