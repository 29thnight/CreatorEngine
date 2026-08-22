#include "ProxyCommand.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "Terrain.h"
#include "FoliageComponent.h"
#include "ImageComponent.h"
#include "SpriteSheetComponent.h"
#include "TextComponent.h"
#include "RenderScene.h"
#include "Material.h"
#include "SpriteRenderer.h"
#include "DecalComponent.h"
#include "LightComponent.h"
#include "LightRenderProxy.h"
#include "Canvas.h"
#include "RectTransformComponent.h"
#include <cstring>

namespace
{
Animator* FindEnabledAnimator(MeshRenderer* component)
{
	if (nullptr == component || nullptr == component->GetOwner()) return nullptr;

	Entity* meshOwner = component->GetOwner();
	Entity::Index ownerIndex = meshOwner->GetParentIndex();
	while (ownerIndex != Entity::INVALID_INDEX)
	{
		Entity* owner = meshOwner->OwnerSceneFindIndex(ownerIndex);
		if (nullptr == owner) break;

		Animator* animator = owner->GetComponent<Animator>();
		if (nullptr != animator && animator->IsEnabled()) return animator;

		ownerIndex = owner->GetParentIndex();
	}

	return nullptr;
}
}

ProxyCommand::ProxyCommand(MeshRenderer* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	// 재질이 없는 메시 갱신은 기존 계약처럼 no-op이다. 등록 프록시에 남아
	// 있는 재질을 지우는 수명 전환은 3-2D에서 create/destroy와 함께 다룬다.
	auto material = component->m_Material;
	if (nullptr == material) return;

	MeshUpdate update{};
	update.worldMatrix = owner->Transform_().GetWorldMatrix();
	update.worldPosition = owner->Transform_().GetWorldPosition();
	update.hasWorldBounds = !component->IsSkinnedMesh() && nullptr != component->m_Mesh;
	if (update.hasWorldBounds)
	{
		update.worldBounds = component->GetBoundingBox();
	}
	update.material = std::move(material);
	update.materialGuid = update.material->m_materialGuid;
	update.lightMapping = component->m_LightMapping;
	update.updateLightMapping = -1 != update.lightMapping.lightmapIndex;
	update.bitflag = component->m_bitflag;
	update.isStatic = owner->IsStatic();
	update.isEnabled = owner->IsEnabled();
	update.isShadowCast = component->m_shadowCast;
	update.isShadowReceive = component->m_shadowRecive;
	update.enableLOD = component->m_isEnableLOD;

	// Animator는 게임 소유 가변 객체다. 렌더 소비 단계가 Animator*를 읽지
	// 않도록 여기서 최종 팔레트를 immutable buffer로 복사한다. 한 명령이
	// 버퍼를 소유하므로 게임/렌더 실행이 겹쳐도 원본 수명에 기대지 않는다.
	if (component->IsSkinnedMesh())
	{
		if (Animator* animator = FindEnabledAnimator(component))
		{
			update.hasAnimator = true;
			update.animatorGuid = animator->GetInstanceID();
			update.bonePalette = std::make_shared<Mathf::xMatrix[]>(MAX_BONES);
			std::memcpy(update.bonePalette.get(), animator->m_FinalTransforms,
				sizeof(animator->m_FinalTransforms));
		}
	}

	m_proxyGUID = component->GetInstanceID();
	m_payload = std::move(update);
}

ProxyCommand::ProxyCommand(SpriteRenderer* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	auto texture = component->GetSprite();
	if (nullptr == texture) return;

	SpriteUpdate update{};
	update.worldMatrix = owner->Transform_().GetWorldMatrix();
	update.worldPosition = owner->Transform_().GetWorldPosition();
	update.texture = std::move(texture);
	update.billboardType = component->GetBillboardType();
	update.billboardAxis = component->GetBillboardAxis();
	update.isStatic = owner->IsStatic();
	update.isEnabled = component->IsEnabled() && owner->IsEnabled();
	update.enableDepth = component->IsEnableDepth();
	update.orderInLayer = component->GetOrderInLayer();

	m_proxyGUID = component->GetInstanceID();
	m_payload = std::move(update);
}

ProxyCommand::ProxyCommand(TerrainComponent* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	TerrainUpdate update{};
	update.worldMatrix = owner->Transform_().GetWorldMatrix();
	update.worldPosition = owner->Transform_().GetWorldPosition();
	update.terrainMesh = component->GetMesh();
	update.terrainMaterial = component->GetMaterialShared();

	m_proxyGUID = component->GetInstanceID();
	m_payload = std::move(update);
}

ProxyCommand::ProxyCommand(FoliageComponent* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	FoliageUpdate update{};
	update.worldMatrix = owner->Transform_().GetWorldMatrix();
	update.worldPosition = owner->Transform_().GetWorldPosition();
	update.foliageTypes = component->GetFoliageTypes();
	update.foliageInstances = component->GetFoliageInstances();

	m_proxyGUID = component->GetInstanceID();
	m_payload = std::move(update);
}

ProxyCommand::ProxyCommand(DecalComponent* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	DecalUpdate update{};
	update.worldMatrix = owner->Transform_().GetWorldMatrix();
	update.diffuse = component->GetDecalTextureShared();
	update.normal = component->GetNormalTextureShared();
	update.orm = component->GetORMTextureShared();
	update.sliceX = component->sliceX;
	update.sliceY = component->sliceY;
	update.sliceNumber = component->sliceNumber;

	m_proxyGUID = component->GetInstanceID();
	m_payload = update;
}

ProxyCommand::ProxyCommand(LightComponent* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	LightUpdate update{};
	update.values = LightRenderProxy::ReadFrom(component);

	m_proxyGUID = component->GetInstanceID();
	m_payload = update;
}

ProxyCommand::ProxyCommand(SpriteSheetComponent* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	SpriteSheetUpdate update{};
	update.data.origin = {
		component->uiinfo.size.x * 0.5f,
		component->uiinfo.size.y * 0.5f
	};
	update.data.position = component->pos;
	update.data.scale = component->scale;
	if (auto* canvas = component->GetOwnerCanvas())
	{
		update.data.canvasOrder = canvas->GetCanvasOrder();
	}
	update.data.layerOrder = component->GetLayerOrder();
	update.data.frameDuration = component->m_frameDuration;
	update.data.isPreview = component->m_isPreview;
	update.data.clipDirection = component->clipDirection;
	update.data.clipPercent = component->clipPercent;
	update.data.deltaTime = owner->IsEnabled() ? component->m_deltaTime : 0.f;
	update.isEnabled = owner->IsEnabled();
	update.isLoop = component->m_isLoop;

	m_proxyGUID = component->GetInstanceID();
	m_payload = std::move(update);
}

ProxyCommand::ProxyCommand(ImageComponent* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	ImageUpdate update{};
	update.data.textures = component->textures;
	update.data.texture = component->m_curtexture;
	update.data.origin = { component->origin.x, component->origin.y };
	update.data.color = component->color;
	update.data.position = component->pos;
	update.data.scale = component->scale;
	update.data.rotation = component->rotate;
	update.data.filpEffect = (SpriteEffects)component->uiEffects;
	if (auto* canvas = component->GetOwnerCanvas())
	{
		update.data.canvasOrder = canvas->GetCanvasOrder();
		update.data.canvasId = canvas->GetInstanceID();
		update.data.renderMode = canvas->GetRenderMode();
		update.data.planeDistance = canvas->GetPlaneDistance();
		if (auto* canvasOwner = canvas->GetOwner())
		{
			update.data.canvasWorld = canvasOwner->Transform_().GetWorldMatrix();
			if (auto* rect = canvasOwner->GetComponent<RectTransformComponent>())
			{
				const auto& root = rect->GetWorldRect();
				update.data.canvasRect = { root.x, root.y, root.width, root.height };
			}
		}
	}
	update.data.layerOrder = component->GetLayerOrder();
	update.data.clipDirection = component->clipDirection;
	update.data.clipPercent = component->clipPercent;
	update.isEnabled = component->IsEnabled() && owner->IsEnabled();

	m_proxyGUID = component->GetInstanceID();
	m_payload = std::move(update);
}

ProxyCommand::ProxyCommand(TextComponent* component, uint64_t sceneEpoch) :
	m_sceneEpoch(sceneEpoch)
{
	if (nullptr == component) return;
	auto* owner = component->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark() || component->IsDestroyMark()) return;

	TextUpdate update{};
	update.data.fontPath = component->GetFontPath();
	update.data.message = component->message;
	update.data.color = component->color;
	update.data.position = { component->pos.x, component->pos.y };
	update.data.fontSize = component->fontSize;
	if (auto* canvas = component->GetOwnerCanvas())
	{
		update.data.canvasOrder = canvas->GetCanvasOrder();
	}
	update.data.layerOrder = component->GetLayerOrder();
	update.data.maxSize = component->stretchSize;
	update.data.stretchX = component->isStretchX;
	update.data.stretchY = component->isStretchY;
	update.data.alignment = component->GetHorizontalAlignment();
	update.isEnabled = owner->IsEnabled();

	// m_textMeasureSize는 현재 UIRenderProxy 어디에서도 0 이외의 값으로
	// 발행되지 않는다. 명령 생산자가 프록시를 역조회해 그 0을 되복사하던
	// 경로는 render -> game readback 계약이 아니었으므로 제거한다.
	m_proxyGUID = component->GetInstanceID();
	m_payload = std::move(update);
}

ProxyCommand ProxyCommand::CreatePrimitive(
	std::shared_ptr<PrimitiveRenderProxy> proxy, uint64_t sceneEpoch)
{
	ProxyCommand command;
	if (nullptr == proxy) return command;
	command.m_proxyGUID = proxy->m_instancedID;
	command.m_sceneEpoch = sceneEpoch;
	command.m_payload = PrimitiveCreate{ std::move(proxy) };
	return command;
}

ProxyCommand ProxyCommand::CreateLight(
	std::shared_ptr<LightRenderProxy> proxy, uint64_t sceneEpoch)
{
	ProxyCommand command;
	if (nullptr == proxy) return command;
	command.m_proxyGUID = proxy->m_instancedID;
	command.m_sceneEpoch = sceneEpoch;
	command.m_payload = LightCreate{ std::move(proxy) };
	return command;
}

ProxyCommand ProxyCommand::CreateUI(
	std::shared_ptr<UIRenderProxy> proxy, uint64_t sceneEpoch)
{
	ProxyCommand command;
	if (nullptr == proxy) return command;
	command.m_proxyGUID = proxy->GetInstanceID();
	command.m_sceneEpoch = sceneEpoch;
	command.m_payload = UICreate{ std::move(proxy) };
	return command;
}

ProxyCommand ProxyCommand::DestroyPrimitive(HashedGuid guid, uint64_t sceneEpoch)
{
	ProxyCommand command;
	command.m_proxyGUID = guid;
	command.m_sceneEpoch = sceneEpoch;
	command.m_payload = PrimitiveDestroy{};
	return command;
}

ProxyCommand ProxyCommand::DestroyLight(HashedGuid guid, uint64_t sceneEpoch)
{
	ProxyCommand command;
	command.m_proxyGUID = guid;
	command.m_sceneEpoch = sceneEpoch;
	command.m_payload = LightDestroy{};
	return command;
}

ProxyCommand ProxyCommand::DestroyUI(HashedGuid guid, uint64_t sceneEpoch)
{
	ProxyCommand command;
	command.m_proxyGUID = guid;
	command.m_sceneEpoch = sceneEpoch;
	command.m_payload = UIDestroy{};
	return command;
}

ProxyCommand::ApplyResult ProxyCommand::Apply(
	RenderScene& renderScene, uint64_t sceneEpoch)
{
	bool applied = false;

	// 다른 씬에서 늦게 도착한 delta는 현재 RenderScene에 절대 적용하지 않는다.
	if (m_sceneEpoch != sceneEpoch)
	{
		m_payload = std::monostate{};
		return ApplyResult::StaleEpoch;
	}

	if (auto* create = std::get_if<PrimitiveCreate>(&m_payload))
	{
		SpinLock lock(renderScene.m_proxyMapFlag);
		applied = nullptr != create->proxy &&
			renderScene.m_proxyMap.try_emplace(m_proxyGUID,
				std::move(create->proxy)).second;
	}
	else if (auto* create = std::get_if<LightCreate>(&m_payload))
	{
		SpinLock lock(renderScene.m_lightProxyMapFlag);
		applied = nullptr != create->proxy &&
			renderScene.m_lightProxyMap.try_emplace(m_proxyGUID,
				std::move(create->proxy)).second;
	}
	else if (auto* create = std::get_if<UICreate>(&m_payload))
	{
		SpinLock lock(renderScene.m_uiProxyMapFlag);
		applied = nullptr != create->proxy &&
			renderScene.m_uiProxyMap.try_emplace(m_proxyGUID,
				std::move(create->proxy)).second;
	}
	else if (std::holds_alternative<PrimitiveDestroy>(m_payload))
	{
		SpinLock lock(renderScene.m_proxyMapFlag);
		applied = 0 != renderScene.m_proxyMap.erase(m_proxyGUID);
	}
	else if (std::holds_alternative<LightDestroy>(m_payload))
	{
		SpinLock lock(renderScene.m_lightProxyMapFlag);
		applied = 0 != renderScene.m_lightProxyMap.erase(m_proxyGUID);
	}
	else if (std::holds_alternative<UIDestroy>(m_payload))
	{
		SpinLock lock(renderScene.m_uiProxyMapFlag);
		applied = 0 != renderScene.m_uiProxyMap.erase(m_proxyGUID);
	}
	else if (auto* update = std::get_if<MeshUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_proxyMapFlag);
		auto it = renderScene.m_proxyMap.find(m_proxyGUID);
		if (it != renderScene.m_proxyMap.end() && nullptr != it->second)
		{
			if (auto* proxy = it->second->As<MeshRenderProxy>())
			{
				proxy->m_worldMatrix = update->worldMatrix;
				proxy->m_worldPosition = update->worldPosition;
				proxy->m_worldBounds = update->worldBounds;
				proxy->m_hasWorldBounds = update->hasWorldBounds;
				proxy->m_isStatic = update->isStatic;
				proxy->m_isEnabled = update->isEnabled;
				proxy->m_isShadowCast = update->isShadowCast;
				proxy->m_isShadowRecive = update->isShadowReceive;
				proxy->m_EnableLOD = update->enableLOD;
				proxy->m_bitflag = update->bitflag;

				if (update->updateLightMapping &&
					proxy->m_LightMapping.lightmapIndex != update->lightMapping.lightmapIndex)
				{
					proxy->m_LightMapping = update->lightMapping;
				}

				if (nullptr != update->material &&
					proxy->m_materialGuid != update->materialGuid)
				{
					proxy->m_Material = update->material;
					proxy->m_materialGuid = update->materialGuid;
				}

				proxy->m_isAnimationEnabled = update->hasAnimator;
				proxy->m_animatorGuid = update->animatorGuid;
				proxy->m_finalTransforms = update->bonePalette;
				applied = true;
			}
		}
	}
	else if (auto* update = std::get_if<TerrainUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_proxyMapFlag);
		auto it = renderScene.m_proxyMap.find(m_proxyGUID);
		if (it != renderScene.m_proxyMap.end() && nullptr != it->second)
		{
			if (auto* proxy = it->second->As<TerrainRenderProxy>())
			{
				proxy->m_worldMatrix = update->worldMatrix;
				proxy->m_worldPosition = update->worldPosition;
				proxy->m_terrainMesh = std::move(update->terrainMesh);
				proxy->m_terrainMaterial = std::move(update->terrainMaterial);
				applied = true;
			}
		}
	}
	else if (auto* update = std::get_if<FoliageUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_proxyMapFlag);
		auto it = renderScene.m_proxyMap.find(m_proxyGUID);
		if (it != renderScene.m_proxyMap.end() && nullptr != it->second)
		{
			if (auto* proxy = it->second->As<FoliageRenderProxy>())
			{
				proxy->m_foliageTypes = std::move(update->foliageTypes);
				proxy->m_foliageInstances = std::move(update->foliageInstances);
				proxy->m_worldMatrix = update->worldMatrix;
				proxy->m_worldPosition = update->worldPosition;
				proxy->RebuildInstanceMap();
				applied = true;
			}
		}
	}
	else if (auto* update = std::get_if<DecalUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_proxyMapFlag);
		auto it = renderScene.m_proxyMap.find(m_proxyGUID);
		if (it != renderScene.m_proxyMap.end() && nullptr != it->second)
		{
			if (auto* proxy = it->second->As<DecalRenderProxy>())
			{
				proxy->m_diffuseTexture = update->diffuse;
				proxy->m_normalTexture = update->normal;
				proxy->m_occluroughmetalTexture = update->orm;
				proxy->m_worldMatrix = update->worldMatrix;
				proxy->m_sliceX = update->sliceX;
				proxy->m_sliceY = update->sliceY;
				proxy->m_sliceNum = update->sliceNumber;
				applied = true;
			}
		}
	}
	else if (auto* update = std::get_if<SpriteUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_proxyMapFlag);
		auto it = renderScene.m_proxyMap.find(m_proxyGUID);
		if (it != renderScene.m_proxyMap.end() && nullptr != it->second)
		{
			if (auto* proxy = it->second->As<SpriteRenderProxy>())
			{
				proxy->m_worldMatrix = update->worldMatrix;
				proxy->m_worldPosition = update->worldPosition;
				proxy->m_isStatic = update->isStatic;
				proxy->m_isEnabled = update->isEnabled;
				proxy->m_spriteTexture = std::move(update->texture);
				proxy->m_billboardType = update->billboardType;
				proxy->m_billboardAxis = update->billboardAxis;
				proxy->m_enableDepth = update->enableDepth;
				proxy->m_orderInLayer = update->orderInLayer;
				applied = true;
			}
		}
	}
	else if (auto* update = std::get_if<LightUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_lightProxyMapFlag);
		auto it = renderScene.m_lightProxyMap.find(m_proxyGUID);
		if (it != renderScene.m_lightProxyMap.end() && nullptr != it->second)
		{
			it->second->Apply(update->values);
			applied = true;
		}
	}
	else if (auto* update = std::get_if<ImageUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_uiProxyMapFlag);
		auto it = renderScene.m_uiProxyMap.find(m_proxyGUID);
		if (it != renderScene.m_uiProxyMap.end() && nullptr != it->second)
		{
			it->second->m_data = std::move(update->data);
			it->second->m_isEnabled = update->isEnabled;
			applied = true;
		}
	}
	else if (auto* update = std::get_if<TextUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_uiProxyMapFlag);
		auto it = renderScene.m_uiProxyMap.find(m_proxyGUID);
		if (it != renderScene.m_uiProxyMap.end() && nullptr != it->second)
		{
			it->second->m_data = std::move(update->data);
			it->second->m_isEnabled = update->isEnabled;
			applied = true;
		}
	}
	else if (auto* update = std::get_if<SpriteSheetUpdate>(&m_payload))
	{
		SpinLock lock(renderScene.m_uiProxyMapFlag);
		auto it = renderScene.m_uiProxyMap.find(m_proxyGUID);
		if (it != renderScene.m_uiProxyMap.end() && nullptr != it->second)
		{
			auto& proxy = *it->second;
			if (!update->isEnabled)
			{
				proxy.m_sequenceState.frameIndex = 0;
				proxy.m_sequenceState.timeAccum = 0.f;
			}
			proxy.m_sequenceState.loop = update->isLoop;
			proxy.m_data = std::move(update->data);
			proxy.m_isEnabled = update->isEnabled;
			applied = true;
		}
	}

	// 큐의 명령은 one-shot이다. drop된 경우에도 다시 적용하지 않는다.
	m_payload = std::monostate{};
	return applied ? ApplyResult::Applied : ApplyResult::MissingTarget;
}
