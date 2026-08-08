#include "LifecycleRegistry.h"

#include <unordered_map>

// 등록 대상 컴포넌트 헤더.
// 이 목록이 곧 "생명주기를 받는 타입의 전부"다.
#include "Animator.h"
#include "BehaviorTreeComponent.h"
#include "BoxColliderComponent.h"
#include "CameraComponent.h"
#include "Canvas.h"
#include "CapsuleColliderComponent.h"
#include "CharacterControllerComponent.h"
#include "DecalComponent.h"
#include "FoliageComponent.h"
#include "ImageComponent.h"
#include "InvalidScriptComponent.h"
#include "LightComponent.h"
#include "RagdollComponent.h"
#include "RectTransformComponent.h"
#include "StateMachineComponent.h"
#include "UIComponent.h"
#include "MeshCollider.h"
#include "MeshRenderer.h"
#include "PlayerInput.h"
#include "RigidBodyComponent.h"
#include "ScriptComponent.h"
#include "SoundComponent.h"
#include "SphereColliderComponent.h"
#include "SpriteRenderer.h"
#include "SpriteSheetComponent.h"
#include "Terrain.h"
#include "TerrainCollider.h"
#include "TextComponent.h"
#include "UIButton.h"
#include "VolumeComponent.h"

namespace Lifecycle
{
    namespace
    {
        std::unordered_map<size_t, uint16_t> g_masks;
    }

    void Registry::Store(size_t typeID, uint16_t mask)
    {
        g_masks[typeID] = mask;
    }

    uint16_t Registry::Find(size_t typeID)
    {
        const auto it = g_masks.find(typeID);
        return (it != g_masks.end()) ? it->second : kUnregistered;
    }

    size_t Registry::Count()
    {
        return g_masks.size();
    }

    // 명시 목록이다. 자동 수집으로 만들지 않은 이유가 둘이다.
    //
    // ① 판정에 타입 T가 컴파일 타임에 필요하다. 리플렉션 레지스트리는 이름과
    //    Meta::Type만 들고 있어 T를 되살릴 수 없다.
    // ② 목록이 눈에 보이는 편이 낫다. "무엇이 생명주기를 받는가"는 엔진의 계약이고,
    //    그것이 26개 헤더의 상속 선언에 흩어져 있으면 전체를 보려면 전부 열어야 한다.
    //
    // 빠뜨리면 조용히 넘어가지 않는다 — Find가 kUnregistered를 돌려주고 등록 지점이
    // 그것을 오류로 보고한다(예전 CRTP는 이 경우 '훅 없는 타입'과 구분되지 않았다).
    void Registry::RegisterAllComponents()
    {
        Register<Animator>();
        Register<BehaviorTreeComponent>();
        Register<BoxColliderComponent>();
        Register<CameraComponent>();
        Register<Canvas>();
        Register<CapsuleColliderComponent>();
        Register<CharacterControllerComponent>();
        Register<DecalComponent>();
        Register<FoliageComponent>();
        Register<ImageComponent>();
        Register<LightComponent>();

        // 훅을 하나도 구현하지 않는 컴포넌트도 등록한다.
        //
        // 마스크가 0이면 어느 리스트에도 안 들어가므로 동작상 차이는 없다. 그런데
        // 등록하지 않으면 Find가 kUnregistered를 돌려주고 가드가 오류를 찍는다 —
        // 정상 상황에 오류가 쌓이면 그 로그를 아무도 안 읽게 되고, 그러면 진짜
        // 누락(새 컴포넌트를 만들고 목록에 안 넣은 경우)이 그 소음에 묻힌다.
        Register<InvalidScriptComponent>();
        Register<RagdollComponent>();
        Register<RectTransformComponent>();
        Register<StateMachineComponent>();
        Register<UIComponent>();
        Register<MeshColliderComponent>();
        Register<MeshRenderer>();
        Register<PlayerInputComponent>();
        Register<RigidBodyComponent>();
        Register<ScriptComponent>();
        Register<SoundComponent>();
        Register<SphereColliderComponent>();
        Register<SpriteRenderer>();
        Register<SpriteSheetComponent>();
        Register<TerrainComponent>();
        Register<TerrainColliderComponent>();
        Register<TextComponent>();
        Register<UIButton>();
        Register<VolumeComponent>();
    }
}
