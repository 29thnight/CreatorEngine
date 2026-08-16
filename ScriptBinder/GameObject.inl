#pragma once
#include "GameObject.h"
#include "Component.h"
// ★ Scene.h를 include하지 말 것. 여기서 Scene이 필요한 자리는 전부
//   SceneObjectAt()(비템플릿, GameObject.cpp에 정의)으로 우회한다.
//   inl이 Scene.h를 물면 Scene.h → GameObject.h → GameObject.inl → Scene.h
//   순환이 되살아나 Scene.h의 자급자족(HeaderSelfSufficiency.cpp)이 깨진다.

template<typename T>
inline T* GameObject::AddComponent()
{
    // K2: 중복 검사가 맵 조회 → FindComponentSlot(마스크 선판정 + 선형 탐색)로
    // 바뀌었다. m_componentIds가 있던 시절엔 여기서 인덱스도 함께 심었지만,
    // 이제 정본은 m_components 하나뿐이라 심을 곳이 없다 — push_back 자체가
    // 곧 등록이다.
    if (FindComponentSlot(TypeTrait::GUIDCreator::GetTypeID<T>()) != kInvalidComponentSlot)
    {
        return nullptr;
    }

    std::shared_ptr<T> component = shared_alloc<T>();
    AttachComponentLifecycle(component);

    m_components.push_back(component);
    component->SetOwner(this);
    m_componentTypeMask |= (1ull << TypeTrait::ComponentTypeIndex::Get<T>());

    if (auto initializable = std::dynamic_pointer_cast<System::IInitializable>(component))
    {
        initializable->Initialize();
    }

    return component.get();
}

template<typename T, typename ...Args>
inline T* GameObject::AddComponent(Args && ...args)
{
    if (FindComponentSlot(TypeTrait::GUIDCreator::GetTypeID<T>()) != kInvalidComponentSlot)
    {
        return nullptr;
    }

    std::shared_ptr<T> component = shared_alloc<T>(std::forward<Args>(args)...);
    AttachComponentLifecycle(component);

    m_components.push_back(component);
    component->SetOwner(this);
    m_componentTypeMask |= (1ull << TypeTrait::ComponentTypeIndex::Get<T>());

    if (auto initializable = std::dynamic_pointer_cast<System::IInitializable>(component))
    {
        initializable->Initialize();
    }

    return component.get();
}

template<typename T>
inline T* GameObject::GetComponent(uint32 index)
{
    if(!m_components.empty())
    {
        return static_cast<T*>(m_components[index].get());
    }
    return nullptr;
}

template<typename T>
inline T* GameObject::GetComponent()
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
inline T* GameObject::GetComponentDynamicCast()
{
    for (auto& component : m_components)
    {
        std::shared_ptr<T> castedComponent = std::dynamic_pointer_cast<T>(component);
        if (nullptr != castedComponent.get())
            return castedComponent.get();
    }
    return nullptr;
}

template<typename T>
inline std::vector<T*> GameObject::GetComponentsInChildren()
{
    std::vector<T*> comps;
    for (auto& childIndex : m_childrenIndices)
    {
        if (GameObject* childObj = SceneObjectAt(childIndex))
        {
            if (T* comp = childObj->GetComponent<T>())
            {
                comps.push_back(comp);
            }
            auto childComps = childObj->GetComponentsInChildren<T>();
            comps.insert(comps.end(), childComps.begin(), childComps.end());
        }
    }
	return comps;
}

template<typename T>
inline std::vector<T*> GameObject::GetComponentsInchildrenDynamicCast() {
    std::vector<T*> comps;
    for (auto& childIndex : m_childrenIndices)
    {
        if (GameObject* childObj = SceneObjectAt(childIndex))
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

template<>
inline Transform* GameObject::GetComponent()
{
    return &m_transform;
}

template<typename T>
inline bool GameObject::HasComponent()
{
    // K1-a: 마스크 비트 검사 한 줄 — m_componentIds(K2에서 소멸)에 기댄 적이
    // 없다. 컴파일 타임에 비트 위치가 정해지는 유일한 자리라 FindComponentSlot의
    // 런타임 Find(HashedGuid)보다도 싸다.
    return 0 != (m_componentTypeMask & (1ull << TypeTrait::ComponentTypeIndex::Get<T>()));
}

template<typename T>
inline std::vector<T*> GameObject::GetComponents()
{
    std::vector<T*> comps;
    for (auto& component : m_components)
    {
        if (std::shared_ptr<T> castedComponent = std::dynamic_pointer_cast<T>(component))
            comps.push_back(castedComponent.get());
    }
    return comps;
}

template<typename T>
inline void GameObject::RemoveComponent(T* component)
{
	// K2: 여기서 물리적으로 슬롯을 비우지 않는다(swap-and-pop을 하지 않는다) —
	// 의도적이다. Scene::RegisterComponent가 컴포넌트를 SystemSchedule의 페이즈
	// 리스트(Update/LateUpdate/FixedUpdate/DestroyWatch 등)에 raw Component*로
	// 등록해 두는데, 그 구독 해제(UnregisterComponent)는 프레임 끝의 단일 파괴
	// 지점(Scene::FlushPendingDestroy)에서만 일어난다(Scene.cpp:1047 주석 —
	// "여기가 유일하다는 것이 순회 중 UAF와 즉시 파괴를 동시에 닫는다"). 지금
	// m_components에서 바로 지우면 마지막 shared_ptr이 여기서 죽어 컴포넌트가
	// 즉시 소멸하고, 아직 구독 해제 전인 스케줄 리스트의 raw 포인터가 댕글링된다
	// — component->Destroy()로 마크만 하고, 실제 슬롯 압축은 기존과 동일하게
	// Scene::DestroyComponents()(프레임마다 도는 압축 패스)에 맡긴다. 이중 구조
	// (벡터+맵)가 사라진 것과 "언제 지우는가"는 별개다 — 즉시 삭제로 바꾸는 건
	// 별도 위험을 새로 들이는 것이라 K2 범위 밖으로 둔다.
    component->Destroy();
	auto it = std::ranges::find_if(m_components, [&](const std::shared_ptr<Component>& comp) { return comp.get() == component; });

	if (it != m_components.end())
	{
		const HashedGuid typeID = component->GetTypeID();

		// K1-a: 마스크는 "이 타입이 하나 이상 있는가"다. AddComponentAllowMultiple로
		// 같은 타입을 여러 개 붙인 경우(스크립트) 하나를 지워도 나머지가 있으면
		// 비트를 유지해야 한다. m_componentIds(K2에서 소멸, 타입당 인덱스 하나만
		// 담아 판정 근거가 못 됐다)가 있던 시절과 마찬가지로, 실제 잔존 여부는
		// m_components 선형 탐색으로만 알 수 있다.
		const bool anyRemaining = std::any_of(m_components.begin(), m_components.end(),
			[&](const std::shared_ptr<Component>& comp)
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
// GameObject.h private 절 참고 — 여기서는 비템플릿이라 inline을 직접 붙여야
// TU마다 중복 정의 없이 헤더에 안전히 둘 수 있다.
inline size_t GameObject::FindComponentSlot(const HashedGuid& typeID) const
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

inline Component* GameObject::FindComponent(const HashedGuid& typeID) const
{
	const size_t slot = FindComponentSlot(typeID);
	return (slot == kInvalidComponentSlot) ? nullptr : m_components[slot].get();
}
