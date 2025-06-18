#pragma once
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
	UniqueTexturePtr		  m_renderTarget{ TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr		  m_depthStencil{ TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr		  m_shadowMapTexture{ TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr		  m_SSRPrevTexture{ TEXTURE_NULL_INITIALIZER };
	ID3D11DepthStencilView*   m_shadowMapDSVarr[cascadeCount]{};
	ID3D11ShaderResourceView* sliceSRV[cascadeCount]{};
	FrameProxyFindInstanceIDs m_findProxyVec[STORE_FRAME_COUNT];
	FrameProxyFindInstanceIDs m_findShadowProxyVec[STORE_FRAME_COUNT];
	ProxyContainer			  m_deferredQueue;
	ProxyContainer			  m_forwardQueue;
	ProxyContainer			  m_shadowRenderQueue;
	Camera					  m_shadowCamera;
	std::atomic_bool          m_isInitalized{ false };
	std::atomic<uint32>       m_frame{};

	Mathf::xMatrix            m_frameCalculatedView{};
	Mathf::xMatrix            m_frameCalculatedProjection{};

	ComPtr<ID3D11Buffer>	m_ViewBuffer;
	ComPtr<ID3D11Buffer>	m_ProjBuffer;

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

	void AddFrame()
	{
		m_frame.fetch_add(1, std::memory_order_relaxed);
	}

	void ClearRenderTarget();

	static bool VaildCheck(Camera* pCamera);
	static RenderPassData* GetData(Camera* pCamera);
};