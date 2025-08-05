#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "../ScriptBinder/RenderableComponents.h"
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
#include "Terrain.h"
#include "UIManager.h"

constexpr size_t TRANSFORM_SIZE = sizeof(Mathf::xMatrix) * MAX_BONES;
concurrent_queue<HashedGuid> RenderScene::RegisteredDestroyProxyGUIDs;

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
	for (auto& pair : m_palleteMap)
	{
		if (pair.second.second)
		{
			std::free(pair.second.second);
			pair.second.second = nullptr;
		}
	}
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
	m_LightController->SetEyePosition(camera.m_eyePosition);
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

RenderPassData* RenderScene::AddRenderPassData(size_t cameraIndex)
{
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
	auto sharedPtr = m_renderDataMap[cameraIndex];
	return sharedPtr.get();
}

void RenderScene::RemoveRenderPassData(size_t cameraIndex)
{
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

	if (m_animatorMap.find(animatorGuid) != m_animatorMap.end()) return;

	m_animatorMap[animatorGuid] = animatorPtr;

	void* voidPtr = std::malloc(TRANSFORM_SIZE);
	if(voidPtr)
	{
		m_palleteMap[animatorGuid].second = (Mathf::xMatrix*)voidPtr;
	}
}

void RenderScene::UnregisterAnimator(const std::shared_ptr<Animator>& animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	if (m_animatorMap.find(animatorGuid) == m_animatorMap.end()) return;
	if (m_palleteMap.find(animatorGuid) == m_palleteMap.end()) return;

	m_animatorMap.erase(animatorGuid);

	if (m_palleteMap[animatorGuid].second)
	{
		free(m_palleteMap[animatorGuid].second);
		m_palleteMap.erase(animatorGuid);
	}
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