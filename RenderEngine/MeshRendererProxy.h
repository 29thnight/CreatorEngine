#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include "LightMapping.h"
#include "Animator.h"

enum class ProxyType
{

};

class Material;
class Mesh;
class OctreeNode;
class MeshRenderer;
class MeshRendererProxy
{
public:
	MeshRendererProxy(MeshRenderer* component);
	~MeshRendererProxy();

	MeshRendererProxy(const MeshRendererProxy& other);
	MeshRendererProxy(MeshRendererProxy&& other) noexcept;

public:
	bool IsNeedUpdateCulling() const { return m_isNeedUptateCulling; }
	void SetNeedUpdateCulling(bool able) { m_isNeedUptateCulling = able; }

	void SetSkinnedMesh(bool isSkinned) { m_isSkinnedMesh = isSkinned; }
	bool IsSkinnedMesh() const { return m_isSkinnedMesh; }

	void Draw();
	void Draw(ID3D11DeviceContext* _defferedContext);

	friend bool SortByAnimationAndMaterialGuid(MeshRendererProxy* a, MeshRendererProxy* b);

	void DistroyProxy();

public:
	Material*		m_Material{ nullptr };
	Mesh*			m_Mesh{ nullptr };
	HashedGuid		m_animatorGuid{};
	HashedGuid      m_materialGuid{};
	HashedGuid      m_instancedID{};

public:
	Mathf::xMatrix*	  m_finalTransforms{};
	LightMapping	  m_LightMapping;
	bool			  m_isSkinnedMesh{ false };
	bool			  m_isAnimationEnabled{ false };
	bool              m_isInstanced{ false };
	bool              m_isCulled{ false };
	Mathf::xMatrix    m_worldMatrix;

private:
	bool m_isNeedUptateCulling{ false };
};

inline bool SortByAnimationAndMaterialGuid(MeshRendererProxy* a, MeshRendererProxy* b)
{
	if (a->m_animatorGuid == b->m_animatorGuid)
	{
		return a->m_materialGuid < b->m_materialGuid;
	}
	return a->m_animatorGuid < b->m_animatorGuid;
}