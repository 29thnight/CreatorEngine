#include "RenderScene.h"
#include "ImGuiRegister.h"
#include "../ScriptBinder/Scene.h"
#include "LightProperty.h"
#include "../ScriptBinder/RenderableComponents.h"
#include "Skeleton.h"
#include "LightController.h"
#include "Benchmark.hpp"
#include "MeshRenderer.h"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneManager.h"
#include "RenderCommand.h"
#include "ImageComponent.h"
#include "UIManager.h"

constexpr size_t TRANSFORM_SIZE = sizeof(Mathf::xMatrix) * MAX_BONES;

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

	m_isInitalized = true;
}

ID3D11RenderTargetView* RenderPassData::GetRTV()
{
	return m_renderTarget->GetRTV();
}

ID3D11DepthStencilView* RenderPassData::GetDSV()
{
	return m_renderTarget->m_pDSV;
}

ID3D11ShaderResourceView*& RenderPassData::GetDepthSRV()
{
	return m_depthStencil->m_pSRV;
}

ID3D11ShaderResourceView*& RenderPassData::GetShadowSRV()
{
	return m_shadowMapTexture->m_pSRV;
}

void RenderPassData::ClearRenderTarget()
{
	DirectX11::ClearRenderTargetView(m_renderTarget->GetRTV(), DirectX::Colors::Transparent);
	DirectX11::ClearDepthStencilView(m_depthStencil->m_pDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void RenderPassData::PushRenderQueue(MeshRendererProxy* proxy)
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

ShadowMapRenderDesc RenderScene::g_shadowMapDesc{};

RenderScene::~RenderScene()
{
	Memory::SafeDelete(m_LightController);
}

void RenderScene::Initialize()
{
	m_LightController = new LightController();
	m_shadowRenderQueue.reserve(500);
}

void RenderScene::SetBuffers(ID3D11Buffer* modelBuffer)
{
	m_ModelBuffer = modelBuffer;
}

void RenderScene::Update(float deltaSecond)
{
	m_currentScene = SceneManagers->GetActiveScene();
	if (m_currentScene == nullptr) return;

    m_LightController->m_lightCount = m_currentScene->UpdateLight(m_LightController->m_lightProperties);
}

void RenderScene::ShadowStage(Camera& camera)
{
	m_LightController->SetEyePosition(camera.m_eyePosition);
	m_LightController->Update();
	m_LightController->RenderAnyShadowMap(*this, camera);
}

void RenderScene::CreateShadowCommandList(Camera& camera)
{
	m_LightController->CreateShadowCommandList(*this, camera);
}

void RenderScene::UseModel()
{
	DirectX11::VSSetConstantBuffer(0, 1, &m_ModelBuffer);
}

void RenderScene::UseModel(ID3D11DeviceContext* deferredContext)
{
	deferredContext->VSSetConstantBuffers(0, 1, &m_ModelBuffer);
}

void RenderScene::UpdateModel(const Mathf::xMatrix& model)
{
	DirectX11::UpdateBuffer(m_ModelBuffer, &model);
}

void RenderScene::UpdateModel(const Mathf::xMatrix& model, ID3D11DeviceContext* deferredContext)
{
	deferredContext->UpdateSubresource(m_ModelBuffer, 0, nullptr, &model, 0, 0);
}

//RenderPassData* RenderScene::AddRenderPassData(size_t cameraIndex)
//{
//	auto it = m_renderDataMap.find(cameraIndex);
//	if (it != m_renderDataMap.end())
//	{
//		return m_renderDataMap[cameraIndex].get();
//	}
//
//	auto newRenderData = std::make_shared<RenderPassData>();
//	m_renderDataMap[cameraIndex] = newRenderData;
//
//	return newRenderData.get();
//}
//
//RenderPassData* RenderScene::GetRenderPassData(size_t cameraIndex)
//{
//	auto it = m_renderDataMap.find(cameraIndex);
//	if (it == m_renderDataMap.end())
//	{
//		return nullptr;
//	}
//
//	return m_renderDataMap[cameraIndex].get();
//}

void RenderScene::RegisterAnimator(Animator* animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	if (m_animatorMap.find(animatorGuid) != m_animatorMap.end()) return;

	m_animatorMap[animatorGuid] = animatorPtr;
}

void RenderScene::UnregisterAnimator(Animator* animatorPtr)
{
	if (nullptr == animatorPtr) return;

	HashedGuid animatorGuid = animatorPtr->GetInstanceID();

	if (m_animatorMap.find(animatorGuid) != m_animatorMap.end()) return;

	m_animatorMap.erase(animatorGuid);
}

void RenderScene::RegisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	if (m_proxyMap.find(meshRendererGuid) != m_proxyMap.end()) return;

	// Create a new proxy for the mesh renderer and insert it into the map
	auto managedCommand = std::make_shared<MeshRendererProxy>(meshRendererPtr);
	m_proxyMap[meshRendererGuid] = managedCommand;
}

MeshRendererProxy* RenderScene::FindProxy(size_t guid)
{
	if (m_proxyMap.find(guid) == m_proxyMap.end()) return nullptr;

	return m_proxyMap[guid].get();
}

void RenderScene::UpdateCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr || meshRendererPtr->IsDestroyMark()) return;

	auto owner = meshRendererPtr->GetOwner();
	if(nullptr == owner || owner->IsDestroyMark()) return;

	Mathf::xMatrix worldMatrix = owner->m_transform.GetWorldMatrix();
	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	if (m_proxyMap.find(meshRendererGuid) == m_proxyMap.end()) return;
	
	auto& proxyObject = m_proxyMap[meshRendererGuid];

	if (nullptr == proxyObject) return;

	HashedGuid aniGuid = proxyObject->m_animatorGuid;

	if(m_animatorMap.find(aniGuid) != m_animatorMap.end() 
		&& proxyObject->IsSkinnedMesh())
	{
		auto* dstPalete = &proxyObject->m_finalTransforms;
		auto* srcPalete = &m_animatorMap[aniGuid]->m_FinalTransforms;

		memcpy(dstPalete, srcPalete, TRANSFORM_SIZE);
	}

	proxyObject->m_worldMatrix = worldMatrix;

	constexpr int INVAILD_INDEX = -1;

	int& lightMapIndex		= meshRendererPtr->m_LightMapping.lightmapIndex;
	int& proxyLightMapIndex = proxyObject->m_LightMapping.lightmapIndex;

	if(INVAILD_INDEX != lightMapIndex && proxyLightMapIndex != lightMapIndex)
	{
		proxyObject->m_LightMapping = meshRendererPtr->m_LightMapping;
	}
}

void RenderScene::UnregisterCommand(MeshRenderer* meshRendererPtr)
{
	if (nullptr == meshRendererPtr) return;

	HashedGuid meshRendererGuid = meshRendererPtr->GetInstanceID();

	if (m_proxyMap.find(meshRendererGuid) == m_proxyMap.end()) return;

	m_proxyMap.erase(meshRendererGuid);
}

void RenderScene::PushShadowRenderQueue(MeshRendererProxy* proxy)
{
	std::unique_lock lock(m_shadowRenderMutex);

	m_shadowRenderQueue.push_back(proxy);
}

RenderScene::ProxyContainer RenderScene::GetShadowRenderQueue()
{
	std::unique_lock lock(m_shadowRenderMutex);

	return m_shadowRenderQueue;
}

void RenderScene::ClearShadowRenderQueue()
{
	std::unique_lock lock(m_shadowRenderMutex);

	m_shadowRenderQueue.clear();
}