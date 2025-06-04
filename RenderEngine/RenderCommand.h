#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include "LightMapping.h"
#include "Animator.h"

class Material;
class Mesh;
class OctreeNode;
class MeshRenderer;
class RenderCommand
{
public:
	RenderCommand(MeshRenderer* component);
	~RenderCommand();

	RenderCommand(const RenderCommand& other);
	RenderCommand(RenderCommand&& other) noexcept;

public:
	bool IsNeedUpdateCulling() const { return m_isNeedUptateCulling; }
	void SetNeedUpdateCulling(bool able) { m_isNeedUptateCulling = able; }

	void SetSkinnedMesh(bool isSkinned) { m_isSkinnedMesh = isSkinned; }
	bool IsSkinnedMesh() const { return m_isSkinnedMesh; }

public:
	Material*	m_Material{ nullptr };
	Mesh*		m_Mesh{ nullptr };
	HashedGuid  m_animatorGuid{};

public:
	DirectX::XMMATRIX m_finalTransforms[MAX_BONES]{};
	LightMapping	  m_LightMapping;
	bool			  m_isSkinnedMesh{ false };
	bool			  m_isAnimationEnabled{ false };
	Mathf::xMatrix    m_worldMatrix{};

private:
	bool m_isNeedUptateCulling{ false };
	//std::unordered_set<OctreeNode*> m_OctreeNodes;
};