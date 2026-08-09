#pragma once
// 그리는 프록시 계층. 예전에는 이 파일에 클래스가 하나뿐이었고
// (`PrimitiveRenderProxy //아 각 타입별로 분리하고 싶다...`) 메시·지형·폴리지·
// 데칼·스프라이트의 필드를 한 클래스가 전부 들고 타입 태그로 갈랐다.
//
// ★ 한 클래스로 겸하던 대가는 셋이었다.
//   ① 어떤 프록시든 모든 필드를 들어 메모리가 최대 타입에 맞춰졌다.
//   ② 어느 필드가 어느 타입의 것인지 주석으로만 표시됐고, 그래서 복사
//      생성자가 여섯 필드를 조용히 빠뜨린 적이 있다.
//   ③ "이 프록시는 지형인가"를 태그로 물은 뒤 필드에 손을 대는 코드가
//      곳곳에 생겼다 — 태그와 필드가 어긋나도 컴파일러가 말해 주지 않는다.
//
// 이제 공통은 RenderProxy(신원·월드 변환)와 PrimitiveRenderProxy(그리는
// 것의 공통)가 들고, 타입별 필드는 파생이 든다. ③은 As<T>()가 받는다 —
// 태그가 맞을 때만 그 타입 포인터를 돌려주므로 태그와 필드가 한 자리에서
// 함께 검사된다.
#include "RenderProxy.h"
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
class MeshRenderer;
class TerrainMesh;
class TerrainMaterial;
class TerrainComponent;
class FoliageComponent;
class DecalComponent;
class SpriteRenderer;
class Texture;
class ShaderPSO;

// 그리는 프록시의 공통 조각. 저장소(RenderScene::m_proxyMap)와 회수 큐를
// 공유하는 단위이기도 하다 — 어느 파생이든 이 타입으로 담긴다.
class PrimitiveRenderProxy : public RenderProxy
{
protected:
	// ★ 파생만 만들 수 있다. 이 클래스를 직접 인스턴스화하면 태그와 실제
	//   타입이 어긋나고, 그러면 As<T>()가 거짓을 말한다 — 태그만 보고
	//   static_cast를 하므로 파생이 아닌 객체를 파생 포인터로 돌려준다.
	//   기본 생성자를 두지 않는 것도 같은 이유다: 어느 파생이든 자기
	//   kProxyType을 명시해 올려야 한다.
	explicit PrimitiveRenderProxy(PrimitiveProxyType type) : m_proxyType(type) {}

public:
	void DestroyProxy() override;

	// 태그가 맞을 때만 그 타입으로 내려본다. 맞지 않으면 널이다.
	//
	// ★ dynamic_cast를 쓰지 않는 이유: 태그(m_proxyType)가 이미 진실이고,
	//   DestroyProxy가 태그를 Expired로 바꾼다. 즉 파괴 통보된 프록시는
	//   여기서 자동으로 널이 되어, 회수 전에 도착한 갱신 커맨드가 죽은
	//   프록시에 값을 쓰는 일이 구조적으로 막힌다. RTTI로는 그 상태를
	//   표현할 수 없다.
	template <typename T>
	T* As()
	{
		static_assert(std::is_base_of_v<PrimitiveRenderProxy, T>,
			"As<T>는 프리미티브 프록시 파생에만 쓴다");
		return (T::kProxyType == m_proxyType) ? static_cast<T*>(this) : nullptr;
	}

	template <typename T>
	const T* As() const
	{
		static_assert(std::is_base_of_v<PrimitiveRenderProxy, T>,
			"As<T>는 프리미티브 프록시 파생에만 쓴다");
		return (T::kProxyType == m_proxyType) ? static_cast<const T*>(this) : nullptr;
	}

public:
	// 기본값이 Expired인 이유: 어떤 경로로든 태그가 설정되지 않은 채
	// 남으면 As<T>()가 전부 널을 돌려주게 한다. 틀린 타입으로 내려보는
	// 것보다 아무것도 안 보이는 쪽이 낫다.
	PrimitiveProxyType				m_proxyType{ PrimitiveProxyType::Expired };
	bool							m_isCulled{ false };
	bool							m_isStatic{ false };
	// 소유 오브젝트의 활성 여부. 예전 이름은 m_isEnableShadow였는데 메시·
	// 스프라이트 양쪽이 owner->IsEnabled()를 넣고 있었고 그림자와는 무관했다.
	bool							m_isEnabled{ true };
};

// ── 메시 ──
class MeshRenderProxy : public PrimitiveRenderProxy
{
public:
	static constexpr PrimitiveProxyType kProxyType = PrimitiveProxyType::MeshRenderer;

	MeshRenderProxy() : PrimitiveRenderProxy(kProxyType) {}
	explicit MeshRenderProxy(MeshRenderer* component);

	bool IsNeedUpdateCulling() const { return m_isNeedUpdateCulling; }
	void SetNeedUpdateCulling(bool able) { m_isNeedUpdateCulling = able; }

	void SetSkinnedMesh(bool isSkinned) { m_isSkinnedMesh = isSkinned; }
	bool IsSkinnedMesh() const { return m_isSkinnedMesh; }

	// DX11 드로우 셋과 LOD 선택 둘은 T5에서 걷었다. 저작 의도인 이 플래그만
	// 남는다(소비자는 DX12가 LOD를 붙일 때 생긴다).
	void SetLODEnabled(bool enable) { m_EnableLOD = enable; }

public:
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
	uint32							m_bitflag{ 0 };

	// 월드 공간 AABB. 뷰별 절두체 컬링이 쓴다(RenderSceneViewPlan ③).
	//
	// ★ m_hasWorldBounds는 "이 상자를 컬링에 믿어도 되는가"다. 스키닝
	//   메시에서는 false다 — 메시의 AABB는 바인드 포즈 것이고 애니메이션이
	//   그 밖으로 정점을 밀 수 있다. 믿고 자르면 팔을 뻗은 캐릭터가 화면
	//   가장자리에서 통째로 사라지는 부류가 된다. 못 자르는 것보다 나쁘다.
	DirectX::BoundingBox			m_worldBounds{};
	bool							m_hasWorldBounds{ false };

	bool							m_isShadowCast{ true };
	bool							m_isShadowRecive{ true };
	bool							m_isSkinnedMesh{ false };
	bool							m_isAnimationEnabled{ false };
	bool							m_isInstanced{ false };
	bool							m_EnableLOD{ false };

private:
	bool							m_isNeedUpdateCulling{ false };
};

// ── 지형 ──
class TerrainRenderProxy : public PrimitiveRenderProxy
{
public:
	static constexpr PrimitiveProxyType kProxyType = PrimitiveProxyType::TerrainComponent;

	TerrainRenderProxy() : PrimitiveRenderProxy(kProxyType) {}
	explicit TerrainRenderProxy(TerrainComponent* component);

public:
	std::shared_ptr<TerrainMesh>	m_terrainMesh{ nullptr };
	TerrainMaterial*				m_terrainMaterial{ nullptr };
	TerrainGizmoBuffer				m_terrainGizmoBuffer{};
	TerrainLayerBuffer				m_terrainlayerBuffer{};
};

// ── 폴리지 ──
class FoliageRenderProxy : public PrimitiveRenderProxy
{
public:
	static constexpr PrimitiveProxyType kProxyType = PrimitiveProxyType::FoliageComponent;

	FoliageRenderProxy() : PrimitiveRenderProxy(kProxyType) {}
	explicit FoliageRenderProxy(FoliageComponent* component);

	// 인스턴스 목록이 바뀔 때마다 타입별 색인을 다시 만든다.
	// 색인이 원소 주소를 들므로 m_foliageInstances를 갈아 끼운 뒤에는
	// 반드시 불러야 한다 — 아니면 옛 벡터의 주소를 가리킨다.
	void RebuildInstanceMap();

public:
	std::vector<FoliageInstance>	m_foliageInstances{};
	std::vector<FoliageType>		m_foliageTypes{};
	std::unordered_map<uint32, std::vector<FoliageInstance*>> instanceMap;
};

// ── 데칼 ──
class DecalRenderProxy : public PrimitiveRenderProxy
{
public:
	static constexpr PrimitiveProxyType kProxyType = PrimitiveProxyType::DecalComponent;

	DecalRenderProxy() : PrimitiveRenderProxy(kProxyType) {}
	explicit DecalRenderProxy(DecalComponent* component);

public:
	Texture*						m_diffuseTexture{};
	Texture*						m_normalTexture{};
	Texture*						m_occluroughmetalTexture{};
	uint32							m_sliceX{ 1 };
	uint32							m_sliceY{ 1 };
	int								m_sliceNum{ 0 };
};

// ── 스프라이트 ──
class SpriteRenderProxy : public PrimitiveRenderProxy
{
public:
	static constexpr PrimitiveProxyType kProxyType = PrimitiveProxyType::SpriteRenderer;

	SpriteRenderProxy() : PrimitiveRenderProxy(kProxyType) {}
	explicit SpriteRenderProxy(SpriteRenderer* component);

public:
	std::shared_ptr<Mesh>           m_quadMesh{ nullptr };
	Texture*						m_spriteTexture{ nullptr };
	std::string						m_customPSOName{};
	std::shared_ptr<ShaderPSO>      m_customPSO{ nullptr };
	BillboardType                   m_billboardType{ BillboardType::None };
	Mathf::Vector3                  m_billboardAxis{ 0.f, 1.f, 0.f };
	bool                            m_enableDepth{ false };
};

// 드로우 큐 정렬. deferred·forward 큐에는 메시 프록시만 들어가므로
// (RenderPassData::PushRenderQueue의 분류가 그렇다) 메시가 아닌 것이
// 섞이면 순서를 정하지 않는다.
inline bool SortByAnimationAndMaterialGuid(PrimitiveRenderProxy* a, PrimitiveRenderProxy* b)
{
	const MeshRenderProxy* lhs = (nullptr != a) ? a->As<MeshRenderProxy>() : nullptr;
	const MeshRenderProxy* rhs = (nullptr != b) ? b->As<MeshRenderProxy>() : nullptr;
	if (nullptr == lhs || nullptr == rhs) return false;

	if (lhs->m_animatorGuid == rhs->m_animatorGuid)
	{
		return lhs->m_materialGuid < rhs->m_materialGuid;
	}
	return lhs->m_animatorGuid < rhs->m_animatorGuid;
}
#endif // !DYNAMICCPP_EXPORTS
