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
    if (m_componentIds.find(TypeTrait::GUIDCreator::GetTypeID<T>()) != m_componentIds.end())
    {
        return nullptr;
    }

    std::shared_ptr<T> component = shared_alloc<T>();
    AttachComponentLifecycle(component);

    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size() - 1;
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
    if (m_componentIds.find(TypeTrait::GUIDCreator::GetTypeID<T>()) != m_componentIds.end())
    {
        return nullptr;
    }

    std::shared_ptr<T> component = shared_alloc<T>(std::forward<Args>(args)...);
    AttachComponentLifecycle(component);

    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size() - 1;
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
    // K1-a: 마스크로 먼저 "없음"을 걸러낸다. 씬 58%·프리팹 89%가 특정 타입을
    // 0개 갖는 게 실측이라(SceneGraphRedesignPlan §4 트랙 K), 그 경우 맵 조회
    // 없이 한 번의 비트 검사로 끝난다.
    if (!HasComponent<T>())
        return nullptr;

    auto it = m_componentIds.find(type_guid(T));
    if (it == m_componentIds.end())
        return nullptr;
    return static_cast<T*>(m_components[it->second].get());
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
    // K1-a: 마스크 비트 검사 한 줄. 예전엔 m_componentIds.find를 두 번 불렀다
    // (첫 호출의 결과를 버리고 다시 찾는 낭비 — GameObject.inl:139-140 구판).
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
    component->Destroy();
	auto it = std::ranges::find_if(m_components, [&](std::shared_ptr<Component> comp) { return comp.get() == component; });

	if (it != m_components.end())
	{
		const HashedGuid typeID = component->GetTypeID();
		m_componentIds.erase(typeID);

		// K1-a: 마스크는 "이 타입이 하나 이상 있는가"다. AddComponentAllowMultiple로
		// 같은 타입을 여러 개 붙인 경우(스크립트) 하나를 지워도 나머지가 있으면
		// 비트를 유지해야 한다 — m_componentIds는 타입당 인덱스 하나만 담아서
		// (GameObject.cpp:181-186) 판정 근거가 못 되고, 실제 잔존 여부는
		// m_components를 봐야 한다.
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
