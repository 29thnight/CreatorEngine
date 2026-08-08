#include "RenderScene.h"
#include "Animator.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "Skeleton.h"
#include "LightController.h"
#include "Benchmark.hpp"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "MeshRendererProxy.h"
#include "UIManager.h"

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

void RenderScene::Update(float deltaSecond)
{
	m_currentScene = SceneManagers->GetActiveScene();
	if (m_currentScene == nullptr) return;

    m_LightController->m_lightCount = m_currentScene->UpdateLight(m_LightController->m_lightProperties);
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

