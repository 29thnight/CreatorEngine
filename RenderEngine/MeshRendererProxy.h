#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include "LightMapping.h"
#include "Animator.h"
#include "BillboardType.h"
#ifndef DYNAMICCPP_EXPORTS
#include "TerrainBuffers.h"
#include "FoliageType.h"
#include "FoliageInstance.h"

enum class PrimitiveProxyType
{
   MeshRenderer,
   FoliageComponent,
   TerrainComponent,
   DecalComponent,
   SpriteRenderer,
   Expired,
};

class Material;
class Mesh;
class OctreeNode;
class MeshRenderer;
class TerrainMesh;
class TerrainMaterial;
class TerrainComponent;
class FoliageComponent;
class DecalComponent;
class SpriteRenderer;
class Texture;
class ShaderPSO;
class PrimitiveRenderProxy //아 각 타입별로 분리하고 싶다...
{
public:
	struct ProxyFilter
	{
		HashedGuid animatorGuid{};
		HashedGuid materialGuid{};
		bool       LODEnabled{ false };
		uint32	   LODLevel{ 0 };
		uint32	   bitflag{ 0 };

		ProxyFilter(size_t animatorGuid, size_t materialGuid, bool LODEnabled, uint32 LODLevel, uint32 bitflag)
			: animatorGuid(animatorGuid), materialGuid(materialGuid), LODEnabled(LODEnabled), LODLevel(LODLevel), bitflag(bitflag) {
		}

		auto operator<=>(const ProxyFilter& other) const = default;
	};
public:
	PrimitiveRenderProxy(MeshRenderer* component);
    PrimitiveRenderProxy(FoliageComponent* component);
	PrimitiveRenderProxy(TerrainComponent* component);
	PrimitiveRenderProxy(DecalComponent* component);
	PrimitiveRenderProxy(SpriteRenderer* component);
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
	PrimitiveProxyType				m_proxyType{ PrimitiveProxyType::MeshRenderer };
	Mathf::Vector3					m_worldPosition{ 0.0f, 0.0f, 0.0f };
	Mathf::xMatrix					m_worldMatrix{ XMMatrixIdentity() };
	HashedGuid						m_instancedID{};
	bool							m_isCulled{ false };
	bool							m_isStatic{ false };

public:
	//meshRenderer type
	// 렌더 스레드가 사용하는 동안 에셋 수명을 보장한다(12.3-⑦).
	// 컴포넌트에서 스냅샷할 때 shared_ptr을 그대로 복사하므로,
	// 원본이 파괴되거나 언로드되어도 이 프록시가 그리는 중에는 안전하다.
	std::shared_ptr<Material>		m_Material{};
	std::shared_ptr<Mesh>			m_Mesh{};
	HashedGuid						m_animatorGuid{};
	HashedGuid						m_materialGuid{};
	// 본 팔레트 버퍼(소유권 공유).
	// RenderScene::AnimationPalette와 같은 버퍼를 가리키며, 애니메이터가 해제되어도
	// 이 프록시가 참조하는 동안에는 버퍼가 살아 있다.
	std::shared_ptr<Mathf::xMatrix[]>	m_finalTransforms{};
	LightMapping					m_LightMapping;
	uint32							m_currLOD{ 0 };
	uint32							m_bitflag{ 0 };

	bool							m_isEnableShadow{ true };
	bool							m_isShadowCast{ true };
	bool							m_isShadowRecive{ true };
	bool							m_isSkinnedMesh{ false };
	bool							m_isAnimationEnabled{ false };
	bool							m_isInstanced{ false };
	bool							m_EnableLOD{ false };

public:
	//terrain type
	std::shared_ptr<TerrainMesh>	m_terrainMesh{ nullptr };
	TerrainMaterial*				m_terrainMaterial{ nullptr };
	TerrainGizmoBuffer				m_terrainGizmoBuffer{};
	TerrainLayerBuffer				m_terrainlayerBuffer{};

public:
	//foliage type
	std::vector<FoliageInstance>	m_foliageInstances{};
	std::vector<FoliageType>		m_foliageTypes{};
	std::unordered_map<uint32, std::vector<FoliageInstance*>> instanceMap;

public:
	//decal type
	Texture*						m_diffuseTexture{};
	Texture*						m_normalTexture{};
	Texture*						m_occluroughmetalTexture{};
	uint32							m_sliceX{ 1 };	
	uint32							m_sliceY{ 1 };	
	int								m_sliceNum{ 0 };

public:
	//sprite type
	std::shared_ptr<Mesh>           m_quadMesh{ nullptr };
	Texture*						m_spriteTexture{ nullptr };
    std::string						m_customPSOName{};
    std::shared_ptr<ShaderPSO>      m_customPSO{ nullptr };
    BillboardType                   m_billboardType{ BillboardType::None };
    Mathf::Vector3                  m_billboardAxis{ 0.f, 1.f, 0.f };
	bool                            m_enableDepth{ false };

private:
	bool							m_isNeedUpdateCulling{ false };
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