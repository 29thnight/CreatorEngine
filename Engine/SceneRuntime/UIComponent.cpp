#include "UIComponent.h"
#include "AuthoringNodeViewAccess.h" // D3-a-4
#include "Entity.h"
#include "Canvas.h"
#include "UIManager.h"
#include "Scene.h"
#include <unordered_map>
#include <unordered_set>

float MaxOreder = 100.0f;

UIComponent::UIComponent()
{
}

void UIComponent::SetCanvas(Canvas* canvas)
{
	if (nullptr == canvas || nullptr == canvas->GetOwner())
	{
		m_ownerCanvasObject = EntityHandle{};
		return;
	}

	Entity* canvasOwner = canvas->GetOwner();
	Scene* scene = canvasOwner->GetScene();
	m_ownerCanvasObject = scene ? scene->HandleOf(canvasOwner->m_index) : EntityHandle{};

	// 직렬화용 이름은 연결 시점에 한 번 기록한다. 예전에는 Canvas::Update가
	// 매 프레임 이름 변화를 폴링해 자식들 문자열을 갱신했다(6-2에서 제거).
	// 런타임 연결은 이제 직접 참조라 이름이 어긋나도 동작에는 영향이 없고,
	// 이 값은 다음 로드 때 캔버스를 되찾는 열쇠로만 쓰인다.
	m_ownerCanvasName = canvas->GetOwner()->m_name.ToString();
}

Canvas* UIComponent::GetOwnerCanvas()
{
	Entity* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	Entity* canvasObject = scene ? scene->Resolve(m_ownerCanvasObject) : nullptr;
	if (!canvasObject || canvasObject->IsDestroyMark()) return nullptr;

	return canvasObject->GetComponent<Canvas>();
}

void UIComponent::SetNavi(Direction dir, Entity* otherUI)
{
	const int direction = static_cast<int>(dir);
	if (direction < 0 || direction >= NavDirectionCount || !otherUI)
		return;
    Navigation nav;
    nav.mode = direction;
	if (!UpdateNavigationRoute(nav, otherUI))
		return;

	Scene* scene = otherUI->GetScene();
	const EntityHandle targetHandle = scene ? scene->HandleOf(otherUI->m_index) : EntityHandle{};
	if (!targetHandle.IsValid()) return;
	navigation[direction] = targetHandle;

    auto it =  std::ranges::find_if(navigations, [&](const Navigation& n)
    {
        return n.mode == nav.mode; 
    });

    if (it == navigations.end())
    {
        navigations.push_back(nav);
    }
    else
    {
	    *it = nav;
    }
}

void UIComponent::OnAddedToScene()
{
	// DDOL 재부착은 같은 C++ 객체에 새 sceneId/index/generation을 부여한다.
	// 옛 씬 핸들을 비우고, 모든 노드가 붙은 뒤 UIManager의 지연 연결/Navigation
	// 해소가 현재 계층에서 새 핸들을 다시 만든다.
	m_ownerCanvasObject = EntityHandle{};
	navigation.fill(EntityHandle{});
	isDeserialized = false;
	m_canvasLinkLogged = false;
}

void UIComponent::OnRemovingFromScene()
{
	if (Canvas* canvas = GetOwnerCanvas())
		canvas->RemoveUIObject(GetOwner());
	m_ownerCanvasObject = EntityHandle{};
	navigation.fill(EntityHandle{});
	isDeserialized = false;
}

namespace
{
	constexpr size_t kNavigationMaxDepth = 256;

	bool BuildAncestorChain(Entity* start, std::vector<Entity*>& chain)
	{
		chain.clear();
		std::unordered_set<Entity*> visited;
		Entity* node = start;
		for (size_t depth = 0; node && depth < kNavigationMaxDepth; ++depth)
		{
			if (!visited.insert(node).second)
				return false;
			chain.push_back(node);

			const Entity::Index parentIndex = node->GetParentIndex();
			if (!Entity::IsValidIndex(parentIndex))
				return true;
			node = node->OwnerSceneFindIndex(parentIndex);
		}
		return node == nullptr;
	}

	bool FindLiveChildOrdinal(Entity* parent, Entity* child, uint32_t& ordinal)
	{
		if (!parent || !child || parent->GetScene() != child->GetScene())
			return false;

		uint32_t liveOrdinal = 0;
		for (Entity::Index childIndex : parent->GetChildrenIndices())
		{
			Entity* candidate = parent->OwnerSceneFindIndex(childIndex);
			if (!candidate || candidate->IsDestroyMark())
				continue;
			if (candidate == child)
			{
				ordinal = liveOrdinal;
				return true;
			}
			++liveOrdinal;
		}
		return false;
	}

	Entity* FindLiveChildAt(Entity* parent, uint32_t ordinal)
	{
		if (!parent)
			return nullptr;

		uint32_t liveOrdinal = 0;
		for (Entity::Index childIndex : parent->GetChildrenIndices())
		{
			Entity* candidate = parent->OwnerSceneFindIndex(childIndex);
			if (!candidate || candidate->IsDestroyMark())
				continue;
			if (liveOrdinal == ordinal)
				return candidate;
			++liveOrdinal;
		}
		return nullptr;
	}
}

bool UIComponent::UpdateNavigationRoute(Navigation& nav, Entity* target)
{
	Entity* source = GetOwner();
	if (!source || !target || source->GetScene() != target->GetScene())
		return false;

	std::vector<Entity*> sourceChain;
	std::vector<Entity*> targetChain;
	if (!BuildAncestorChain(source, sourceChain) || !BuildAncestorChain(target, targetChain))
		return false;

	std::unordered_map<Entity*, size_t> sourceDepth;
	sourceDepth.reserve(sourceChain.size());
	for (size_t i = 0; i < sourceChain.size(); ++i)
		sourceDepth.emplace(sourceChain[i], i);

	size_t targetLcaDepth = 0;
	auto sourceLca = sourceDepth.end();
	for (; targetLcaDepth < targetChain.size(); ++targetLcaDepth)
	{
		sourceLca = sourceDepth.find(targetChain[targetLcaDepth]);
		if (sourceLca != sourceDepth.end())
			break;
	}
	if (sourceLca == sourceDepth.end())
		return false;

	std::vector<uint32_t> childPath;
	childPath.reserve(targetLcaDepth);
	Entity* parent = targetChain[targetLcaDepth];
	for (size_t depth = targetLcaDepth; depth > 0; --depth)
	{
		Entity* child = targetChain[depth - 1];
		uint32_t ordinal = 0;
		if (!FindLiveChildOrdinal(parent, child, ordinal))
			return false;
		childPath.push_back(ordinal);
		parent = child;
	}

	nav.parentHops = static_cast<uint32_t>(sourceLca->second);
	nav.childOrdinals = std::move(childPath);
	return true;
}

Entity* UIComponent::ResolveNavigationRoute(const Navigation& nav) const
{
	if (!nav.HasTarget())
		return nullptr;

	Entity* node = GetOwner();
	for (uint32_t hop = 0; node && hop < nav.parentHops; ++hop)
	{
		const Entity::Index parentIndex = node->GetParentIndex();
		if (!Entity::IsValidIndex(parentIndex))
			return nullptr;
		node = node->OwnerSceneFindIndex(parentIndex);
	}
	for (uint32_t ordinal : nav.childOrdinals)
	{
		node = FindLiveChildAt(node, ordinal);
		if (!node)
			return nullptr;
	}
	return node;
}

void UIComponent::OnBeforeSerialize()
{
	Entity* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	for (Navigation& nav : navigations)
	{
		if (nav.mode < 0 || nav.mode >= NavDirectionCount)
			continue;
		if (Entity* target = scene ? scene->Resolve(navigation[nav.mode]) : nullptr)
			UpdateNavigationRoute(nav, target);
	}
}

void UIComponent::LoadLegacyNavigation(const Authoring::NodeView& view)
{
	const YAML::Node& componentNode = Authoring::NodeViewAccess::Node(view);
	m_legacyNavigationIds.fill(HashedGuid{});
	const YAML::Node legacyNavigations = componentNode["navigations"];
	if (!legacyNavigations || !legacyNavigations.IsSequence())
		return;

	for (const auto& legacy : legacyNavigations)
	{
		if (!legacy["mode"] || !legacy["navObject"])
			continue;
		const int mode = legacy["mode"].as<int>();
		if (mode < 0 || mode >= NavDirectionCount)
			continue;
		m_legacyNavigationIds[mode] = HashedGuid(legacy["navObject"].as<size_t>());
	}
}

void UIComponent::DeserializeNavi()
{
    Entity* thisObj = GetOwner();
	if (!thisObj)
		return;

	size_t expected = 0;
	size_t resolved = 0;
    for (auto& nav : navigations)
    {
		if (nav.mode < 0 || nav.mode >= NavDirectionCount)
			continue;
		++expected;

		Entity* obj = ResolveNavigationRoute(nav);
		if (!obj && m_legacyNavigationIds[nav.mode].m_ID_Data != HashedGuid::INVAILD_ID)
		{
			obj = thisObj->OwnerSceneFindInstanceID(m_legacyNavigationIds[nav.mode]);
			if (obj)
			{
				// 구 scene 파일을 메모리에서 즉시 새 표현으로 승격한다. 다음 저장은
				// navObject를 다시 쓰지 않고 이 경로만 쓴다.
				UpdateNavigationRoute(nav, obj);
				m_legacyNavigationIds[nav.mode] = HashedGuid{};
			}
		}

		if (obj)
        {
			Scene* scene = obj->GetScene();
			navigation[(int)nav.mode] = scene ? scene->HandleOf(obj->m_index) : EntityHandle{};
			++resolved;
        }
	}

	isDeserialized = (expected == resolved);
}

Entity* UIComponent::GetNextNavi(Direction dir)
{
	const int direction = static_cast<int>(dir);
	if (direction < 0 || direction >= NavDirectionCount) return nullptr;
	Entity* owner = GetOwner();
	Scene* scene = owner ? owner->GetScene() : nullptr;
	return scene ? scene->Resolve(navigation[direction]) : nullptr;
}

bool UIComponent::IsNavigationThis()
{
	Entity* thisObj = GetOwner();
	Entity* selectedObj = UIManagers->GetSelectUI();

    return nullptr != selectedObj && thisObj == selectedObj;
}


