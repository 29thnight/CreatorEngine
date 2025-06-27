#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include "LightMapping.h"
#include "Animator.h"
#include "TerrainBuffers.h"
#include "LODGroup.h"

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
class TerrainMaterial;
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
	bool IsNeedUpdateCulling() const { return m_isNeedUpdateCulling; }
	void SetNeedUpdateCulling(bool able) { m_isNeedUpdateCulling = able; }

	void SetSkinnedMesh(bool isSkinned) { m_isSkinnedMesh = isSkinned; }
	bool IsSkinnedMesh() const { return m_isSkinnedMesh; }

	void Draw();
	void Draw(ID3D11DeviceContext* _deferredContext);

	void DrawShadow();
	void DrawShadow(ID3D11DeviceContext* _deferredContext);

	friend bool SortByAnimationAndMaterialGuid(PrimitiveRenderProxy* a, PrimitiveRenderProxy* b);

	void DistroyProxy();

	void GenerateLODGroup();

public:
	//공통
	PrimitiveProxyType	m_proxyType{ PrimitiveProxyType::MeshRenderer };
	Mathf::Vector3		m_worldPosition{ 0.0f, 0.0f, 0.0f };
	Mathf::xMatrix		m_worldMatrix{ XMMatrixIdentity() };
	HashedGuid			m_instancedID{};
	bool				m_isCulled{ false };

public:
	//meshRenderer type
	Material*					m_Material{ nullptr };
	Mesh*						m_Mesh{ nullptr };
	std::unique_ptr<LODGroup>	m_LODGroup{ nullptr };
	HashedGuid					m_animatorGuid{};
	HashedGuid					m_materialGuid{};
	Mathf::xMatrix*				m_finalTransforms{};
	LightMapping				m_LightMapping;
	//TODO : bitflag 처리
	bool						m_isSkinnedMesh{ false };
	bool						m_isAnimationEnabled{ false };
	bool						m_isInstanced{ false };

	bool						m_EnableLOD{ false };
	float						m_LODReductionRatio = 0.5f;
	size_t						m_MaxLODLevels = 3;
	float						m_LODDistance = 0.0f;

public:
	//terrain type
	TerrainMesh*				m_terrainMesh{ nullptr };
	TerrainMaterial*			m_terrainMaterial{ nullptr };
	TerrainGizmoBuffer			m_terrainGizmoBuffer{};
	TerrainLayerBuffer			m_terrainlayerBuffer{};
	
private:
	bool						m_isNeedUpdateCulling{ false };
};

inline bool SortByAnimationAndMaterialGuid(PrimitiveRenderProxy* a, PrimitiveRenderProxy* b)
{
	if (a->m_animatorGuid == b->m_animatorGuid)
	{
		return a->m_materialGuid < b->m_materialGuid;
	}
	return a->m_animatorGuid < b->m_animatorGuid;
}