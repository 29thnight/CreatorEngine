#pragma once
#include "GameObject.h"
#include "Component.h"
#include <unordered_set>
// ★ Scene.h를 include하지 말 것. 여기서 Scene이 필요한 자리는 전부
//   SceneObjectAt()(비템플릿, Entity.cpp에 정의)으로 우회한다.
//   inl이 Scene.h를 물면 Scene.h → Entity.h → Entity.inl → Scene.h
//   순환이 되살아나 Scene.h의 자급자족(HeaderSelfSufficiency.cpp)이 깨진다.

template<typename T>
inline T* Entity::AddComponent()
{
    // K2: 중복 검사가 맵 조회 → FindComponentSlot(마스크 선판정 + 선형 탐색)로
    // 바뀌었다. m_componentIds가 있던 시절엔 여기서 인덱스도 함께 심었지만,
    // 이제 정본은 m_components 하나뿐이라 심을 곳이 없다 — push_back 자체가
    // 곧 등록이다.
    if (FindComponentSlot(TypeTrait::GUIDCreator::GetTypeID<T>()) != kInvalidComponentSlot)
    {
        return nullptr;
    }

    // K2 스테이지 A: make_shared → make_unique. push_back으로 소유권을
    // 옮기기 전에 raw 포인터를 먼저 뽑아둔다 — move 후에는 로컬 component가
    // null이라 이후 줄에서 쓸 수 없다.
    std::unique_ptr<T> component = std::make_unique<T>();
    T* rawComponent = component.get();
    AttachComponentLifecycle(rawComponent);

    m_components.push_back(std::move(component));
    rawComponent->SetOwner(this);
    m_componentTypeMask |= (1ull << TypeTrait::ComponentTypeIndex::Get<T>());

    if (auto* initializable = dynamic_cast<System::IInitializable*>(rawComponent))
    {
        initializable->Initialize();
    }

    return rawComponent;
}

template<typename T, typename ...Args>
inline T* Entity::AddComponent(Args && ...args)
{
    if (FindComponentSlot(TypeTrait::GUIDCreator::GetTypeID<T>()) != kInvalidComponentSlot)
    {
        return nullptr;
    }

    std::unique_ptr<T> component = std::make_unique<T>(std::forward<Args>(args)...);
    T* rawComponent = component.get();
    AttachComponentLifecycle(rawComponent);

    m_components.push_back(std::move(component));
    rawComponent->SetOwner(this);
    m_componentTypeMask |= (1ull << TypeTrait::ComponentTypeIndex::Get<T>());

    if (auto* initializable = dynamic_cast<System::IInitializable*>(rawComponent))
    {
        initializable->Initialize();
    }

    return rawComponent;
}

template<typename T>
inline T* Entity::GetComponent()
{
    // K2: 맵 조회 → FindComponentSlot(마스크 선판정 + 선형 탐색)로 수렴.
    // K1-a 마스크로 먼저 "없음"을 걸러내는 효과는 FindComponentSlot 내부에
    // 그대로 있다 — 씬 58%·프리팹 89%가 특정 타입을 0개 갖는 게 실측이라
    // (SceneGraphRedesignPlan §4 트랙 K), 그 경우 벡터를 훑지 않고 끝난다.
    const size_t slot = FindComponentSlot(type_guid(T));
    if (slot == kInvalidComponentSlot)
        return nullptr;
    return static_cast<T*>(m_components[slot].get());
}

template<typename T>
inline T* Entity::GetComponentDynamicCast()
{
    // K2 스테이지 A: dynamic_pointer_cast(shared_ptr 전용) → dynamic_cast — 고유
    // 소유라 원본 소유권을 옮길 필요가 없고, 원한 것도 애초에 raw 포인터였다.
    for (auto& component : m_components)
    {
        if (T* casted = dynamic_cast<T*>(component.get()))
            return casted;
    }
    return nullptr;
}

// GetComponentsInChildren 전용 순회 가드(SceneGraphRedesignPlan §4 트랙 S, S2).
//
// Scene.h의 TryEnterTraversal(UpdateModelRecursive·LayoutUINode가 공유)을 여기서
// 못 쓴다 — 이 파일은 Scene.h를 include할 수 없다(파일 상단 주석, 순환 방지).
// 그래서 같은 가드(파괴 표시·순환 방문·최대 깊이)를 독립적으로 다시 둔다.
// 예전 구현은 이 셋 다 없었다 — m_childrenIndices에 순환이 생기면(자산 손상 등)
// 그대로 무한 재귀였다. 방문 집합은 재귀 한 호출 트리에서만 스택으로 들고
// 있는다(Scene 쪽과 달리 병렬 실행 경로가 없어 공유 걱정은 없다).
namespace GameObjectInlDetail
{
    inline constexpr int kComponentsInChildrenMaxDepth = 64;

    template<typename T>
    static void CollectComponentsInChildren(Entity* node, std::vector<T*>& out,
        std::unordered_set<const Entity*>& visited, int depth)
    {
        if (nullptr == node) return;
        if (depth > kComponentsInChildrenMaxDepth) return; // 과深/순환 방어. 조용히 끊는다 —
            // 이 헬퍼는 로거 인프라를 끌어오지 않는 경량 인라인 경로다(Scene 쪽 동종
            // 가드가 같은 사고를 이미 로그로 남긴다).

        for (auto& childIndex : node->m_childrenIndices)
        {
            Entity* childObj = node->SceneObjectAt(childIndex);
            if (nullptr == childObj || childObj->IsDestroyMark()) continue;
            if (!visited.insert(childObj).second) continue; // 이미 봤다 — 순환이면 여기서 멈춘다.

            if (T* comp = childObj->GetComponent<T>())
            {
                out.push_back(comp);
            }
            CollectComponentsInChildren<T>(childObj, out, visited, depth + 1);
        }
    }
}

template<typename T>
inline std::vector<T*> Entity::GetComponentsInChildren()
{
    std::vector<T*> comps;
    std::unordered_set<const Entity*> visited;
    GameObjectInlDetail::CollectComponentsInChildren<T>(this, comps, visited, 0);
    return comps;
}

template<typename T>
inline std::vector<T*> Entity::GetComponentsInchildrenDynamicCast() {
    std::vector<T*> comps;
    for (auto& childIndex : m_childrenIndices)
    {
        if (Entity* childObj = SceneObjectAt(childIndex))
        {
            if (T* comp = childObj->GetComponentDynamicCast<T>())
            {
                comps.push_back(comp);
            }
            auto childComps = childObj->GetComponentsInchildrenDynamicCast<T>();
            comps.insert(comps.end(), childComps.begin(), childComps.end());
        }
    }
    return comps;
}

// S1-b: m_transform 값 멤버가 사라지며 &m_transform이 더는 성립하지 않는다.
// m_pTransformComponent(생성자에서 AddComponent<Transform>() 직후 채움)를
// 그대로 돌려준다 — FindComponentSlot 선형 탐색을 건너뛰는 지름길이라는
// 원래 의도(이 특수화가 존재하는 이유)는 그대로 유지된다.
template<>
inline Transform* Entity::GetComponent()
{
    return m_pTransformComponent;
}

template<typename T>
inline bool Entity::HasComponent()
{
    // K1-a: 마스크 비트 검사 한 줄 — m_componentIds(K2에서 소멸)에 기댄 적이
    // 없다. 컴파일 타임에 비트 위치가 정해지는 유일한 자리라 FindComponentSlot의
    // 런타임 Find(HashedGuid)보다도 싸다.
    return 0 != (m_componentTypeMask & (1ull << TypeTrait::ComponentTypeIndex::Get<T>()));
}

template<typename T>
inline std::vector<T*> Entity::GetComponents()
{
    std::vector<T*> comps;
    for (auto& component : m_components)
    {
        if (T* casted = dynamic_cast<T*>(component.get()))
            comps.push_back(casted);
    }
    return comps;
}

template<typename T>
inline void Entity::RemoveComponent(T* component)
{
	// K2: 여기서 물리적으로 슬롯을 비우지 않는다(swap-and-pop을 하지 않는다) —
	// 의도적이다. Scene::RegisterComponent가 컴포넌트를 SystemSchedule의 페이즈
	// 리스트(Update/LateUpdate/FixedUpdate/DestroyWatch 등)에 raw Component*로
	// 등록해 두는데, 그 구독 해제(UnregisterComponent)는 프레임 끝의 단일 파괴
	// 지점(Scene::FlushPendingDestroy)에서만 일어난다(Scene.cpp:1047 주석 —
	// "여기가 유일하다는 것이 순회 중 UAF와 즉시 파괴를 동시에 닫는다"). 지금
	// m_components에서 바로 지우면 고유 소유(K2 스테이지 A: std::unique_ptr)가
	// 여기서 끝나 컴포넌트가 즉시 소멸하고, 아직 구독 해제 전인 스케줄 리스트의
	// raw 포인터가 댕글링된다 — component->Destroy()로 마크만 하고, 실제 슬롯
	// 압축은 기존과 동일하게 Scene::DestroyComponents()(프레임마다 도는 압축
	// 패스)에 맡긴다. 이중 구조(벡터+맵)가 사라진 것과 "언제 지우는가"는 별개다 —
	// 즉시 삭제로 바꾸는 건 별도 위험을 새로 들이는 것이라 K2 범위 밖으로 둔다.
    component->Destroy();
	auto it = std::ranges::find_if(m_components, [&](const std::unique_ptr<Component>& comp) { return comp.get() == component; });

	if (it != m_components.end())
	{
		const HashedGuid typeID = component->GetTypeID();

		// K1-a: 마스크는 "이 타입이 하나 이상 있는가"다. AddComponentAllowMultiple로
		// 같은 타입을 여러 개 붙인 경우(스크립트) 하나를 지워도 나머지가 있으면
		// 비트를 유지해야 한다. m_componentIds(K2에서 소멸, 타입당 인덱스 하나만
		// 담아 판정 근거가 못 됐다)가 있던 시절과 마찬가지로, 실제 잔존 여부는
		// m_components 선형 탐색으로만 알 수 있다.
		const bool anyRemaining = std::any_of(m_components.begin(), m_components.end(),
			[&](const std::unique_ptr<Component>& comp)
			{
				return comp && comp.get() != component && comp->GetTypeID() == typeID;
			});

		if (!anyRemaining)
		{
			const uint32_t index = TypeTrait::ComponentTypeIndex::Find(typeID);
			if (index != TypeTrait::ComponentTypeIndex::kInvalid)
			{
				m_componentTypeMask &= ~(1ull << index);
			}
		}
	}
}

// 타입→슬롯 탐색의 단일 구현 (SceneGraphRedesignPlan §4 트랙 K, K2). 선언은
// Entity.h private 절 참고 — 여기서는 비템플릿이라 inline을 직접 붙여야
// TU마다 중복 정의 없이 헤더에 안전히 둘 수 있다.
inline size_t Entity::FindComponentSlot(const HashedGuid& typeID) const
{
	// K1-a 마스크가 이 typeID를 알고 있고(등록됨) 비트가 꺼져 있으면 "확실히
	// 없음" — 벡터를 훑지 않는다. 마스크가 모르는 typeID(미등록 타입, kInvalid)면
	// 마스크로 판단할 수 없으니 그냥 선형 탐색으로 내려간다.
	const uint32_t maskIndex = TypeTrait::ComponentTypeIndex::Find(typeID);
	if (maskIndex != TypeTrait::ComponentTypeIndex::kInvalid &&
		0 == (m_componentTypeMask & (1ull << maskIndex)))
	{
		return kInvalidComponentSlot;
	}

	for (size_t i = 0; i < m_components.size(); ++i)
	{
		const auto& component = m_components[i];
		// tombstone 방어: Destroy()로 마크만 되고 아직 Scene::DestroyComponents()의
		// 압축 패스를 못 거친 슬롯은 파괴 대기 중이라도 물리적으로는 여전히
		// m_components에 남아 있다(위 RemoveComponent<T> 주석 참고) — nullptr인
		// 경우만 걸러내면 된다.
		if (component && component->GetTypeID() == typeID)
		{
			return i;
		}
	}
	return kInvalidComponentSlot;
}

inline Component* Entity::FindComponent(const HashedGuid& typeID) const
{
	const size_t slot = FindComponentSlot(typeID);
	return (slot == kInvalidComponentSlot) ? nullptr : m_components[slot].get();
}
