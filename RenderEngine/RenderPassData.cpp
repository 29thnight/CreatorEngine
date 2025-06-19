#include "RenderPassData.h"
#include "DeviceState.h"
#include "RenderScene.h"
#include "Material.h"

bool RenderPassData::VaildCheck(Camera* pCamera)
{
	auto renderScene = SceneManagers->m_ActiveRenderScene;
	if (pCamera && renderScene)
	{
		if (nullptr != renderScene->GetRenderPassData(pCamera->m_cameraIndex))
		{
			return true;
		}
	}

	//std::cout << "Invaild data" << std::endl;
	return false;
}

RenderPassData* RenderPassData::GetData(Camera* pCamera)
{
	auto renderScene = SceneManagers->m_ActiveRenderScene;
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

RenderPassData::RenderPassData() : m_shadowCamera(true)
{
}

RenderPassData::~RenderPassData()
{
	ClearRenderQueue();
}

void RenderPassData::Initalize(uint32 index)
{
	if (m_isInitalized) return;

	std::string cameraRTVName = "RenderPassData(" + std::to_string(index) + ") RTV";

	auto renderTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		cameraRTVName,
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
	m_renderTarget.swap(renderTexture);


	auto depthStencil = TextureHelper::CreateDepthTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"RenderPassData(" + std::to_string(index) + ") DSV"
	);
	m_depthStencil.swap(depthStencil);

	m_deferredQueue.reserve(500);
	m_forwardQueue.reserve(500);
	m_shadowRenderQueue.reserve(800);

	ShadowMapRenderDesc& desc = RenderScene::g_shadowMapDesc;
	Texture* shadowMapTexture = Texture::CreateArray(
		desc.m_textureWidth,
		desc.m_textureHeight,
		"Shadow Map",
		DXGI_FORMAT_R32_TYPELESS,
		D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
		cascadeCount
	);
	shadowMapTexture->m_textureType = TextureType::ImageTexture;

	auto ssrTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"prevSSRTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
	m_SSRPrevTexture.swap(ssrTexture);

	for (int i = 0; i < cascadeCount; ++i)
	{
		sliceSRV[i] = DirectX11::CreateSRVForArraySlice(DeviceState::g_pDevice, shadowMapTexture->m_pTexture, DXGI_FORMAT_R32_FLOAT, i);
	}

	for (int i = 0; i < cascadeCount; i++)
	{
		CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
		depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		depthStencilViewDesc.Texture2DArray.ArraySize = 1;
		depthStencilViewDesc.Texture2DArray.FirstArraySlice = i;

		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateDepthStencilView(
				shadowMapTexture->m_pTexture,
				&depthStencilViewDesc,
				&m_shadowMapDSVarr[i]
			)
		);

	}

	//안에서 배열은 3으로 고정중 필요하면 수정
	shadowMapTexture->CreateSRV(DXGI_FORMAT_R32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2DARRAY);
	shadowMapTexture->m_textureType = TextureType::ImageTexture;
	m_shadowMapTexture = MakeUniqueTexturePtr(shadowMapTexture);

	XMMATRIX identity = XMMatrixIdentity();

	std::string viewBufferName = "Camera(" + std::to_string(index) + ")ViewBuffer";
	std::string projBufferName = "Camera(" + std::to_string(index) + ")ProjBuffer";

	m_ViewBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ViewBuffer.Get(), viewBufferName.c_str());
	m_ProjBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ProjBuffer.Get(), projBufferName.c_str());

	m_shadowCamera.m_isOrthographic = true;

	m_isInitalized = true;
}

void RenderPassData::ClearRenderTarget()
{
	DirectX11::ClearRenderTargetView(m_renderTarget->GetRTV(), DirectX::Colors::Transparent);
	DirectX11::ClearDepthStencilView(m_depthStencil->m_pDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void RenderPassData::PushRenderQueue(PrimitiveRenderProxy* proxy)
{
	Material* mat = proxy->m_Material;
	if (nullptr == mat) return;

	{
		switch (mat->m_renderingMode)
		{
		case MaterialRenderingMode::Opaque:
			m_deferredQueue.push_back(proxy);
			break;
		case MaterialRenderingMode::Transparent:
			m_forwardQueue.push_back(proxy);
			break;
		}
	}
}

void RenderPassData::SortRenderQueue()
{
	if (!m_deferredQueue.empty())
	{
		std::sort(
			m_deferredQueue.begin(),
			m_deferredQueue.end(),
			SortByAnimationAndMaterialGuid
		);
	}

	if (!m_forwardQueue.empty())
	{
		std::sort(
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

