#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Camera.h"
#include "Texture.h"
#include "concurrent_vector.h"

using namespace concurrency;
class Camera;
class PrimitiveRenderProxy;

class RenderPassData
{
public:
	using ProxyContainer = concurrent_vector<PrimitiveRenderProxy*>;
	using FrameProxyFindInstanceIDs = concurrent_vector<HashedGuid>;
	static constexpr int STORE_FRAME_COUNT = 3;
	static constexpr int cascadeCount = 3;
public:
	Managed::UniquePtr<Texture> m_renderTarget;
	Managed::UniquePtr<Texture> m_depthStencil;
	Managed::UniquePtr<Texture> m_shadowMapTexture;
	Managed::UniquePtr<Texture> m_SSRPrevTexture;
	ID3D11DepthStencilView*   m_shadowMapDSVarr[cascadeCount]{};
	ID3D11ShaderResourceView* sliceSRV[cascadeCount]{};
	FrameProxyFindInstanceIDs m_findProxyVec[STORE_FRAME_COUNT];
	FrameProxyFindInstanceIDs m_findShadowProxyVec[STORE_FRAME_COUNT];
	ProxyContainer			  m_deferredQueue;
	ProxyContainer			  m_forwardQueue;
	ProxyContainer            m_terrainQueue;
	ProxyContainer			  m_foliageQueue;
	ProxyContainer			  m_shadowRenderQueue;
	Camera					  m_shadowCamera;
	//flags
	std::atomic_bool          m_isInitalized{ false };
	std::atomic_bool          m_isDestroy{ false };
	std::atomic<uint32>		  m_index{ 0 };
	std::atomic<uint32>       m_frame{};

	Mathf::xMatrix            m_frameCalculatedView{};
	Mathf::xMatrix            m_frameCalculatedProjection{};

	ComPtr<ID3D11Buffer>	  m_ViewBuffer;
	ComPtr<ID3D11Buffer>	  m_ProjBuffer;

	RenderPassData();
	~RenderPassData();

	void Initalize(uint32 index);

	void PushRenderQueue(PrimitiveRenderProxy* proxy);
	void SortRenderQueue();
	void ClearRenderQueue();

	void PushShadowRenderQueue(PrimitiveRenderProxy* proxy);
	void SortShadowRenderQueue();
	void ClearShadowRenderQueue();

	void PushCullData(const HashedGuid& instanceID);
	FrameProxyFindInstanceIDs& GetCullDataBuffer();
	void ClearCullDataBuffer();

	void PushShadowRenderData(const HashedGuid& instanceID);
	FrameProxyFindInstanceIDs& GetShadowRenderDataBuffer();
	void ClearShadowRenderDataBuffer();

	void UpdateData(Camera* pCamera) {
		m_frameCalculatedView = pCamera->CalculateView();
		m_frameCalculatedProjection = pCamera->CalculateProjection();
	}

	void AddFrame()
	{
		m_frame.fetch_add(1, std::memory_order_relaxed);
	}

	void ClearRenderTarget();

	static bool VaildCheck(Camera* pCamera);
	static RenderPassData* GetData(Camera* pCamera);
};
#endif // !DYNAMICCPP_EXPORTS