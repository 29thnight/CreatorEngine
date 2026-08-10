#include "ProxyCommand.h"
#include "Animator.h"
#include "MeshRenderer.h"
#include "Terrain.h"
#include "FoliageComponent.h"
#include "ImageComponent.h"
#include "SpriteSheetComponent.h"
#include "TextComponent.h"
#include "RenderScene.h"
#include "SceneManager.h"
#include "Material.h"
#include "SpriteRenderer.h"
#include "DecalComponent.h"
#include "LightComponent.h"
#include "LightRenderProxy.h"
#include <execution>

// 맵에서 꺼낸 프록시를 파생 타입 shared_ptr로 좁힌다.
//
// 태그가 맞지 않으면(다른 타입이거나 이미 파괴 통보된 Expired) 빈 것을
// 돌려준다 — 갱신 커맨드가 죽은 프록시에 값을 쓰지 못하게 하는 자리다.
template <typename T>
static std::shared_ptr<T> NarrowProxy(const std::shared_ptr<PrimitiveRenderProxy>& proxy)
{
	if (nullptr == proxy || nullptr == proxy->As<T>()) return {};

	return std::static_pointer_cast<T>(proxy);
}

constexpr size_t TRANSFORM_SIZE = sizeof(Mathf::xMatrix) * MAX_BONES;

ProxyCommand::ProxyCommand(MeshRenderer* pComponent) :
	m_proxyGUID(pComponent->GetInstanceID())
{
	auto renderScene				= SceneManagers->GetRenderScene();
	auto componentPtr				= pComponent;
	auto owner						= componentPtr->GetOwner();
	bool isStatic					= owner->IsStatic();
	bool isEnabled					= owner->IsEnabled();
	bool isShadowCast				= pComponent->m_shadowCast;
	bool isShadowRecive				= pComponent->m_shadowRecive;
	Mathf::xMatrix worldMatrix		= owner->m_transform.GetWorldMatrix();
	Mathf::Vector3 worldPosition	= owner->m_transform.GetWorldPosition();
	// 컬링용 월드 AABB도 여기서 뜬다. 월드 행렬이 바뀌면 상자도 바뀌므로
	// 생성 때 한 번 담고 마는 값이 아니다. 컴포넌트 읽기는 게임 스레드인
	// 이 자리에서 끝내고, 람다에는 결과만 실어 보낸다.
	bool hasWorldBounds				= (!pComponent->IsSkinnedMesh() && nullptr != pComponent->m_Mesh);
	DirectX::BoundingBox worldBounds{};
	if (hasWorldBounds)
	{
		worldBounds = pComponent->GetBoundingBox();
	}
	// 람다로 값 캡처되어 렌더 스레드까지 전달되므로 shared_ptr을 유지한다.
	// 전달 도중 원본이 교체·해제되어도 이 커맨드가 참조하는 머티리얼은 살아 있다.
	auto originMat					= pComponent->m_Material;

	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;

	if (nullptr == originMat) 
	{
		m_updateFunction = [=]
		{
			// If the material is null, we do not need to update anything.
		};
		return;
	}

	// 이 생성자는 Scene::UpdateRenderData가 부른다.
	// RenderScene의 Register/Unregister 경로와 같은 락을 잡지 않으면
	// unordered_map 리해시와 겹쳐 힙이 손상된다.
	SpinLock lock(renderScene->m_proxyMapFlag);

	auto proxyObject				= NarrowProxy<MeshRenderProxy>(renderScene->m_proxyMap[m_proxyGUID]);
	if (!proxyObject) return;

	HashedGuid aniGuid				= proxyObject->m_animatorGuid;
	HashedGuid matGuid				= proxyObject->m_materialGuid;
	HashedGuid originMatGuid		= pComponent->m_Material->m_materialGuid;
	bool isEnableLOD				= pComponent->m_isEnableLOD;
	uint32 bitflag					= pComponent->m_bitflag;

	// 람다에 값으로 캡처되어 렌더 스레드까지 전달된다.
	// shared_ptr이므로 캡처된 동안 버퍼 수명이 보장된다.
	std::shared_ptr<Mathf::xMatrix[]> palletePtr{};
	bool isAnimationUpdate{ false };
	bool isMatChange{ false };

	if (matGuid != originMatGuid && originMat)
	{
		isMatChange = true;
	}

	if (renderScene->m_animatorMap.find(aniGuid) != renderScene->m_animatorMap.end()
		&& proxyObject->IsSkinnedMesh())
	{
		palletePtr = renderScene->m_palleteMap[aniGuid].second;
		if (!proxyObject->m_finalTransforms)
		{
			proxyObject->m_finalTransforms = palletePtr;
		}

		if (false == renderScene->m_palleteMap[aniGuid].first)
		{
			auto* srcPalete = &renderScene->m_animatorMap[aniGuid]->m_FinalTransforms;

			memcpy(palletePtr.get(), srcPalete, TRANSFORM_SIZE);
		}

		renderScene->m_palleteMap[aniGuid].first = true;
		isAnimationUpdate = true;
	}

	constexpr int INVAILD_INDEX = -1;

	bool isLightMappingUpdatable{ false };
	LightMapping copyLightMapping{};
	int& lightMapIndex = pComponent->m_LightMapping.lightmapIndex;
	int& proxyLightMapIndex = proxyObject->m_LightMapping.lightmapIndex;

	if (INVAILD_INDEX != lightMapIndex && proxyLightMapIndex != lightMapIndex)
	{
		copyLightMapping = pComponent->m_LightMapping;
		isLightMappingUpdatable = true;
	}

	m_updateFunction = [=]
	{
		if(isAnimationUpdate && palletePtr)
		{
			proxyObject->m_finalTransforms = palletePtr;
		}

		proxyObject->m_worldMatrix		= worldMatrix;
		proxyObject->m_worldPosition	= worldPosition;
		proxyObject->m_worldBounds		= worldBounds;
		proxyObject->m_hasWorldBounds	= hasWorldBounds;
		proxyObject->m_isStatic			= isStatic;
		proxyObject->m_isEnabled		= isEnabled;
		proxyObject->m_isShadowCast		= isShadowCast;
		proxyObject->m_isShadowRecive	= isShadowRecive;
		proxyObject->m_EnableLOD		= isEnableLOD;
		proxyObject->m_bitflag			= bitflag;

		if(isLightMappingUpdatable)
		{
			proxyObject->m_LightMapping = copyLightMapping;
		}

		if (isMatChange)
		{
			proxyObject->m_Material = originMat;
			proxyObject->m_materialGuid = originMatGuid;
		}
		//proxyObject->m_Material->UpdateCBufferView();
	};
}

ProxyCommand::ProxyCommand(SpriteRenderer* pComponent)
{
	m_proxyGUID = pComponent->GetInstanceID();
	auto renderScene = SceneManagers->GetRenderScene();
	auto componentPtr = pComponent;
	auto owner = componentPtr->GetOwner();
	bool isStatic = owner->IsStatic();
	bool isEnabled = owner->IsEnabled();
	Mathf::xMatrix worldMatrix = owner->m_transform.GetWorldMatrix();
	Mathf::Vector3 worldPosition = owner->m_transform.GetWorldPosition();
    BillboardType billboardType = componentPtr->GetBillboardType();
    auto billboardAxis = componentPtr->GetBillboardAxis();
	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;

	SpinLock lock(renderScene->m_proxyMapFlag);

	auto proxyObject = NarrowProxy<SpriteRenderProxy>(renderScene->m_proxyMap[m_proxyGUID]);
	if (!proxyObject) return;
	Texture* originTexture = pComponent->GetSprite().get();
	bool isEnableDepth = pComponent->IsEnableDepth();
	if (!originTexture)
	{
		m_updateFunction = [=]
		{
			// If the texture is null, we do not need to update anything.
		};
		return;
	}

	m_updateFunction = [=]()
	{
		proxyObject->m_worldMatrix = worldMatrix;
		proxyObject->m_worldPosition = worldPosition;
		proxyObject->m_isStatic = isStatic;
		proxyObject->m_isEnabled = isEnabled;
        proxyObject->m_spriteTexture = originTexture;
        proxyObject->m_billboardType = billboardType;
        proxyObject->m_billboardAxis = billboardAxis;
		proxyObject->m_enableDepth = isEnableDepth;
    };
}


ProxyCommand::ProxyCommand(TerrainComponent* pComponent)
{
	m_proxyGUID = pComponent->GetInstanceID();
	auto renderScene = SceneManagers->GetRenderScene();
	auto owner = pComponent->GetOwner();
	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;
	Mathf::xMatrix worldMatrix = owner->m_transform.GetWorldMatrix();
	Mathf::Vector3 worldPosition = owner->m_transform.GetWorldPosition();
	auto terrainMesh = pComponent->GetMesh();

	SpinLock lock(renderScene->m_proxyMapFlag);

	auto proxyObject = NarrowProxy<TerrainRenderProxy>(renderScene->m_proxyMap[m_proxyGUID]);
	if (!proxyObject) return;

	m_updateFunction = [=]()
	{
		proxyObject->m_worldMatrix = worldMatrix;
		proxyObject->m_worldPosition = worldPosition;
		proxyObject->m_terrainMesh = terrainMesh;
	};
}

ProxyCommand::ProxyCommand(FoliageComponent* pComponent) :
	m_proxyGUID(pComponent->GetInstanceID())
{
	auto renderScene = SceneManagers->GetRenderScene();
	auto owner = pComponent->GetOwner();
	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;
	Mathf::xMatrix worldMatrix = owner->m_transform.GetWorldMatrix();
	Mathf::Vector3 worldPosition = owner->m_transform.GetWorldPosition();

	SpinLock lock(renderScene->m_proxyMapFlag);

	auto proxyObject = NarrowProxy<FoliageRenderProxy>(renderScene->m_proxyMap[m_proxyGUID]);
	if (!proxyObject) return;

	std::vector<FoliageType> foliageTypes = pComponent->GetFoliageTypes();
	std::vector<FoliageInstance> foliageInstances = pComponent->GetFoliageInstances();

	m_updateFunction = [=]()
	{
		proxyObject->m_foliageTypes = foliageTypes;
		proxyObject->m_foliageInstances = foliageInstances;
		proxyObject->m_worldMatrix = worldMatrix;
		proxyObject->m_worldPosition = worldPosition;

		// 색인은 벡터를 갈아 끼운 뒤에 다시 만든다(원소 주소를 든다).
		proxyObject->RebuildInstanceMap();
	};
}

ProxyCommand::ProxyCommand(DecalComponent* pComponent):
	m_proxyGUID(pComponent->GetInstanceID())
{
	auto renderScene = SceneManagers->GetRenderScene();
	auto owner = pComponent->GetOwner();
	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;
	Mathf::xMatrix worldMatrix = owner->m_transform.GetWorldMatrix();

	SpinLock lock(renderScene->m_proxyMapFlag);

	auto proxyObject = NarrowProxy<DecalRenderProxy>(renderScene->m_proxyMap[m_proxyGUID]);
	if (!proxyObject) return;

	Texture* diffuse = pComponent->GetDecalTexture();
	Texture* normal = pComponent->GetNormalTexture();
	Texture* orm = pComponent->GetORMTexture();
	uint32 sliceX = pComponent->sliceX;
	uint32 sliceY = pComponent->sliceY;
	int sliceNum = pComponent->sliceNumber;

	m_updateFunction = [=]()
	{
		proxyObject->m_diffuseTexture = diffuse;
		proxyObject->m_normalTexture = normal;
		proxyObject->m_occluroughmetalTexture = orm;
		proxyObject->m_worldMatrix = worldMatrix;
		proxyObject->m_sliceX = sliceX;
		proxyObject->m_sliceY = sliceY;
		proxyObject->m_sliceNum = sliceNum;
	};
}

ProxyCommand::ProxyCommand(LightComponent* pComponent) :
	m_proxyGUID(pComponent->GetInstanceID())
{
	// 광원은 매 프레임 갱신이라 파괴되는 프레임에 여기로 들어오는 것이
	// 정상 상태다. 다른 커맨드처럼 빈 채로 두면 ProxyCommandExecute가
	// 던지므로, 어느 경로로 빠져나가도 부를 수 있는 것을 먼저 넣는다.
	m_updateFunction = [] {};

	auto renderScene = SceneManagers->GetRenderScene();
	auto owner = pComponent->GetOwner();
	if (!renderScene || !owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;

	// 컴포넌트 읽기는 락 밖에서 끝낸다 — 트랜스폼 질의가 게임 오브젝트
	// 계층을 타므로 프록시 맵 락 안에서 할 일이 아니다.
	const LightRenderProxy::Values values = LightRenderProxy::ReadFrom(pComponent);

	SpinLock lock(renderScene->m_lightProxyMapFlag);

	auto it = renderScene->m_lightProxyMap.find(m_proxyGUID);
	if (it == renderScene->m_lightProxyMap.end() || !it->second) return;

	auto proxyObject = it->second;
	m_updateFunction = [proxyObject, values]
	{
		proxyObject->Apply(values);
	};
}

ProxyCommand::ProxyCommand(SpriteSheetComponent* pComponent) :
	m_proxyGUID(pComponent->GetInstanceID())
{
	//TODO : implement SpriteSheetComponent proxy command
	auto renderScene = SceneManagers->GetRenderScene();
	auto owner = pComponent->GetOwner();
	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;

	SpinLock lock(renderScene->m_uiProxyMapFlag);
	auto iter = renderScene->m_uiProxyMap.find(m_proxyGUID);
	if (iter == renderScene->m_uiProxyMap.end() || !iter->second) return;
	std::weak_ptr<UIRenderProxy> weakProxyObject = iter->second->shared_from_this();

	auto origin			= DirectX::XMFLOAT2{ pComponent->uiinfo.size.x * 0.5f, pComponent->uiinfo.size.y * 0.5f };
	auto position		= pComponent->pos;
	auto scale			= pComponent->scale;
	// 캔버스가 아직 연결되지 않았거나 먼저 파괴됐으면 널이다.
	// 지연 연결(6-3) 도입으로 "연결 전 한두 프레임"이 정상 상태가 됐다 —
	// UIRenderProxy의 같은 자리는 원래부터 널을 걸렀는데 여기만 빠져 있었다.
	auto* canvas = pComponent->GetOwnerCanvas();
	const int canvasOrder = (nullptr != canvas) ? canvas->GetCanvasOrder() : 0;
	int layerOrder		= pComponent->GetLayerOrder();
	float frameDuration = pComponent->m_frameDuration;
	bool isLoop			= pComponent->m_isLoop;
	float deltaTime		= pComponent->m_deltaTime;
	bool isEnable		= owner->IsEnabled();
	bool isPreview		= pComponent->m_isPreview;
	auto clipDirection = pComponent->clipDirection;
	float clipPercent = pComponent->clipPercent;


	m_updateFunction = [weakProxyObject, canvasOrder, isPreview, 
		isEnable, origin, position, scale, layerOrder, clipDirection, clipPercent,
		frameDuration, isLoop, deltaTime]() mutable
	{
		if (auto proxyObject = weakProxyObject.lock())
		{
			// texture는 imutable처럼 관리(한번 설정되면 이후 변경되지 않음)
			UIRenderProxy::SpriteSheetData data{};
			data.origin = origin;
			data.position = position;
			data.scale = scale;
			data.canvasOrder = canvasOrder;
			data.layerOrder = layerOrder;
			data.frameDuration = frameDuration;
			data.isPreview = isPreview;
			data.clipDirection = clipDirection;
			data.clipPercent = clipPercent;
			if (!isEnable)
			{
				proxyObject->m_sequenceState.frameIndex = 0;
				proxyObject->m_sequenceState.timeAccum = 0.f;
				data.deltaTime = 0;
			}
			else
			{
				data.deltaTime = deltaTime;
			}
			proxyObject->m_sequenceState.loop = isLoop;

			proxyObject->m_data = std::move(data);
			proxyObject->m_isEnabled = isEnable;
		}

	};

}

ProxyCommand::ProxyCommand(ImageComponent* pComponent)
{
	if (nullptr == pComponent) return;
	m_proxyGUID = pComponent->GetInstanceID();

	auto renderScene = SceneManagers->GetRenderScene();
	auto owner = pComponent->GetOwner();
	if (!renderScene || !owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;

	SpinLock lock(renderScene->m_uiProxyMapFlag);
	auto iter = renderScene->m_uiProxyMap.find(m_proxyGUID);
	if (iter == renderScene->m_uiProxyMap.end() || !iter->second) return;
	std::weak_ptr<UIRenderProxy> weakProxyObject = iter->second->shared_from_this();

    DirectX::XMFLOAT2 origin{ pComponent->origin.x, pComponent->origin.y };
	auto textures	= pComponent->textures;
	auto curTexture	= pComponent->m_curtexture;
	auto color		= pComponent->color;
	auto position	= pComponent->pos;
	auto scale		= pComponent->scale;
	float rotation	= pComponent->rotate;
	// 캔버스가 아직 연결되지 않았거나 먼저 파괴됐으면 널이다.
	// 지연 연결(6-3) 도입으로 "연결 전 한두 프레임"이 정상 상태가 됐다 —
	// UIRenderProxy의 같은 자리는 원래부터 널을 걸렀는데 여기만 빠져 있었다.
	auto* canvas = pComponent->GetOwnerCanvas();
	const int canvasOrder = (nullptr != canvas) ? canvas->GetCanvasOrder() : 0;
	int layerOrder	= pComponent->GetLayerOrder();
	auto clipDirection = pComponent->clipDirection;
	auto clipPercent   = pComponent->clipPercent;
	bool isEnable = owner->IsEnabled();


	m_updateFunction = [weakProxyObject, textures = std::move(textures),
		curTexture, origin, position, scale, isEnable, canvasOrder,
		rotation, layerOrder, color, clipDirection, clipPercent]() mutable
	{
		if (auto proxyObject = weakProxyObject.lock())
		{
			UIRenderProxy::ImageData data{};
			data.textures = std::move(textures);
			data.texture = curTexture;
			data.origin = origin;
			data.color = color;
			data.position = position;
			data.scale = scale;
			data.rotation = rotation;
			data.canvasOrder = canvasOrder;
			data.layerOrder = layerOrder;
			data.clipDirection = clipDirection;
			data.clipPercent = clipPercent;
			proxyObject->m_data = std::move(data);
			proxyObject->m_isEnabled = isEnable;

		}
	};
}

ProxyCommand::ProxyCommand(TextComponent* pComponent)
{
	if (nullptr == pComponent) return;
	m_proxyGUID = pComponent->GetInstanceID();

	auto renderScene = SceneManagers->GetRenderScene();
	auto owner = pComponent->GetOwner();
	if (!renderScene || !owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;

	SpinLock lock(renderScene->m_uiProxyMapFlag);
	auto iter = renderScene->m_uiProxyMap.find(m_proxyGUID);
	if (iter == renderScene->m_uiProxyMap.end() || !iter->second) return;
	std::weak_ptr<UIRenderProxy> weakProxyObject = iter->second->shared_from_this();

	pComponent->m_textMeasureSize = weakProxyObject.lock()->m_textMeasureSize;
	auto fontPath = pComponent->GetFontPath();
	auto message = pComponent->message;
    auto color = pComponent->color;
    auto position = pComponent->pos;
    float fontSize = pComponent->fontSize;
	// 캔버스가 아직 연결되지 않았거나 먼저 파괴됐으면 널이다.
	// 지연 연결(6-3) 도입으로 "연결 전 한두 프레임"이 정상 상태가 됐다 —
	// UIRenderProxy의 같은 자리는 원래부터 널을 걸렀는데 여기만 빠져 있었다.
	auto* canvas = pComponent->GetOwnerCanvas();
	const int canvasOrder = (nullptr != canvas) ? canvas->GetCanvasOrder() : 0;
    int layerOrder = pComponent->GetLayerOrder();
    auto maxSize = pComponent->stretchSize;
    bool stretchX = pComponent->isStretchX;
    bool stretchY = pComponent->isStretchY;
	auto alignment = pComponent->GetHorizontalAlignment();
	bool isEnable = owner->IsEnabled();

    m_updateFunction = [weakProxyObject, canvasOrder, isEnable, fontPath, message, 
		color, position, fontSize, layerOrder, maxSize, stretchX, stretchY, alignment]()
    {
        if (auto proxyObject = weakProxyObject.lock())
        {
            UIRenderProxy::TextData data{};
            data.fontPath = fontPath;
            data.message = message;
            data.color = color;
            data.position = Mathf::Vector2(position);
            data.fontSize = fontSize;
			data.canvasOrder = canvasOrder;
            data.layerOrder = layerOrder;
            data.maxSize = maxSize;
            data.stretchX = stretchX;
            data.stretchY = stretchY;
			data.alignment = alignment;
            proxyObject->m_data = std::move(data);
            proxyObject->m_isEnabled = isEnable;
        }
    };
}

ProxyCommand::ProxyCommand(const ProxyCommand& other) :
	m_proxyGUID(other.m_proxyGUID),
	m_updateFunction(other.m_updateFunction)
{
}

ProxyCommand::ProxyCommand(ProxyCommand&& other) noexcept :
	m_proxyGUID(other.m_proxyGUID),
	m_updateFunction(std::move(other.m_updateFunction))
{
}

ProxyCommand& ProxyCommand::operator=(const ProxyCommand& other)
{
	m_proxyGUID = other.m_proxyGUID;
	m_updateFunction = other.m_updateFunction;

	return *this;
}

ProxyCommand& ProxyCommand::operator=(ProxyCommand&& other) noexcept
{
	m_proxyGUID = other.m_proxyGUID;
	m_updateFunction = std::move(other.m_updateFunction);

	return *this;
}

void ProxyCommand::ProxyCommandExecute()
{
	if (!m_updateFunction) throw std::runtime_error("proxy invokable empty");

	m_updateFunction();

	m_updateFunction = nullptr;
}

