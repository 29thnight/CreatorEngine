#pragma once
#include "Core.Minimal.h"
#include "../ScriptBinder/Transform.h"
#include "ObjectRenderers.h"

class Bone;
class SceneObject
{
public:
	using Index = int;
	SceneObject(const std::string_view& name, SceneObject::Index index, SceneObject::Index parentIndex);
	SceneObject(SceneObject&) = delete;
	SceneObject(SceneObject&&) noexcept = default;
	SceneObject& operator=(SceneObject&) = delete;

	void ShowBoneHierarchy(Bone* bone);
	void RenderBoneEditor();
	void EditorMeshRenderer();

	std::string m_name;
	Transform m_transform;
	const Index m_index;
	const Index m_parentIndex;
	std::vector<SceneObject::Index> m_childrenIndices;


	MeshRenderer m_meshRenderer;
	SpriteRenderer m_spriteRenderer;
	Animator* m_animator{ nullptr };

	//ui ÄÄÆ÷³ÍÆ®¿ë
	UIRenderer m_UiRenderer;
	//debug layer
	Bone* selectedBone{ nullptr };
};