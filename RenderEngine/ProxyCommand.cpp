#include "ProxyCommand.h"
#include "MeshRenderer.h"
#include "Terrain.h"
#include "FoliageComponent.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "RenderScene.h"
#include "SceneManager.h"
#include "Material.h"
#include "DecalComponent.h"

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

		if(isLightMappingUpdatable)
		{
			proxyObject->m_LightMapping = copyLightMapping;
		}

		if (isMatChange)
		{
			proxyObject->m_Material = originMat;
			proxyObject->m_materialGuid = originMatGuid;
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

	std::vector<FoliageType> foliageTypes;
	std::vector<FoliageInstance> foliageInstances;

	size_t foliageTypesSize = pComponent->GetFoliageTypes().size();
	size_t foliageInstancesSize = pComponent->GetFoliageInstances().size();
	size_t proxyFoliageSize = proxyObject->m_foliageTypes.size();
	size_t proxyInstancesSize = proxyObject->m_foliageInstances.size();

	if (foliageTypesSize != proxyFoliageSize)
	{
		foliageTypes = pComponent->GetFoliageTypes();
	}

	if (foliageInstancesSize != proxyInstancesSize)
	{
		foliageInstances = pComponent->GetFoliageInstances();
	}

	m_updateFunction = [=]()
	{
		if (proxyFoliageSize != foliageTypesSize)
		{
			proxyObject->m_foliageTypes = foliageTypes;
		}

		if (proxyInstancesSize != foliageInstancesSize)
		{
			proxyObject->m_foliageInstances = foliageInstances;
		}
		proxyObject->m_worldMatrix = worldMatrix;
		proxyObject->m_worldPosition = worldPosition;
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
	auto proxyObject = iter->second.get();

	DirectX::XMFLOAT2 origin{ pComponent->uiinfo.size.x * 0.5f, pComponent->uiinfo.size.y * 0.5f };
	auto textures	= pComponent->textures;
	auto curTexture	= pComponent->m_curtexture;
	auto color		= pComponent->color;
	auto position	= pComponent->pos;
	auto scale		= pComponent->scale;
	float rotation	= pComponent->rotate;
	int layerOrder	= pComponent->GetLayerOrder();
	auto clipDirection = pComponent->clipDirection;
	auto clipPercent   = pComponent->clipPercent;
	auto shaderPath = pComponent->GetCustomPixelShader();
	auto cpuBuffer = pComponent->GetCustomPixelCPUBuffer();

	if (!shaderPath.empty())
	{
		proxyObject->SetCustomPixelShader(shaderPath);
	}

	m_updateFunction = [proxyObject, textures = std::move(textures), 
		curTexture, origin, position, scale, 
		rotation, layerOrder, color, clipDirection, clipPercent, buffer = std::move(cpuBuffer)]() mutable
	{
		UIRenderProxy::ImageData data{};
		data.textures		= std::move(textures);
		data.texture		= curTexture;
		data.origin			= origin;
		data.color			= color;
		data.position		= position;
		data.scale			= scale;
		data.rotation		= rotation;
		data.layerOrder		= layerOrder;
		data.clipDirection      = clipDirection;
		data.clipPercent        = clipPercent;
		proxyObject->m_data = std::move(data);

		if (!buffer.empty())
		{
			proxyObject->SetCustomPixelBuffer(buffer);
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
	auto proxyObject = iter->second.get();

	auto font = pComponent->font;
	auto message = pComponent->message;
	auto color = pComponent->color;
	auto position = pComponent->pos;
	float fontSize = pComponent->fontSize;
	int layerOrder = pComponent->GetLayerOrder();

	m_updateFunction = [proxyObject, font, message, color, position, fontSize, layerOrder]()
	{
		UIRenderProxy::TextData data{};
		data.font = font;
		data.message = message;
		data.color = color;
		data.position = Mathf::Vector2(position);
		data.fontSize = fontSize;
		data.layerOrder = layerOrder;
		proxyObject->m_data = std::move(data);
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
}
