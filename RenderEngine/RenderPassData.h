#pragma once
#include "Camera.h"
#include "Texture.h"

class Camera;
class MeshRendererProxy;
class RenderPassData
{
public:
	using ProxyContainer = std::vector<MeshRendererProxy*>;
	static constexpr int cascadeCount = 3;
public:
	UniqueTexturePtr		  m_renderTarget{ TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr		  m_depthStencil{ TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr		  m_shadowMapTexture{ TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr		  m_SSRPrevTexture{ TEXTURE_NULL_INITIALIZER };
	ID3D11DepthStencilView*   m_shadowMapDSVarr[cascadeCount]{};
	ID3D11ShaderResourceView* sliceSRV[3]{};
	ProxyContainer			  m_deferredQueue;
	ProxyContainer			  m_forwardQueue;
	Camera					  m_shadowCamera;
	std::atomic_bool          m_isInitalized{ false };
	std::mutex				  m_dataMutex;

	RenderPassData();
	~RenderPassData();

	void Initalize(uint32 index);

	void PushRenderQueue(MeshRendererProxy* proxy);
	void SortRenderQueue();
	void ClearRenderQueue();

	void ClearRenderTarget();

	static bool VaildCheck(Camera* pCamera);
	static RenderPassData* GetData(Camera* pCamera);
};