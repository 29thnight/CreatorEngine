#pragma once
#include "Core.Minimal.h"
#include "../ScriptBinder/Transform.h"
#include "ObjectRenderers.h"

class SceneObject
{
public:
	using Index = int;
	SceneObject(const std::string_view& name, SceneObject::Index index, SceneObject::Index parentIndex);
	SceneObject(SceneObject&) = delete;
	SceneObject(SceneObject&&) noexcept = default;
	SceneObject& operator=(SceneObject&) = delete;

	std::string m_name;
	Transform m_transform;
	const Index m_index;
	const Index m_parentIndex;
	std::vector<SceneObject::Index> m_childrenIndices;

	MeshRenderer m_meshRenderer;
	SpriteRenderer m_spriteRenderer;
	Animator m_animator;
};