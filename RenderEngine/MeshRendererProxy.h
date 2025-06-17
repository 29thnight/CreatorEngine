#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include "LightMapping.h"
#include "Animator.h"
#include "TerrainBuffers.h"

enum class PrimitiveProxyType
{
   MeshRenderer,
   TerrainComponent
};

class Material;
class Mesh;
class OctreeNode;
class MeshRenderer;
class TerrainMesh;
class TerrainComponent;
class PrimitiveRenderProxy
{
public:
	PrimitiveRenderProxy(MeshRenderer* component);
	PrimitiveRenderProxy(TerrainComponent* component);
	~PrimitiveRenderProxy();

	PrimitiveRenderProxy(const PrimitiveRenderProxy& other);
	PrimitiveRenderProxy(PrimitiveRenderProxy&& other) noexcept;

public:
	bool IsNeedUpdateCulling() const { return m_isNeedUptateCulling; }
	void SetNeedUpdateCulling(bool able) { m_isNeedUptateCulling = able; }

	void SetSkinnedMesh(bool isSkinned) { m_isSkinnedMesh = isSkinned; }
	bool IsSkinnedMesh() const { return m_isSkinnedMesh; }

	void Draw();
	void Draw(ID3D11DeviceContext* _defferedContext);

	friend bool SortByAnimationAndMaterialGuid(PrimitiveRenderProxy* a, PrimitiveRenderProxy* b);

	void DistroyProxy();

public:
	//공통
	PrimitiveProxyType	m_proxyType{ PrimitiveProxyType::MeshRenderer };
	Mathf::xMatrix		m_worldMatrix;
	HashedGuid			m_instancedID{};
	bool				m_isCulled{ false };

public:
	//meshRenderer type
	Material*			m_Material{ nullptr };
	Mesh*				m_Mesh{ nullptr };
	HashedGuid			m_animatorGuid{};
	HashedGuid			m_materialGuid{};
	Mathf::xMatrix*		m_finalTransforms{};
	LightMapping		m_LightMapping;
	//TODO : bitflag 처리
	bool				m_isSkinnedMesh{ false };
	bool				m_isAnimationEnabled{ false };
	bool				m_isInstanced{ false };

public:
	//terrain type
	TerrainMesh*		m_terrainMesh{ nullptr };
	TerrainGizmoBuffer  m_terrainGizmoBuffer{};
	TerrainLayerBuffer  m_terrainlayerBuffer{};

private:
	bool				m_isNeedUptateCulling{ false };
};

inline bool SortByAnimationAndMaterialGuid(PrimitiveRenderProxy* a, PrimitiveRenderProxy* b)
{
	if (a->m_animatorGuid == b->m_animatorGuid)
	{
		return a->m_materialGuid < b->m_materialGuid;
	}
	return a->m_animatorGuid < b->m_animatorGuid;
}