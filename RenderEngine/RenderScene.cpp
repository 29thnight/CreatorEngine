#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "LightProperty.h"
#include "Skeleton.h"
#include "Benchmark.hpp"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "PrimitiveRenderProxy.h"
#include "LightRenderProxy.h"

ShadowMapRenderDesc RenderScene::g_shadowMapDesc{};

RenderScene::~RenderScene()
{
}

void RenderScene::Initialize()
{
	m_renderDataMap.resize(10);
}

void RenderScene::Finalize()
{
	{
		SpinLock lightLock(m_lightProxyMapFlag);
		m_lightProxyMap.clear();
	}
	{
		SpinLock uiLock(m_uiProxyMapFlag);
		m_uiProxyMap.clear();
	}

	{
		SpinLock lock(m_proxyMapFlag);
		m_proxyMap.clear();
	}
	m_renderDataMap.clear();
	m_animationJob.Finalize();
}

bool RenderScene::BeginProxyFrame(uint64_t sceneEpoch)
{
	if (sceneEpoch < m_consumedSceneEpoch) return false;
	if (sceneEpoch == m_consumedSceneEpoch) return true;

	// epoch가 바뀌는 프레임 경계에서만 기존 씬 저장소를 한꺼번에 접는다.
	// producer의 SetScene/OnDestroy는 이 맵들을 직접 만지지 않는다.
	{
		SpinLock lock(m_proxyMapFlag);
		m_proxyMap.clear();
	}
	{
		SpinLock lock(m_lightProxyMapFlag);
		m_lightProxyMap.clear();
	}
	{
		SpinLock lock(m_uiProxyMapFlag);
		m_uiProxyMap.clear();
	}
	m_consumedSceneEpoch = sceneEpoch;
	return true;
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
	}
	counts.animators = m_animationJob.GetAnimatorCount();
	// 팔레트는 더 이상 RenderScene registry가 아니다. Animator의 pose를 update
	// delta가 명령 소유 버퍼로 복사하므로 진단 행은 활성 animator와 1:1이다.
	counts.animationPalettes = counts.animators;

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

