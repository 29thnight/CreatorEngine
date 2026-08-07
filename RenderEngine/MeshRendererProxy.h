#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include "LightMapping.h"
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

	// ── 복사·이동은 컴파일러에게 맡긴다 ──
	//
	// 예전에는 둘 다 초기화 목록에 멤버를 손으로 나열했다(각 40여 줄). 그
	// 방식은 여섯 필드를 빠뜨리고 있었다 — m_currLOD · m_terrainGizmoBuffer ·
	// m_terrainlayerBuffer · m_foliageTypes · instanceMap · m_enableDepth.
	// 복사·이동 양쪽 모두 같은 여섯이다.
	//
	// 이 클래스가 여섯 종류의 프록시(메시·지형·폴리지·데칼·스프라이트)를
	// 겸하는 탓에 필드가 많고, 손으로 적는 한 다음에 필드를 더해도 또
	// 빠진다 — 컴파일러가 말해 주지 않기 때문이다. default면 누락 자체가
	// 구조적으로 불가능하다.
	//
	// ★ default로 바꿀 수 있는 근거: 소멸자가 비어 있다. 이 프록시는
	//   shared_ptr만 소유하고 raw 포인터(Texture*·TerrainMaterial*)는
	//   관찰만 한다. 그래서 손으로 쓴 복사가 기본 복사보다 나은 점이
	//   하나도 없었다 — 빠뜨릴 기회만 만들었다.
	//
	// ★ 이동에서 noexcept를 뗀 이유: instanceMap(std::unordered_map)의 이동이
	//   noexcept가 아니라, `noexcept = default`로 적으면 그 불일치 때문에
	//   함수가 삭제된다. 프록시는 어디서도 값으로 담기지 않으므로
	//   (보관이 전부 포인터·shared_ptr) noexcept 여부가 걸리는 자리가 없다.
	//
	// 지금 이 둘을 부르는 코드는 없다. 잠재 함정을 닫아 두는 것이 목적이다.
	PrimitiveRenderProxy(const PrimitiveRenderProxy& other) = default;
	PrimitiveRenderProxy(PrimitiveRenderProxy&& other) = default;

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