#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "LightProperty.h"
#include "Skeleton.h"
#include "Benchmark.hpp"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "PrimitiveRenderProxy.h"
#include "LightRenderProxy.h"

concurrent_queue<HashedGuid> RenderScene::RegisteredDestroyProxyGUIDs;
concurrent_queue<HashedGuid> RenderScene::RegisteredDestroyLightProxyGUIDs;
concurrent_queue<HashedGuid> RenderScene::RegisteredDestroyUIProxyGUIDs;

ShadowMapRenderDesc RenderScene::g_shadowMapDesc{};

RenderScene::~RenderScene()
{
}

void RenderScene::Initialize()
{
	m_renderDataMap.resize(10);
	m_animationJob.SetRenderScene(this);
}

void RenderScene::Finalize()
{
	{
		SpinLock lightLock(m_lightProxyMapFlag);
		m_lightProxyMap.clear();
	}

	SpinLock lock(m_proxyMapFlag);
	m_proxyMap.clear();
	m_animatorMap.clear();
	// 팔레트 버퍼는 shared_ptr가 관리하므로 수동 해제가 필요 없다.
	// (예전의 수동 free는 UnregisterAnimator와 겹치면 더블 프리가 될 수 있었다.)
	m_palleteMap.clear();
	m_renderDataMap.clear();
	m_animationJob.Finalize();
}

// Update()가 여기 있었다. 남은 일이 없어 걷었다 (PHASE 4-2).
//
// 두 가지가 순서대로 사라졌다:
//   ① 광원 재수집 — Scene::m_lights 전체를 매 프레임 훑어 LightController로
//      복사했다. 광원이 등록/해제 기반 프록시가 된 뒤로 소비자가 없다
//      (RenderSceneViewPlan ①).
//   ② 활성 씬 당김 — m_currentScene = SceneManagers->GetActiveScene().
//      씬은 이미 SetScene으로 밀려 들어온다(초기화·씬 생성·활성 씬 변경
//      세 자리 전부 EnhancedSceneRenderer::SetActiveScene을 거친다).
//      ★ 당김이 푸시를 덮고 있었다: SetActiveScene은 SetScene(scene) 직후
//        Update(0.f)를 불렀는데, 그 Update가 방금 받은 scene을 버리고 전역의
//        활성 씬으로 되돌렸다. 둘이 어긋나는 전환 순간에만 드러나는 종류다.
//
// 이 당김이 RenderScene의 마지막 SceneManager 의존이었다.

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
		SpinLock lock(m_lightProxyMapFlag);
		counts.lightProxies = m_lightProxyMap.size();
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

RenderScene::LightProxySnapshot RenderScene::GetLightProxySnapshot()
{
	LightProxySnapshot snapshot;
	SpinLock lock(m_lightProxyMapFlag);
	snapshot.reserve(m_lightProxyMap.size());
	for (const auto& [guid, proxy] : m_lightProxyMap)
	{
		(void)guid;
		// 꺼진 광원은 여기서 거른다. 뷰마다 다시 거르면 카메라 수만큼
		// 같은 판정을 반복하게 되고, 이 판정은 뷰와 무관하다.
		if (nullptr != proxy && proxy->IsEnabled())
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

// FindLightProxy는 두지 않는다. FindProxy 계열은 락을 놓은 뒤 원시 포인터만
// 돌려주므로, 반환 직후 회수가 돌면 댕글링이다(프리미티브 쪽에 이미 있는
// 위험이라 복제하지 않았다). 뷰별 광원 목록(RenderSceneViewPlan ②)은
// 스냅샷으로 받으면 되고, 그쪽은 shared_ptr가 수명을 붙든다.

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

	while (!RenderScene::RegisteredDestroyLightProxyGUIDs.empty())
	{
		HashedGuid ID;
		if (RenderScene::RegisteredDestroyLightProxyGUIDs.try_pop(ID))
		{
			SpinLock lock(m_lightProxyMapFlag);
			m_lightProxyMap.erase(ID);
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

