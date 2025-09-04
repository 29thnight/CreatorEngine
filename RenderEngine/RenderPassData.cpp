#include "RenderPassData.h"
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

	m_renderTarget.reset();
	m_depthStencil.reset();
	for (auto& srv : sliceSRV)
	{
		Memory::SafeDelete(srv);
	}

	for (auto& dsv : m_shadowMapDSVarr)
	{
		Memory::SafeDelete(dsv);
	}

	m_shadowMapTexture.reset();
	m_SSRPrevTexture.reset();
	m_ViewBuffer.Reset();
	m_ProjBuffer.Reset();
}

void RenderPassData::Initalize(uint32 index)
{
	if (m_isInitalized) return;

	m_index = index;

	std::string cameraRTVName = "RenderPassData(" + std::to_string(index) + ") RTV";

	auto renderTexture = TextureHelper::CreateRenderTexture(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		cameraRTVName,
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
	m_renderTarget.swap(renderTexture);

	auto depthStencil = TextureHelper::CreateDepthTexture(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"RenderPassData(" + std::to_string(index) + ") DSV"
	);
	m_depthStencil.swap(depthStencil);

	m_deferredQueue.reserve(500);
	m_forwardQueue.reserve(500);
	m_shadowRenderQueue.reserve(800);

	ShadowMapRenderDesc& desc = RenderScene::g_shadowMapDesc;
	auto shadowMapTexture = Texture::CreateManagedArray(
		desc.m_textureWidth,
		desc.m_textureHeight,
		"Shadow Map",
		DXGI_FORMAT_R32_TYPELESS,
		D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
		cascadeCount
	);
	shadowMapTexture->m_textureType = TextureType::ImageTexture;

	auto ssrTexture = TextureHelper::CreateRenderTexture(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"prevSSRTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
	m_SSRPrevTexture.swap(ssrTexture);

	for (int i = 0; i < cascadeCount; ++i)
	{
		sliceSRV[i] = DirectX11::CreateSRVForArraySlice(DirectX11::DeviceStates->g_pDevice, shadowMapTexture->m_pTexture, DXGI_FORMAT_R32_FLOAT, i);
	}

	for (int i = 0; i < cascadeCount; i++)
	{
		CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
		depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		depthStencilViewDesc.Texture2DArray.ArraySize = 1;
		depthStencilViewDesc.Texture2DArray.FirstArraySlice = i;

		DirectX11::ThrowIfFailed(
			DirectX11::DeviceStates->g_pDevice->CreateDepthStencilView(
				shadowMapTexture->m_pTexture,
				&depthStencilViewDesc,
				&m_shadowMapDSVarr[i]
			)
		);

	}

	//안에서 배열은 3으로 고정중 필요하면 수정
	shadowMapTexture->CreateSRV(DXGI_FORMAT_R32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2DARRAY);
	shadowMapTexture->m_textureType = TextureType::ImageTexture;
	m_shadowMapTexture.swap(shadowMapTexture);

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
	PrimitiveProxyType	proxyType = proxy->m_proxyType;
	Material*			mat{ nullptr };
	Mesh*				mesh{ nullptr };
	TerrainMaterial*	terrainMat{ nullptr };

	switch (proxyType)
	{
	case PrimitiveProxyType::MeshRenderer:
		mat = proxy->m_Material;
		mesh = proxy->m_Mesh;
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
	default:
		break;
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
	m_terrainQueue.clear();
	m_foliageQueue.clear();
	m_UIRenderQueue.clear();
	m_decalQueue.clear();
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
		std::sort(
			m_UIRenderQueue.begin(),
			m_UIRenderQueue.end(),
			[](UIRenderProxy* a, UIRenderProxy* b) {
				int layerA = 0;
				int layerB = 0;
				if (std::holds_alternative<UIRenderProxy::ImageData>(a->m_data)) {
					layerA = std::get<UIRenderProxy::ImageData>(a->m_data).layerOrder;
				}
				else if (std::holds_alternative<UIRenderProxy::TextData>(a->m_data)) {
					layerA = std::get<UIRenderProxy::TextData>(a->m_data).layerOrder;
				}
				if (std::holds_alternative<UIRenderProxy::ImageData>(b->m_data)) {
					layerB = std::get<UIRenderProxy::ImageData>(b->m_data).layerOrder;
				}
				else if (std::holds_alternative<UIRenderProxy::TextData>(b->m_data)) {
					layerB = std::get<UIRenderProxy::TextData>(b->m_data).layerOrder;
				}
				return layerA < layerB;
			}
		);
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
