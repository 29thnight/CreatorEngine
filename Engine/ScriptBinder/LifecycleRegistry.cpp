#include "LifecycleRegistry.h"
#include "ComponentTypeUUID.h"

#include <unordered_map>

// 등록 대상 컴포넌트 헤더.
// 이 목록이 곧 "생명주기를 받는 타입의 전부"다.
#include "Animator.h"
#include "BehaviorTreeComponent.h"
#include "BoneComponent.h"
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
#include "Transform.h"
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
        // 이 함수는 두 자리에서 부를 수 있다 — Scene::RegisterComponent가 표가
        // 비어 있으면 먼저 세우고(기본 씬의 Main Camera·Directional Light가
        // ComponentFactory::Initialize보다 먼저 만들어지는 기동 순서 때문,
        // Scene.cpp:789 참고), ComponentFactory::Initialize가 나중에 무조건
        // 다시 부른다. Register<T>()의 g_masks 덮어쓰기는 원래 멱등이라 두 번
        // 불려도 무해했지만, 아래 ComponentTypeUUID::RegisterAll()(K1-b)은 같은
        // 이름을 두 번 보면 "중복 등록"으로 즉시 abort한다 — 그래서 이 함수 자체를
        // 멱등하게 만들어야 한다.
        if (0 != Count()) return;

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
        // E7-b(트랙 E): 저장된 GameObjectType::Bone 판정을 대신하는 마커
        // 컴포넌트. 생명주기 훅을 하나도 오버라이드하지 않는다(마스크 0,
        // 위 문단과 같은 사유로 등록 자체는 필요).
        Register<BoneComponent>();
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
        // S1-b: Transform이 Component로 승격되며 신규 등록. Awake/Update 등
        // 어느 훅도 오버라이드하지 않는다(마스크 0, 위 문단과 같은 사유로
        // 등록 자체는 필요) — Transform 갱신은 Scene::UpdateModelRecursive
        // 순회로 도는 별도 경로이지 Component 생명주기 디스패치가 아니다.
        Register<Transform>();
        Register<UIButton>();
        Register<VolumeComponent>();

        // K1-b: 영속 UUID 표를 채운다. 위 Register<T>() 목록과
        // ComponentTypeUUID::kTable은 서로 다른 자리에서 손으로 유지하는 두
        // 목록이라 개수가 어긋날 수 있다 — 한쪽에는 타입을 추가했는데 다른
        // 쪽을 빠뜨린 경우다. RegisterAll() 내부가 이름·UUID 중복을 잡고,
        // 아래 개수 비교가 "누락"(한쪽에만 있음)을 잡는다.
        ComponentTypeUUID::RegisterAll();
        if (Count() != ComponentTypeUUID::kTable.size())
        {
            Debug->LogCritical("LifecycleRegistry/ComponentTypeUUID 등록 수 불일치: Lifecycle="
                + std::to_string(Count()) + ", UUID=" + std::to_string(ComponentTypeUUID::kTable.size())
                + " - 두 목록 중 한쪽에 타입을 추가하고 다른 쪽을 빠뜨렸을 가능성");
            Log::FlushNow();
            std::abort();
        }
    }
}
