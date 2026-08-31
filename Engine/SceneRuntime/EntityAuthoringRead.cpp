#include "EntityAuthoringRead.h"

#include "RectTransformComponent.h"
#include "Transform.h"

namespace EntityAuthoring
{
GameObjectType InferCreationType(const Authoring::ReadNode& node)
{
	// E7-c 읽기 호환: 옛 씬/프리팹은 필드를 갖는다. 이 값은 객체 상태로
	// 저장하지 않고 생성 순간 공간 컴포넌트를 고르는 데 한 번만 쓴다.
	if (const Authoring::ReadNode legacyType = node["m_gameObjectType"])
	{
		return static_cast<GameObjectType>(legacyType.As<int>());
	}

	bool hasTransform = false;
	bool hasRectTransform = false;
	if (const Authoring::ReadNode components = node["m_components"];
		components && components.IsSequence())
	{
		for (const auto componentNode : components)
		{
			const Meta::Type* componentType = nullptr;
			try { componentType = Meta::ExtractTypeFromYAML(componentNode); }
			catch (const std::exception&) { continue; }
			if (!componentType) continue;

			hasTransform |= componentType->typeID == type_guid(Transform);
			hasRectTransform |= componentType->typeID == type_guid(RectTransformComponent);
		}
	}

	// UI는 Rect만, Canvas는 Rect+Transform, 나머지는 Transform이라는 S3의
	// 공간 구성 규칙을 역으로 읽는다. Light/Camera/Mesh/Bone 등은 나머지
	// 컴포넌트가 정체성을 말하므로 생성 아키타입은 Empty면 충분하다.
	if (hasRectTransform)
		return hasTransform ? GameObjectType::Canvas : GameObjectType::UI;
	return GameObjectType::Empty;
}

Entity::SerializedHierarchy ReadSerializedHierarchy(const Authoring::ReadNode& node)
{
	Entity::SerializedHierarchy result{};
	Authoring::ReadNode parentNode;
	Authoring::ReadNode rootNode;
	Authoring::ReadNode childrenNode;

	// 키 이름은 구 자산/도구 호환을 위해 유지한다. H3에서 달라진 경계는 이 값이
	// Entity 멤버로 역직렬화되지 않고 로드 배치 DTO로만 들어간다는 점이다.
	parentNode = node["m_parentIndex"];
	rootNode = node["m_rootIndex"];
	childrenNode = node["m_childrenIndices"];

	if (parentNode) result.parentIndex = parentNode.As<Entity::Index>(Entity::INVALID_INDEX);
	if (rootNode) result.rootIndex = rootNode.As<Entity::Index>(Entity::kSceneRootIndex);
	if (childrenNode && childrenNode.IsSequence())
	{
		result.childrenIndices.reserve(childrenNode.Size());
		for (const auto child : childrenNode)
			result.childrenIndices.push_back(child.As<Entity::Index>());
	}
	return result;
}
}
