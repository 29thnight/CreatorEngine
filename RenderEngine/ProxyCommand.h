#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "PrimitiveRenderProxy.h"
#include "LightRenderProxy.h"
#include "UIRenderProxy.h"
#include <variant>

class FoliageComponent;
class TerrainComponent;
class SpriteRenderer;
class ImageComponent;
class TextComponent;
class MeshRenderer;
class DecalComponent;
class SpriteSheetComponent;
class LightComponent;
class RenderScene;

// 게임 스레드가 캡처하고 렌더 스레드가 적용하는 RenderScene delta.
//
// std::function 안에 프록시 shared_ptr/weak_ptr를 숨겨 운반하지 않는다.
// payload는 컴포넌트에서 읽은 값만 소유하고, 대상 프록시는 소비 시점에
// RenderScene이 GUID로 해석한다. 따라서 생산자는 RenderScene 맵/락을
// 건드리지 않고, 소비자는 이미 파괴된 대상을 안전하게 drop할 수 있다.
class ProxyCommand
{
public:
	enum class ApplyResult : uint8_t
	{
		Applied,
		StaleEpoch,
		MissingTarget,
	};

	struct PrimitiveCreate
	{
		std::shared_ptr<PrimitiveRenderProxy> proxy{};
	};

	struct LightCreate
	{
		std::shared_ptr<LightRenderProxy> proxy{};
	};

	struct UICreate
	{
		std::shared_ptr<UIRenderProxy> proxy{};
	};

	struct PrimitiveDestroy {};
	struct LightDestroy {};
	struct UIDestroy {};

	struct MeshUpdate
	{
		Mathf::xMatrix worldMatrix{ XMMatrixIdentity() };
		Mathf::Vector3 worldPosition{};
		DirectX::BoundingBox worldBounds{};
		std::shared_ptr<Material> material{};
		std::shared_ptr<Mathf::xMatrix[]> bonePalette{};
		HashedGuid animatorGuid{};
		HashedGuid materialGuid{};
		LightMapping lightMapping{};
		uint32 bitflag{ 0 };
		bool hasWorldBounds{ false };
		bool isStatic{ false };
		bool isEnabled{ false };
		bool isShadowCast{ true };
		bool isShadowReceive{ true };
		bool enableLOD{ false };
		bool updateLightMapping{ false };
		bool hasAnimator{ false };
	};

	struct TerrainUpdate
	{
		Mathf::xMatrix worldMatrix{ XMMatrixIdentity() };
		Mathf::Vector3 worldPosition{};
		std::shared_ptr<TerrainMesh> terrainMesh{};
		std::shared_ptr<TerrainMaterial> terrainMaterial{};
	};

	struct FoliageUpdate
	{
		Mathf::xMatrix worldMatrix{ XMMatrixIdentity() };
		Mathf::Vector3 worldPosition{};
		std::vector<FoliageType> foliageTypes{};
		std::vector<FoliageInstance> foliageInstances{};
	};

	struct DecalUpdate
	{
		Mathf::xMatrix worldMatrix{ XMMatrixIdentity() };
		std::shared_ptr<Texture> diffuse{};
		std::shared_ptr<Texture> normal{};
		std::shared_ptr<Texture> orm{};
		uint32 sliceX{ 1 };
		uint32 sliceY{ 1 };
		int sliceNumber{ 0 };
	};

	struct SpriteUpdate
	{
		Mathf::xMatrix worldMatrix{ XMMatrixIdentity() };
		Mathf::Vector3 worldPosition{};
		std::shared_ptr<Texture> texture{};
		Mathf::Vector3 billboardAxis{ 0.f, 1.f, 0.f };
		BillboardType billboardType{ BillboardType::None };
		bool isStatic{ false };
		bool isEnabled{ false };
		bool enableDepth{ false };
	};

	struct LightUpdate
	{
		LightRenderProxy::Values values{};
	};

	struct ImageUpdate
	{
		UIRenderProxy::ImageData data{};
		bool isEnabled{ false };
	};

	struct TextUpdate
	{
		UIRenderProxy::TextData data{};
		bool isEnabled{ false };
	};

	struct SpriteSheetUpdate
	{
		UIRenderProxy::SpriteSheetData data{};
		bool isEnabled{ false };
		bool isLoop{ false };
	};

	using Payload = std::variant<std::monostate,
		PrimitiveCreate, LightCreate, UICreate,
		PrimitiveDestroy, LightDestroy, UIDestroy,
		MeshUpdate, TerrainUpdate, FoliageUpdate, DecalUpdate, SpriteUpdate,
		LightUpdate, ImageUpdate, TextUpdate, SpriteSheetUpdate>;

    ProxyCommand() = default;
    ~ProxyCommand() = default;

    ProxyCommand(MeshRenderer* pComponent, uint64_t sceneEpoch);
    ProxyCommand(LightComponent* pComponent, uint64_t sceneEpoch);
	ProxyCommand(TerrainComponent* pComponent, uint64_t sceneEpoch);
    ProxyCommand(FoliageComponent* pComponent, uint64_t sceneEpoch);
	ProxyCommand(ImageComponent* pComponent, uint64_t sceneEpoch);
	ProxyCommand(TextComponent* pComponent, uint64_t sceneEpoch);
    ProxyCommand(DecalComponent* pComponent, uint64_t sceneEpoch);
	ProxyCommand(SpriteSheetComponent* pComponent, uint64_t sceneEpoch);
	ProxyCommand(SpriteRenderer* pComponent, uint64_t sceneEpoch);
	ProxyCommand(const ProxyCommand&) = default;
	ProxyCommand(ProxyCommand&&) noexcept = default;
	ProxyCommand& operator=(const ProxyCommand&) = default;
	ProxyCommand& operator=(ProxyCommand&&) noexcept = default;

public:
	static ProxyCommand CreatePrimitive(
		std::shared_ptr<PrimitiveRenderProxy> proxy, uint64_t sceneEpoch);
	static ProxyCommand CreateLight(
		std::shared_ptr<LightRenderProxy> proxy, uint64_t sceneEpoch);
	static ProxyCommand CreateUI(
		std::shared_ptr<UIRenderProxy> proxy, uint64_t sceneEpoch);
	static ProxyCommand DestroyPrimitive(HashedGuid guid, uint64_t sceneEpoch);
	static ProxyCommand DestroyLight(HashedGuid guid, uint64_t sceneEpoch);
	static ProxyCommand DestroyUI(HashedGuid guid, uint64_t sceneEpoch);

	bool IsValid() const noexcept
	{
		return !std::holds_alternative<std::monostate>(m_payload);
	}

	// 단 한 번 소비한다. 대상이 이미 해제되었거나 타입이 다르면 false로
	// drop하고 payload를 비운다.
	ApplyResult Apply(RenderScene& renderScene, uint64_t sceneEpoch);
	uint64_t GetSceneEpoch() const noexcept { return m_sceneEpoch; }

	// bounded RenderThread queue가 같은 프록시의 연속 update를 latest-wins로
	// 접을 때 쓰는 값 정체성이다. create/destroy는 절대 접지 않는다.
	bool IsUpdate() const noexcept
	{
		// variant 선언 순서에서 update payload의 첫 슬롯은 MeshUpdate(7)다.
		return m_payload.index() >= 7u;
	}
	size_t GetProxyGuidValue() const noexcept { return m_proxyGUID.m_ID_Data; }
	size_t GetPayloadKind() const noexcept { return m_payload.index(); }

private:
	HashedGuid	m_proxyGUID{};
	uint64_t	m_sceneEpoch{ 0 };
	Payload		m_payload{};
};
#endif // !DYNAMICCPP_EXPORTS
