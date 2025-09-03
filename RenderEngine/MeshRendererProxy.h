#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include "LightMapping.h"
#include "Animator.h"
#ifndef DYNAMICCPP_EXPORTS
#include "TerrainBuffers.h"
#include "FoliageType.h"
#include "FoliageInstance.h"

enum class PrimitiveProxyType
{
   MeshRenderer,
   FoliageComponent,
   TerrainComponent
};

class Material;
class Mesh;
class OctreeNode;
class MeshRenderer;
class TerrainMesh;
class TerrainMaterial;
class TerrainComponent;
class FoliageComponent;
class PrimitiveRenderProxy
{
public:
	struct ProxyFilter
	{
		HashedGuid animatorGuid{};
		HashedGuid materialGuid{};
		bool       LODEnabled{ false };
		uint32	   LODLevel{ 0 };

		ProxyFilter(size_t animatorGuid, size_t materialGuid, bool LODEnabled, uint32 LODLevel)
			: animatorGuid(animatorGuid), materialGuid(materialGuid), LODEnabled(LODEnabled), LODLevel(LODLevel) {
		}

		auto operator<=>(const ProxyFilter& other) const = default;
	};
public:
	PrimitiveRenderProxy(MeshRenderer* component);
    PrimitiveRenderProxy(FoliageComponent* component);
	PrimitiveRenderProxy(TerrainComponent* component);
	~PrimitiveRenderProxy();

	PrimitiveRenderProxy(const PrimitiveRenderProxy& other);
	PrimitiveRenderProxy(PrimitiveRenderProxy&& other) noexcept;

public:
	bool IsNeedUpdateCulling() const { return m_isNeedUpdateCulling; }
	void SetNeedUpdateCulling(bool able) { m_isNeedUpdateCulling = able; }

	void SetSkinnedMesh(bool isSkinned) { m_isSkinnedMesh = isSkinned; }
	bool IsSkinnedMesh() const { return m_isSkinnedMesh; }

	void Draw(ID3D11DeviceContext* _deferredContext);
	void DrawShadow(ID3D11DeviceContext* _deferredContext);
	void DrawInstanced(ID3D11DeviceContext* _deferredContext, size_t instanceCount);

	friend bool SortByAnimationAndMaterialGuid(PrimitiveRenderProxy* a, PrimitiveRenderProxy* b);

	void DestroyProxy();

	void InitializeLODs(const std::vector<float>& lodScreenSpaceThresholds);
	void SetLODEnabled(bool enable) { m_EnableLOD = enable; }
	uint32_t GetLODLevel(Camera* camera);

public:
	// Common properties
	PrimitiveProxyType	m_proxyType{ PrimitiveProxyType::MeshRenderer };
	Mathf::Vector3		m_worldPosition{ 0.0f, 0.0f, 0.0f };
	Mathf::xMatrix		m_worldMatrix{ XMMatrixIdentity() };
	HashedGuid			m_instancedID{};
	bool				m_isCulled{ false };
	bool				m_isStatic{ false };

public:
	//meshRenderer type
	Material*					m_Material{ nullptr };
	Mesh*						m_Mesh{ nullptr };
	HashedGuid					m_animatorGuid{};
	HashedGuid					m_materialGuid{};
	Mathf::xMatrix*				m_finalTransforms{};
	LightMapping				m_LightMapping;
	uint32						m_currLOD{ 0 };

	bool                        m_isEnableShadow{ true };
	bool						m_isShadowCast{ true };
	bool						m_isShadowRecive{ true };
	bool						m_isSkinnedMesh{ false };
	bool						m_isAnimationEnabled{ false };
	bool						m_isInstanced{ false };
	bool						m_EnableLOD{ false };

public:
	//terrain type
	std::shared_ptr<TerrainMesh> m_terrainMesh{ nullptr };
	TerrainMaterial*			m_terrainMaterial{ nullptr };
	TerrainGizmoBuffer			m_terrainGizmoBuffer{};
	TerrainLayerBuffer			m_terrainlayerBuffer{};

public:
	//foliage type
	std::vector<FoliageInstance>	m_foliageInstances{};
	std::vector<FoliageType>		m_foliageTypes{};

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
#endif // !DYNAMICCPP_EXPORTS