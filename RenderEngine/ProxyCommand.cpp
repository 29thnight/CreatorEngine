#include "ProxyCommand.h"
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
#include "ShaderSystem.h"
#include <execution>

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
	Material* originMat				= pComponent->m_Material;

	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;

	if (nullptr == originMat) 
	{
		m_updateFunction = [=]
		{
			// If the material is null, we do not need to update anything.
		};
		return;
	}

	auto& proxyObject				= renderScene->m_proxyMap[m_proxyGUID];
	HashedGuid aniGuid				= proxyObject->m_animatorGuid;
	HashedGuid matGuid				= proxyObject->m_materialGuid;
	HashedGuid originMatGuid		= pComponent->m_Material->m_materialGuid;
	bool isEnableLOD				= pComponent->m_isEnableLOD;
	uint32 bitflag					= pComponent->m_bitflag;

	Mathf::xMatrix* palletePtr{ nullptr };
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

			memcpy(palletePtr, srcPalete, TRANSFORM_SIZE);
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
		proxyObject->m_isStatic			= isStatic;
		proxyObject->m_isEnableShadow	= isEnabled;
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
    std::string customPSOName = componentPtr->GetCustomPSOName();
    BillboardType billboardType = componentPtr->GetBillboardType();
    auto billboardAxis = componentPtr->GetBillboardAxis();
	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;
	auto& proxyObject = renderScene->m_proxyMap[m_proxyGUID];
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
		proxyObject->m_isEnableShadow = isEnabled;
        proxyObject->m_spriteTexture = originTexture;
        proxyObject->m_customPSOName = customPSOName;
        proxyObject->m_billboardType = billboardType;
        proxyObject->m_billboardAxis = billboardAxis;
		proxyObject->m_enableDepth = isEnableDepth;
        if (!customPSOName.empty())
        {
            auto it = ShaderSystem->ShaderAssets.find(customPSOName);
            proxyObject->m_customPSO = (it != ShaderSystem->ShaderAssets.end()) ? it->second : nullptr;
        }
        else
        {
            proxyObject->m_customPSO = nullptr;
        }
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
	auto& proxyObject = renderScene->m_proxyMap[m_proxyGUID];

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
	auto& proxyObject = renderScene->m_proxyMap[m_proxyGUID];
	if (!proxyObject) return;

	std::vector<FoliageType> foliageTypes = pComponent->GetFoliageTypes();
	std::vector<FoliageInstance> foliageInstances = pComponent->GetFoliageInstances();

	m_updateFunction = [=]()
	{
		proxyObject->m_foliageTypes = foliageTypes;
		proxyObject->m_foliageInstances = foliageInstances;
		proxyObject->m_worldMatrix = worldMatrix;
		proxyObject->m_worldPosition = worldPosition;

		proxyObject->instanceMap.clear();

		for (auto& inst : proxyObject->m_foliageInstances)
		{
			if (inst.m_isCulled) continue;
			proxyObject->instanceMap[inst.m_foliageTypeID].push_back(&inst);
		}
	};
}

ProxyCommand::ProxyCommand(DecalComponent* pComponent):
	m_proxyGUID(pComponent->GetInstanceID())
{
	auto renderScene = SceneManagers->GetRenderScene();
	auto owner = pComponent->GetOwner();
	if (!owner || owner->IsDestroyMark() || pComponent->IsDestroyMark()) return;
	Mathf::xMatrix worldMatrix = owner->m_transform.GetWorldMatrix();
	auto& proxyObject = renderScene->m_proxyMap[m_proxyGUID];

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
	auto* canvas = pComponent->GetOwnerCanvas();
	int canvasOrder{};
	{
		canvasOrder = canvas->GetCanvasOrder();
	}
	int layerOrder		= pComponent->GetLayerOrder();
	float frameDuration = pComponent->m_frameDuration;
	auto cpuBuffer		= pComponent->GetCustomPixelCPUBuffer();
	auto shaderPath		= pComponent->GetCustomPixelShader();
	bool isLoop			= pComponent->m_isLoop;
	float deltaTime		= pComponent->m_deltaTime;
	bool isEnable		= owner->IsEnabled();
	bool isPreview		= pComponent->m_isPreview;
	auto clipDirection = pComponent->clipDirection;
	float clipPercent = pComponent->clipPercent;

	if (!shaderPath.empty())
	{
		auto shaderObject = weakProxyObject.lock();
		if (shaderObject)
		{
			shaderObject->SetCustomPixelShader(shaderPath);
		}
	}

	m_updateFunction = [weakProxyObject, canvasOrder, isPreview, 
		isEnable, origin, position, scale, layerOrder, clipDirection, clipPercent,
		frameDuration, isLoop, deltaTime, buffer = std::move(cpuBuffer)]() mutable
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
			if (!buffer.empty())
			{
				proxyObject->SetCustomPixelBuffer(buffer);
			}
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
	auto* canvas = pComponent->GetOwnerCanvas();
	int canvasOrder{};
	{
		canvasOrder = canvas->GetCanvasOrder();
	}
	int layerOrder	= pComponent->GetLayerOrder();
	auto clipDirection = pComponent->clipDirection;
	auto clipPercent   = pComponent->clipPercent;
	auto shaderPath = pComponent->GetCustomPixelShader();
	auto cpuBuffer = pComponent->GetCustomPixelCPUBuffer();
	bool isEnable = owner->IsEnabled();

	if (!shaderPath.empty())
	{
		auto sh_ptr = weakProxyObject.lock();
		sh_ptr->SetCustomPixelShader(shaderPath);
	}

	m_updateFunction = [weakProxyObject, textures = std::move(textures),
		curTexture, origin, position, scale, isEnable, canvasOrder,
		rotation, layerOrder, color, clipDirection, clipPercent, buffer = std::move(cpuBuffer)]() mutable
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

			if (!buffer.empty())
			{
				proxyObject->SetCustomPixelBuffer(buffer);
			}
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
	auto font = pComponent->font;
	auto message = pComponent->message;
    auto color = pComponent->color;
    auto position = pComponent->pos;
    float fontSize = pComponent->fontSize;
	auto* canvas = pComponent->GetOwnerCanvas();
	int canvasOrder{};
	{
		canvasOrder = canvas->GetCanvasOrder();
	}
    int layerOrder = pComponent->GetLayerOrder();
    auto maxSize = pComponent->stretchSize;
    bool stretchX = pComponent->isStretchX;
    bool stretchY = pComponent->isStretchY;
	auto alignment = pComponent->GetHorizontalAlignment();
	bool isEnable = owner->IsEnabled();

    m_updateFunction = [weakProxyObject, canvasOrder, isEnable, font, message, 
		color, position, fontSize, layerOrder, maxSize, stretchX, stretchY, alignment]()
    {
        if (auto proxyObject = weakProxyObject.lock())
        {
            UIRenderProxy::TextData data{};
            data.font = font;
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
