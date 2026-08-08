#include "RenderPassData.h"
#include "RHI/RHICommandContext.h"
#include "DeviceState.h"
#include "RenderScene.h"
#include "Material.h"
#include "SceneManager.h"

bool RenderPassData::VaildCheck(Camera* pCamera)
{
	auto renderScene = SceneManagers->GetRenderScene();
	if (pCamera && renderScene)
	{
		if (nullptr != renderScene->GetRenderPassData(pCamera->m_cameraIndex))
		{
			return true;
		}
	}
	return false;
}

RenderPassData* RenderPassData::GetData(Camera* pCamera)
{
	auto renderScene = SceneManagers->GetRenderScene();
	if (pCamera && renderScene)
	{
		auto renderPassData = renderScene->GetRenderPassData(pCamera->m_cameraIndex);
		if (nullptr != renderPassData)
		{
			return renderPassData;
		}
	}

	return nullptr;
}

RenderScene* RenderPassData::GetActiveRenderScene()
{
	return SceneManagers->GetRenderScene();
}

RenderPassData::RenderPassData() : m_shadowCamera(false)
{
}

RenderPassData::~RenderPassData()
{
	ClearRenderQueue();
	ClearShadowRenderQueue();
	ClearCullDataBuffer();
	ClearShadowRenderDataBuffer();
	m_isInitalized = false;

}

void RenderPassData::Initalize(uint32 index)
{
	if (m_isInitalized) return;

	m_index = index;

	// ★ 카메라 DX11 렌더타깃·깊이 생성을 걷었다 (T6, 2026-08-08).
	//   T3이 "EffectSystem 셋이 OMSetRenderTargets에 그대로 건다"며 남겨 둔
	//   것인데, 그 EffectSystem이 PHASE 10-0에서 통째로 사라졌다. 남은
	//   소비자는 크기를 찍는 진단 하나였고 그것도 함께 걷었다.

	m_deferredQueue.reserve(500);
	m_forwardQueue.reserve(500);
	m_shadowRenderQueue.reserve(800);

	// 캐스케이드 스크래치는 여기서 한 번만 크기를 잡는다.
	// 매 프레임 재할당하지 않아야 렌더 스레드가 힙을 건드릴 일이 없다.
	m_cascadeInfo.resize(cascadeCount);
	m_cascadeEnd.reserve(cascadeCount + 1);

	// ── 여기 있던 DX11 자원 넷을 걷어냈다 (T3) ──
	//
	// 카메라마다 그림자 맵(R32_TYPELESS 배열 3장) + 슬라이스 SRV 3 + DSV 3 +
	// SSR 히스토리(화면 크기 RGBA16F) + 뷰/투영 상수 버퍼 둘을 만들고 있었다.
	// 만들고 소멸자에서 놓는 것이 전부였다 — **읽는 코드가 하나도 없다.**
	//
	// DX12 라이브 렌더러가 자기 그림자 맵을 따로 만들면서 이쪽 소비자가
	// 사라졌고, SSR 히스토리는 셰이더가 읽는 유일한 줄이 주석 처리돼 있어
	// 처음부터 닫힌 고리였다(EnhancedSSRPass.h 참고).
	//
	// 뷰/투영 버퍼는 BindFrameCameraBuffers 하나만 썼는데 그 함수도 호출자가
	// 0이었다 — 구 RHI(RHICommandContext)의 마지막 표면이라 함께 지웠다.
	m_shadowCamera.m_isOrthographic = true;

	m_isInitalized = true;
}

Mathf::Vector4 RenderPassData::ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth) const
{
	// 화면 크기는 카메라 상태가 아니라 전역 뷰포트에서 온다 — Camera 쪽 구현과
	// 같은 출처다(그쪽도 GetScreenSize()가 g_Viewport를 읽었다).
	const float width = DirectX11::DeviceStates->g_Viewport.Width;
	const float height = DirectX11::DeviceStates->g_Viewport.Height;

	// 1. 스크린 좌표를 NDC 좌표로 변환
	const float x_ndc = (2.0f * screenPosition.x / width) - 1.0f;
	const float y_ndc = 1.0f - (2.0f * screenPosition.y / height);
	const Mathf::Vector4 screenPositionNDC{ x_ndc, y_ndc, depth, 1.0f };

	// 2. 역투영 → 3. 역뷰. 둘 다 프레임 밀봉 값이다.
	const Mathf::Vector4 viewPosition =
		XMVector3TransformCoord(screenPositionNDC, GetFrameSnapshot().inverseProjection);

	return XMVector3TransformCoord(viewPosition, GetFrameSnapshot().inverseView);
}

void RenderPassData::PushRenderQueue(PrimitiveRenderProxy* proxy)
{
	PrimitiveProxyType	proxyType = proxy->m_proxyType;
	Material*			mat{ nullptr };
	Mesh*				mesh{ nullptr };
	TerrainMaterial*	terrainMat{ nullptr };

	switch (proxyType)
	{
	case PrimitiveProxyType::MeshRenderer:
		mat = proxy->m_Material.get();
		mesh = proxy->m_Mesh.get();
		if (nullptr == mat || nullptr == mesh) 
		{
			return;
		}

		switch (mat->m_renderingMode)
		{
		case MaterialRenderingMode::Opaque:
			m_deferredQueue.push_back(proxy);
			break;
		case MaterialRenderingMode::Transparent:
			m_forwardQueue.push_back(proxy);
			break;
		}

		break;
	case PrimitiveProxyType::FoliageComponent:
		m_foliageQueue.push_back(proxy);
		break;
	case PrimitiveProxyType::TerrainComponent:
		terrainMat = proxy->m_terrainMaterial;
		if (terrainMat != nullptr) {
			// Not assigned RenderingMode.
			m_terrainQueue.push_back(proxy);
		}
		break;
	case PrimitiveProxyType::DecalComponent:
		if (proxy->m_diffuseTexture != nullptr || proxy->m_normalTexture != nullptr || proxy->m_occluroughmetalTexture != nullptr) {
			m_decalQueue.push_back(proxy);
		}
		break;
	case PrimitiveProxyType::SpriteRenderer:
		if (proxy->m_quadMesh != nullptr && proxy->m_spriteTexture != nullptr)
		{
			m_spriteRenderQueue.push_back(proxy);
		}
		break;
	default:
		break;
	}
}

void RenderPassData::SortRenderQueue()
{
	if (!m_deferredQueue.empty())
	{
		std::ranges::sort(
			m_deferredQueue.begin(),
			m_deferredQueue.end(),
			SortByAnimationAndMaterialGuid
		);
	}

	if (!m_forwardQueue.empty())
	{
		std::ranges::sort(
			m_forwardQueue.begin(),
			m_forwardQueue.end(),
			SortByAnimationAndMaterialGuid
		);
	}
}

void RenderPassData::ClearRenderQueue()
{
	m_deferredQueue.clear();
	m_forwardQueue.clear();
	m_terrainQueue.clear();
	m_foliageQueue.clear();
	m_UIRenderQueue.clear();
	m_decalQueue.clear();
    m_spriteRenderQueue.clear();
}

void RenderPassData::PushShadowRenderQueue(PrimitiveRenderProxy* proxy)
{
	m_shadowRenderQueue.push_back(proxy);
}

void RenderPassData::SortShadowRenderQueue()
{
	if (!m_deferredQueue.empty())
	{
		std::sort(
			m_shadowRenderQueue.begin(),
			m_shadowRenderQueue.end(),
		SortByAnimationAndMaterialGuid	
		);
	}
}

void RenderPassData::ClearShadowRenderQueue()
{
	m_shadowRenderQueue.clear();
}

void RenderPassData::PushUIRenderQueue(UIRenderProxy* proxy)
{
	m_UIRenderQueue.push_back(proxy);
}

void RenderPassData::SortUIRenderQueue()
{
	if (!m_UIRenderQueue.empty())
	{
		std::ranges::sort(m_UIRenderQueue, [](const auto& lhs, const auto& rhs)
		{
			if (lhs->GetCanvasOrder() != rhs->GetCanvasOrder())
				return lhs->GetCanvasOrder() < rhs->GetCanvasOrder();
			return lhs->GetLayerOrder() < rhs->GetLayerOrder();
		});
	}
}

void RenderPassData::ClearUIRenderQueue()
{
	m_UIRenderQueue.clear();
}

void RenderPassData::PushCullData(const HashedGuid& instanceID)
{
	size_t index = m_frame.load(std::memory_order_relaxed) % 3;
	m_findProxyVec[index].push_back(instanceID);
}

RenderPassData::FrameProxyFindInstanceIDs& RenderPassData::GetCullDataBuffer()
{
	size_t prevIndex = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
	return m_findProxyVec[prevIndex];
}

void RenderPassData::ClearCullDataBuffer()
{
	size_t prevIndex = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
	m_findProxyVec[prevIndex].clear();
}

void RenderPassData::PushShadowRenderData(const HashedGuid& instanceID)
{
	size_t index = m_frame.load(std::memory_order_relaxed) % 3;
	m_findShadowProxyVec[index].push_back(instanceID);
}

RenderPassData::FrameProxyFindInstanceIDs& RenderPassData::GetShadowRenderDataBuffer()
{
	size_t prevIndex = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
	return m_findShadowProxyVec[prevIndex];
}

void RenderPassData::ClearShadowRenderDataBuffer()
{
	size_t prevIndex = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
	m_findShadowProxyVec[prevIndex].clear();
}

void RenderPassData::PushUIRenderData(const HashedGuid& instanceID)
{
	size_t index = m_frame.load(std::memory_order_relaxed) % 3;
	m_findUIProxyVec[index].push_back(instanceID);
}

RenderPassData::FrameUIProxyIDs& RenderPassData::GetUIRenderDataBuffer()
{
	size_t prevIndex = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
	return m_findUIProxyVec[prevIndex];
}

void RenderPassData::ClearUIRenderDataBuffer()
{
	size_t prevIndex = (m_frame.load(std::memory_order_relaxed) + 1) % 3;
	m_findUIProxyVec[prevIndex].clear();
}
