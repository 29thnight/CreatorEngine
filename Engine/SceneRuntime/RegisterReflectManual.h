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
#include "LogSystem.h"          // VerifyReflectRegistration — critical 로그 후 abort
#include <cstdlib>
#include <string>
#include "MeshRenderer.h"
#include "LightMapping.h"
#include "ShadowMapPassSetting.h"
#include "BlackBoardValue.h"
#include "AssetEntry.h"
#include "DecalComponent.h"
#include "CameraComponent.h"
#include "PhysicsBodyComponent.h"
#include "CharacterMovementComponent.h"
#include "SpriteRenderer.h"
#include "AssetBundle.h"
#include "BloomSetting.h"
#include "AAPassSetting.h"
#include "MaterialFlowInformation.h"
#include "SSGIPassSetting.h"
#include "Camera.h"
#include "ColorGradingPassSetting.h"
#include "FoliageComponent.h"
#include "BitMaskPassSetting.h"
#include "DeferredPassSetting.h"
#include "FoliageInstance.h"
#include "SoundComponent.h"
#include "Scene.h"
#include "SceneRenderProfile.h"
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
#include "PrefabOverride.h"
#include "Canvas.h"
#include "BTBuildNode.h"
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
#include "SceneRenderProfileComponent.h"

#define REFLECT_TYPE_LIST(X) \
    X(MeshRenderer) \
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
    X(PhysicsBodyComponent) \
    X(CharacterMovementComponent) \
    X(Canvas) \
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
    X(Navigation) \
    X(Object) \
    X(PlayerInputComponent) \
    X(Prefab) \
    X(PrefabOverride) \
    X(RectTransformComponent) \
    X(RenderPassSettings) \
    X(SSAOPassSetting) \
    X(SSGIPassSetting) \
    X(Scene) \
    X(ScriptComponent) \
    X(SoundComponent) \
    X(SpriteRenderer) \
    X(SpriteSheetComponent) \
    X(StateMachineComponent) \
    X(TerrainComponent) \
    X(TextComponent) \
    X(ToneMapPassSetting) \
    X(TransCondition) \
    X(Transform) \
    X(UIButton) \
    X(VignettePassSetting) \
    X(SceneRenderProfileComponent) \
    X(SceneRenderProfile) \
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

// 등록소의 서술자가 목록의 타입을 쓰고 읽을 수 있는가 — 기동 때 1회. 서술자는 등록 함수의 번역 단위에서 만들어지고,
// 그곳이 엔진 serializer 특수화를 못 보면(ReflgenRegistration.h 에서 빠지면) 그 필드는 직렬화기 없이 남는다. 컴파일
// 오류가 없고 씬을 저장·로드할 때 serialization_error 로 처음 드러나며, 번역 단위마다 서술도 갈린다. reflgen 은
// 다른 곳에 특수화가 있다는 것을 알 수 없으므로 엔진이 확인한다: 목록의 타입은 전부 서술자로 쓰고 읽는다(TypeOps 는
// postLoad 만 든다). transient 필드는 reflgen 이 판정에서 뺀다.
inline void VerifyReflectRegistration()
{
    std::string failures;
    const auto check = [&failures](const reflgen::type_descriptor& type) {
        if (type.is_serializable() && type.is_deserializable())
        {
            return;
        }
        failures += "\n  " + std::string(type.name()) + (type.is_serializable() ? "" : " [쓰기 불가]")
            + (type.is_deserializable() ? "" : " [읽기 불가]") + " -";
        for (const reflgen::field_info& field : type.fields())
        {
            if (!field.is_serializable() || !field.is_deserializable())
            {
                failures += " " + std::string(field.name()) + "(" + std::string(field.type_name()) + ")";
            }
        }
    };

#define REFLECT_VERIFY_ONE(T) \
    check(Meta::TypeOf<T>());

    REFLECT_TYPE_LIST(REFLECT_VERIFY_ONE)
#undef REFLECT_VERIFY_ONE

    if (!failures.empty())
    {
        Debug::PrintLog(spdlog::level::critical, "reflgen 서술자가 쓰거나 읽지 못하는 타입(필드의 serializer 가 등록 함수에"
            " 보이지 않는다 - Engine/RenderEngine/ReflgenRegistration.h 를 확인하라):" + failures);
        Log::FlushNow();
        std::abort();
    }
}

inline void RegisterReflectManual()
{
    reflgen::generated::register_RenderEngine(Meta::Types());
    reflgen::generated::register_SceneRuntime(Meta::Types());

#define REFLECT_REGISTER_ONE(T) \
    Meta::Typed::RegisterOps<T>();

    REFLECT_TYPE_LIST(REFLECT_REGISTER_ONE)
#undef REFLECT_REGISTER_ONE

    VerifyReflectRegistration();
}
