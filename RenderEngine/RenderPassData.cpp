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

	m_deferredQueue.reserve(300);
	m_forwardQueue.reserve(300);

	ShadowMapRenderDesc& desc = RenderScene::g_shadowMapDesc;
	Texture* shadowMapTexture = Texture::CreateArray(
		desc.m_textureWidth,
		desc.m_textureHeight,
		"Shadow Map",
		DXGI_FORMAT_R32_TYPELESS,
		D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
		cascadeCount
	);

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
		std::unique_lock lock(m_dataMutex);

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
	std::unique_lock lock(m_dataMutex);

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
	std::unique_lock lock(m_dataMutex);

	m_deferredQueue.clear();
	m_forwardQueue.clear();
}

