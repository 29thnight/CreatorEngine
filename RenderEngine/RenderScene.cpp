#include "RenderScene.h"
#include "Animator.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "Skeleton.h"
#include "LightController.h"
#include "Benchmark.hpp"
#include "FoliageComponent.h"
#include "MeshRenderer.h"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "MeshRendererProxy.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "Terrain.h"
#include "UIManager.h"
#include "DecalComponent.h"
#include "SpriteRenderer.h"
#include "SpriteSheetComponent.h"

constexpr size_t TRANSFORM_SIZE = sizeof(Mathf::xMatrix) * MAX_BONES;
concurrent_queue<HashedGuid> RenderScene::RegisteredDestroyProxyGUIDs;
concurrent_queue<HashedGuid> RenderScene::RegisteredDestroyUIProxyGUIDs;

ShadowMapRenderDesc RenderScene::g_shadowMapDesc{};

RenderScene::~RenderScene()
{
}

void RenderScene::Initialize()
{
	m_renderDataMap.resize(10);
	m_LightController = new LightController();
	m_animationJob.SetRenderScene(this);
}

void RenderScene::Finalize()
{
	Memory::SafeDelete(m_LightController);

	SpinLock lock(m_proxyMapFlag);
	m_proxyMap.clear();
	m_animatorMap.clear();
	// 팔레트 버퍼는 shared_ptr가 관리하므로 수동 해제가 필요 없다.
	// (예전의 수동 free는 UnregisterAnimator와 겹치면 더블 프리가 될 수 있었다.)
	m_palleteMap.clear();
	m_renderDataMap.clear();
	m_animationJob.Finalize();
}

void RenderScene::SetBuffers(ID3D11Buffer* modelBuffer)
{
	m_ModelBuffer = modelBuffer;
}

void RenderScene::Update(float deltaSecond)
{
	m_currentScene = SceneManagers->GetActiveScene();
	if (m_currentScene == nullptr) return;

    m_LightController->m_lightCount = m_currentScene->UpdateLight(m_LightController->m_lightProperties);
}

void RenderScene::ShadowStage(Camera& camera)
{
	// 살아 있는 카메라가 아니라 프레임 밀봉된 위치를 쓴다(PHASE 3-2).
	// 스냅샷이 아직 없으면(첫 프레임 등) 이 단계는 건너뛴다 — 예전에는
	// 그 경우에도 카메라를 직접 읽어 게임 스레드와 값이 찢어질 수 있었다.
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	m_LightController->SetEyePosition(renderData->GetFrameSnapshot().eyePosition);
	m_LightController->Update();
	m_LightController->RenderAnyShadowMap(*this, camera);
}

void RenderScene::CreateShadowCommandList(ID3D11DeviceContext* deferredContext, Camera& camera)
{
	m_LightController->CreateShadowCommandList(deferredContext, *this, camera);
}

void RenderScene::UseModel()
{
	DirectX11::VSSetConstantBuffer(0, 1, &m_ModelBuffer);
}

void RenderScene::UseModel(ID3D11DeviceContext* deferredContext)
{
	deferredContext->VSSetConstantBuffers(0, 1, &m_ModelBuffer);
}

void RenderScene::UpdateModel(const Mathf::xMatrix& model)
{
	DirectX11::UpdateBuffer(m_ModelBuffer, &model);
}

void RenderScene::UpdateModel(const Mathf::xMatrix& model, ID3D11DeviceContext* deferredContext)
{
	deferredContext->UpdateSubresource(m_ModelBuffer, 0, nullptr, &model, 0, 0);
}

RenderScene::ResourceCounts RenderScene::GetResourceCounts()
{
	ResourceCounts counts{};

	{
		// 프록시 맵을 조작하는 다른 경로와 동일한 락 규약을 따른다.
		SpinLock lock(m_proxyMapFlag);
		counts.proxies = m_proxyMap.size();
		counts.animators = m_animatorMap.size();
		counts.animationPalettes = m_palleteMap.size();
	}

	{
		SpinLock lock(m_uiProxyMapFlag);
		counts.uiProxies = m_uiProxyMap.size();
	}

	for (const auto& data : m_renderDataMap)
	{
		if (data != nullptr)
		{
			++counts.renderPassDatas;
		}
	}

	return counts;
}

RenderScene::ProxySnapshot RenderScene::GetPrimitiveProxySnapshot()
{
	ProxySnapshot snapshot;
	SpinLock lock(m_proxyMapFlag);
	snapshot.reserve(m_proxyMap.size());
	for (const auto& [guid, proxy] : m_proxyMap)
	{
		(void)guid;
		if (proxy != nullptr)
		{
			snapshot.push_back(proxy);
		}
	}
	return snapshot;
}

RenderPassData* RenderScene::AddRenderPassData(size_t cameraIndex)
{
	if (m_renderDataMap.empty())
	{
		return nullptr;
	}

	auto ptr = m_renderDataMap[cameraIndex];
	if (nullptr != ptr)
	{
		return ptr.get();
	}

	auto newRenderData = std::make_shared<RenderPassData>();
	newRenderData->Initalize(cameraIndex);
	m_renderDataMap[cameraIndex] = newRenderData;
	//m_renderDataMap.insert({ cameraIndex, newRenderData });

	return newRenderData.get();
}

RenderPassData* RenderScene::GetRenderPassData(size_t cameraIndex)
{
	if (m_renderDataMap.empty())
	{
		return nullptr;
	}

	auto sharedPtr = m_renderDataMap[cameraIndex];
	return sharedPtr.get();
}

void RenderScene::RemoveRenderPassData(size_t cameraIndex)
{
	if (m_renderDataMap.empty())
	{
		return;
	}

	auto sharedPtr = m_renderDataMap[cameraIndex];
	if (nullptr != sharedPtr)
	{
		sharedPtr->m_isDestroy = true;
	}
}

void RenderScene::EraseRenderPassData()
{
	for(auto& ptr : m_renderDataMap)
	{
		if (nullptr == ptr) continue;

		if (ptr->m_isDestroy)
		{
			ptr.reset();
			ptr = nullptr;
		}
	}
}

void RenderScene::RegisterAnimator(const std::shared_ptr<Animator>& animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	// 애니메이터/팔레트 맵은 프록시 맵과 같은 락으로 보호한다.
	// ProxyCommand 생성자가 스레드풀에서 이 맵들을 동시에 읽기 때문이다.
	SpinLock lock(m_proxyMapFlag);

	if (m_animatorMap.find(animatorGuid) != m_animatorMap.end()) return;

	m_animatorMap[animatorGuid] = animatorPtr;

	m_palleteMap[animatorGuid].second = std::make_shared<Mathf::xMatrix[]>(Skeleton::MAX_BONES);
}

void RenderScene::UnregisterAnimator(const std::shared_ptr<Animator>& animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	m_animatorMap.erase(animatorGuid);

	// 맵에서 지워도 버퍼를 참조 중인 프록시가 있으면 shared_ptr가 수명을 유지한다.
	// 마지막 참조가 사라질 때 해제되므로 렌더 스레드가 해제된 메모리를 읽는 일이 없다.
	m_palleteMap.erase(animatorGuid);
}

void RenderScene::RegisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(meshRendererGuid) != m_proxyMap.end()) return;

	// Create a new proxy for the mesh renderer and insert it into the map
	auto managedCommand = std::make_shared<PrimitiveRenderProxy>(meshRendererPtr);
	m_proxyMap[meshRendererGuid] = managedCommand;
}

void RenderScene::RegisterCommand(FoliageComponent* foliagePtr)
{
    if (nullptr == foliagePtr) return;

    HashedGuid guid = foliagePtr->GetInstanceID();

    SpinLock lock(m_proxyMapFlag);

    if (m_proxyMap.find(guid) != m_proxyMap.end()) return;

    auto managed = std::make_shared<PrimitiveRenderProxy>(foliagePtr);
    m_proxyMap[guid] = managed;
}

bool RenderScene::InvaildCheckMeshRenderer(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr || meshRendererPtr->IsDestroyMark()) return false;

	auto owner = meshRendererPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(meshRendererGuid) == m_proxyMap.end()) return false;

	auto& proxyObject = m_proxyMap[meshRendererGuid];

	if (nullptr == proxyObject) return false;

	return true;
}

bool RenderScene::InvaildCheckFoliage(FoliageComponent* foliagePtr)
{
    if (nullptr == foliagePtr || foliagePtr->IsDestroyMark()) return false;

    auto owner = foliagePtr->GetOwner();
    if (nullptr == owner || owner->IsDestroyMark()) return false;

    HashedGuid guid = foliagePtr->GetInstanceID();

    SpinLock lock(m_proxyMapFlag);

    if (m_proxyMap.find(guid) == m_proxyMap.end()) return false;

    auto& proxyObject = m_proxyMap[guid];

    if (nullptr == proxyObject) return false;

    return true;
}

PrimitiveRenderProxy* RenderScene::FindProxy(size_t guid)
{
	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(guid) == m_proxyMap.end()) return nullptr;

	return m_proxyMap[guid].get();
}

UIRenderProxy* RenderScene::FindUIProxy(size_t guid)
{
	SpinLock lock(m_uiProxyMapFlag);

	if (m_uiProxyMap.find(guid) == m_uiProxyMap.end()) return nullptr;
	return m_uiProxyMap[guid].get();
}

void RenderScene::OnProxyDestroy()
{
	while (!RenderScene::RegisteredDestroyProxyGUIDs.empty())
	{
		HashedGuid ID;
		if (RenderScene::RegisteredDestroyProxyGUIDs.try_pop(ID))
		{
			{
				SpinLock lock(m_proxyMapFlag);
				m_proxyMap.erase(ID);
			}
		}
	}

	while (!RenderScene::RegisteredDestroyUIProxyGUIDs.empty())
	{
		HashedGuid ID;
		if (RenderScene::RegisteredDestroyUIProxyGUIDs.try_pop(ID))
		{
			{
				SpinLock lock(m_uiProxyMapFlag);
				m_uiProxyMap.erase(ID);
			}
		}
	}

	for (auto& [guid, pair] : m_palleteMap)
	{
		auto& isUpdated = pair.first;

		isUpdated = false;
	}
}

void RenderScene::UpdateCommand(MeshRenderer* meshRendererPtr)
{
    ProxyCommand moveCommand = MakeProxyCommand(meshRendererPtr);
    ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

void RenderScene::UpdateCommand(FoliageComponent* foliagePtr)
{
    ProxyCommand moveCommand = MakeProxyCommand(foliagePtr);
    ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(MeshRenderer* meshRendererPtr)
{
	if (!InvaildCheckMeshRenderer(meshRendererPtr)) 
	{
		throw std::runtime_error("InvaildCheckMeshRenderer");
	}

    ProxyCommand command(meshRendererPtr);
    return command;
}

ProxyCommand RenderScene::MakeProxyCommand(FoliageComponent* foliagePtr)
{
    if (!InvaildCheckFoliage(foliagePtr))
    {
		throw std::runtime_error("InvaildCheckFoliage");
    }

    ProxyCommand command(foliagePtr);
    return command;
}

void RenderScene::UnregisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);
	
	if (m_proxyMap.find(meshRendererGuid) == m_proxyMap.end()) return;

        m_proxyMap[meshRendererGuid]->DestroyProxy();
}

void RenderScene::RegisterCommand(TerrainComponent* terrainPtr)
{
	if (nullptr == terrainPtr) return;
	HashedGuid terrainGuid = terrainPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(terrainGuid) != m_proxyMap.end()) return;
	// Create a new proxy for the terrain and insert it into the map
	auto managedCommand = std::make_shared<PrimitiveRenderProxy>(terrainPtr);
	m_proxyMap[terrainGuid] = managedCommand;
}

bool RenderScene::InvaildCheckTerrain(TerrainComponent* terrainPtr)
{
	if (nullptr == terrainPtr || terrainPtr->IsDestroyMark()) return false;
	auto owner = terrainPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid terrainGuid = terrainPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(terrainGuid) == m_proxyMap.end()) return false;
	auto& proxyObject = m_proxyMap[terrainGuid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::UpdateCommand(TerrainComponent* terrainPtr)
{
	if (!InvaildCheckTerrain(terrainPtr)) 
	{
		throw std::runtime_error("InvaildCheckTerrain");
	}
	ProxyCommand moveCommand = MakeProxyCommand(terrainPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(TerrainComponent* terrainPtr)
{
	if (!InvaildCheckTerrain(terrainPtr)) 
	{
		throw std::runtime_error("InvaildCheckTerrain");
	}
	ProxyCommand command(terrainPtr);
	return command;
}

void RenderScene::UnregisterCommand(TerrainComponent* terrainPtr)
{
	if (nullptr == terrainPtr) return;
	HashedGuid terrainGuid = terrainPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(terrainGuid) == m_proxyMap.end()) return;
	m_proxyMap[terrainGuid]->DestroyProxy();
}

void RenderScene::UnregisterCommand(FoliageComponent* foliagePtr)
{
    if (nullptr == foliagePtr) return;

    HashedGuid guid = foliagePtr->GetInstanceID();

    SpinLock lock(m_proxyMapFlag);

    if (m_proxyMap.find(guid) == m_proxyMap.end()) return;

    m_proxyMap[guid]->DestroyProxy();
}

void RenderScene::RegisterCommand(DecalComponent* decalPtr)
{
	if (nullptr == decalPtr) return;

	HashedGuid guid = decalPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(guid) != m_proxyMap.end()) return;

	auto managed = std::make_shared<PrimitiveRenderProxy>(decalPtr);
	m_proxyMap[guid] = managed;
}

bool RenderScene::InvaildCheckDecal(DecalComponent* decalPtr)
{
	if (nullptr == decalPtr || decalPtr->IsDestroyMark()) return false;

	auto owner = decalPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;

	HashedGuid guid = decalPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(guid) == m_proxyMap.end()) return false;

	auto& proxyObject = m_proxyMap[guid];

	if (nullptr == proxyObject) return false;

	return true;
}

void RenderScene::UpdateCommand(DecalComponent* decalPtr)
{
	ProxyCommand moveCommand = MakeProxyCommand(decalPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(DecalComponent* decalPtr)
{
	if (!InvaildCheckDecal(decalPtr))
	{
		throw std::runtime_error("InvaildCheckDecal");
	}

	ProxyCommand command(decalPtr);
	return command;
}

void RenderScene::UnregisterCommand(DecalComponent* decalPtr)
{
	if (nullptr == decalPtr) return;

	HashedGuid guid = decalPtr->GetInstanceID();

	SpinLock lock(m_proxyMapFlag);

	if (m_proxyMap.find(guid) == m_proxyMap.end()) return;

	m_proxyMap[guid]->DestroyProxy();
}

void RenderScene::RegisterCommand(ImageComponent* imagePtr)
{
	if (nullptr == imagePtr) return;
	HashedGuid imageGuid = imagePtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(imageGuid) != m_uiProxyMap.end()) return;
	// Create a new proxy for the image and insert it into the map
	auto managedCommand = std::make_shared<UIRenderProxy>(imagePtr);
	m_uiProxyMap[imageGuid] = managedCommand;
}

bool RenderScene::InvaildCheckImage(ImageComponent* imagePtr)
{
	if (nullptr == imagePtr || imagePtr->IsDestroyMark()) return false;
	auto owner = imagePtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid imageGuid = imagePtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(imageGuid) == m_uiProxyMap.end()) return false;
	auto& proxyObject = m_uiProxyMap[imageGuid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::UpdateCommand(ImageComponent* imagePtr)
{
	if (!InvaildCheckImage(imagePtr)) 
	{
		return;
		//throw std::runtime_error("InvaildCheckImage");
	}
	ProxyCommand moveCommand = MakeProxyCommand(imagePtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(ImageComponent* imagePtr)
{
	if (!InvaildCheckImage(imagePtr)) 
	{
		throw std::runtime_error("InvaildCheckImage");
	}
	ProxyCommand command(imagePtr);
	return command;
}

void RenderScene::UnregisterCommand(ImageComponent* imagePtr)
{
	if (nullptr == imagePtr) return;
	HashedGuid imageGuid = imagePtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(imageGuid) == m_uiProxyMap.end()) return;
	m_uiProxyMap[imageGuid]->DestroyProxy();
}

void RenderScene::RegisterCommand(TextComponent* textPtr)
{
	if (nullptr == textPtr) return;
	HashedGuid textGuid = textPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(textGuid) != m_uiProxyMap.end()) return;
	// Create a new proxy for the text and insert it into the map
	auto managedCommand = std::make_shared<UIRenderProxy>(textPtr);
	m_uiProxyMap[textGuid] = managedCommand;
}

bool RenderScene::InvaildCheckText(TextComponent* textPtr)
{
	if (nullptr == textPtr || textPtr->IsDestroyMark()) return false;
	auto owner = textPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid textGuid = textPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(textGuid) == m_uiProxyMap.end()) return false;
	auto& proxyObject = m_uiProxyMap[textGuid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::UpdateCommand(TextComponent* textPtr)
{
	if (!InvaildCheckText(textPtr)) 
	{
		return;
		//throw std::runtime_error("InvaildCheckText");
	}
	ProxyCommand moveCommand = MakeProxyCommand(textPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(TextComponent* textPtr)
{
	if (!InvaildCheckText(textPtr)) 
	{
		throw std::runtime_error("InvaildCheckText");
	}
	ProxyCommand command(textPtr);
	return command;
}

void RenderScene::UnregisterCommand(TextComponent* textPtr)
{
	if (nullptr == textPtr) return;
	HashedGuid textGuid = textPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(textGuid) == m_uiProxyMap.end()) return;
	m_uiProxyMap[textGuid]->DestroyProxy();
}

void RenderScene::RegisterCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (nullptr == spriteSheetPtr) return;
	HashedGuid guid = spriteSheetPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(guid) != m_uiProxyMap.end()) return;
	auto managed = std::make_shared<UIRenderProxy>(spriteSheetPtr);
	m_uiProxyMap[guid] = managed;
}

void RenderScene::UnregisterCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (nullptr == spriteSheetPtr) return;
	HashedGuid guid = spriteSheetPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(guid) == m_uiProxyMap.end()) return;
	m_uiProxyMap[guid]->DestroyProxy();
}

void RenderScene::UpdateCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (!InvaildCheckSpriteSheet(spriteSheetPtr)) 
	{
		throw std::runtime_error("InvaildCheckSpriteSheet");
	}
	ProxyCommand moveCommand = MakeProxyCommand(spriteSheetPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(SpriteSheetComponent* spriteSheetPtr)
{
	if (!InvaildCheckSpriteSheet(spriteSheetPtr)) 
	{
		throw std::runtime_error("InvaildCheckSpriteSheet");
	}
	ProxyCommand command(spriteSheetPtr);
	return command;
}

bool RenderScene::InvaildCheckSpriteSheet(SpriteSheetComponent* spriteSheetPtr)
{
	if (nullptr == spriteSheetPtr || spriteSheetPtr->IsDestroyMark()) return false;
	auto owner = spriteSheetPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid guid = spriteSheetPtr->GetInstanceID();
	SpinLock lock(m_uiProxyMapFlag);
	if (m_uiProxyMap.find(guid) == m_uiProxyMap.end()) return false;
	auto& proxyObject = m_uiProxyMap[guid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::RegisterCommand(SpriteRenderer* spriteRendererPtr)
{
	if (nullptr == spriteRendererPtr) return;
	HashedGuid spriteRendererGuid = spriteRendererPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(spriteRendererGuid) != m_proxyMap.end()) return;
	// Create a new proxy for the sprite renderer and insert it into the map
	auto managedCommand = std::make_shared<PrimitiveRenderProxy>(spriteRendererPtr);
	m_proxyMap[spriteRendererGuid] = managedCommand;
}

bool RenderScene::InvaildCheckSpriteRenderer(SpriteRenderer* spriteRendererPtr)
{
	if (nullptr == spriteRendererPtr || spriteRendererPtr->IsDestroyMark()) return false;
	auto owner = spriteRendererPtr->GetOwner();
	if (nullptr == owner || owner->IsDestroyMark()) return false;
	HashedGuid spriteRendererGuid = spriteRendererPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(spriteRendererGuid) == m_proxyMap.end()) return false;
	auto& proxyObject = m_proxyMap[spriteRendererGuid];
	if (nullptr == proxyObject) return false;
	return true;
}

void RenderScene::UpdateCommand(SpriteRenderer* spriteRendererPtr)
{
	if (!InvaildCheckSpriteRenderer(spriteRendererPtr)) 
	{
		throw std::runtime_error("InvaildCheckSpriteRenderer");
	}
	ProxyCommand moveCommand = MakeProxyCommand(spriteRendererPtr);
	ProxyCommandQueue->PushProxyCommand(std::move(moveCommand));
}

ProxyCommand RenderScene::MakeProxyCommand(SpriteRenderer* spriteRendererPtr)
{
	if (!InvaildCheckSpriteRenderer(spriteRendererPtr)) 
	{
		throw std::runtime_error("InvaildCheckSpriteRenderer");
	}
	ProxyCommand command(spriteRendererPtr);
	return command;
}

void RenderScene::UnregisterCommand(SpriteRenderer* spriteRendererPtr)
{
	if (nullptr == spriteRendererPtr) return;
	HashedGuid spriteRendererGuid = spriteRendererPtr->GetInstanceID();
	SpinLock lock(m_proxyMapFlag);
	if (m_proxyMap.find(spriteRendererGuid) == m_proxyMap.end()) return;
	m_proxyMap[spriteRendererGuid]->DestroyProxy();
}
