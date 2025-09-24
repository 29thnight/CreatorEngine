#include "ForwardPass.h"
#include "ShaderSystem.h"
#include "Material.h"
#include "Skeleton.h"
#include "Scene.h"
#include "RenderableComponents.h"
#include "Mesh.h"
#include "LightController.h"
#include "LightProperty.h"
#include "Benchmark.hpp"
#include "MeshRendererProxy.h"
#include "RenderDebugManager.h"

struct alignas(16) ForwardBuffer
{
	bool32 m_useEnvironmentMap{};
	float m_envMapIntensity{ 1.f };
};

struct alignas(16) MatrixBuffer {
	XMMATRIX View;
	XMMATRIX Proj;
	float add;
};

ForwardPass::ForwardPass()
{
	m_pso = std::make_unique<PipelineStateObject>();
	m_instancePSO = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Forward"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	m_instancePSO->m_vertexShader = &ShaderSystem->VertexShaders["InstancedVertexShader"];
	m_instancePSO->m_pixelShader = &ShaderSystem->PixelShaders["Forward"];
	m_instancePSO->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	InputLayOutContainer vertexLayoutDesc = {
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	m_pso->CreateInputLayout(std::move(vertexLayoutDesc));

	InputLayOutContainer instanceVertexLayoutDesc = {
	{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	//{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	//{ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	m_instancePSO->CreateInputLayout(std::move(instanceVertexLayoutDesc));

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	m_instancePSO->m_samplers.push_back(linearSampler);
	m_instancePSO->m_samplers.push_back(pointSampler);

	m_Buffer = DirectX11::CreateBuffer(sizeof(ForwardBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_materialBuffer = DirectX11::CreateBuffer(sizeof(MaterialInfomation), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_boneBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * Skeleton::MAX_BONES, D3D11_BIND_CONSTANT_BUFFER, nullptr);

	constexpr uint32 MAX_INSTANCES = 2048; // Max number of instances per draw call
	m_maxInstanceCount = MAX_INSTANCES;

	/*CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
	rasterizerDesc.CullMode = D3D11_CULL_NONE;

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_instancePSO->m_rasterizerState
		)
	);*/

	D3D11_BUFFER_DESC instanceBufferDesc{};
	instanceBufferDesc.ByteWidth = sizeof(Mathf::xMatrix) * MAX_INSTANCES;
	instanceBufferDesc.Usage = D3D11_USAGE_DEFAULT; // Changed for UpdateSubresource
	instanceBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	instanceBufferDesc.CPUAccessFlags = 0; // No direct CPU access needed
	instanceBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	instanceBufferDesc.StructureByteStride = sizeof(Mathf::xMatrix);

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateBuffer(&instanceBufferDesc, nullptr, &m_instanceBuffer)
	);

	// Create a shader resource view for the instance buffer
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = MAX_INSTANCES;

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_instanceBuffer.Get(), &srvDesc, &m_instanceBufferSRV)
	);

	CD3D11_DEPTH_STENCIL_DESC depthStencilDesc2(D3D11_DEFAULT);
	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(
			&depthStencilDesc2,
			&m_depthNoWrite
		)
	);

	CD3D11_BLEND_DESC1 blendDesc = CD3D11_BLEND_DESC1(CD3D11_DEFAULT());
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateBlendState1(
			&blendDesc,
			&m_blendPassState
		)
	);

	m_CopiedTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_CopiedTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_CopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_MatrixBuffer = DirectX11::CreateBuffer(sizeof(MatrixBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

ForwardPass::~ForwardPass()
{
}

void ForwardPass::UseEnvironmentMap(
	Managed::SharedPtr<Texture> envMap,
	Managed::SharedPtr<Texture> preFilter,
	Managed::SharedPtr<Texture> brdfLut)
{
	m_EnvironmentMap = envMap;
	m_PreFilter = preFilter;
	m_BrdfLut = brdfLut;
}

void ForwardPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void ForwardPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	if(m_EnvironmentMap.expired() || 
	   m_PreFilter.expired() || 
	   m_BrdfLut.expired())
	{
		return; // Ensure environment maps are set before proceeding
	}

	auto envMap = m_EnvironmentMap.lock();
	auto preFilter = m_PreFilter.lock();
	auto brdfLut = m_BrdfLut.lock();

	ID3D11DeviceContext* deferredPtr = deferredContext;

	DirectX11::CopyResource(deferredPtr, m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);
	MatrixBuffer matrixBuffer{};
	matrixBuffer.Proj = renderData->m_frameCalculatedProjection;
	matrixBuffer.View = renderData->m_frameCalculatedView;
	matrixBuffer.add = add;
	DirectX11::UpdateBuffer(deferredPtr, m_MatrixBuffer.Get(), &matrixBuffer);
	DirectX11::PSSetConstantBuffer(deferredPtr, 12, 1, m_MatrixBuffer.GetAddressOf());
	DirectX11::PSSetShaderResources(deferredPtr, 15, 1, &m_CopiedTexture->m_pSRV);
	DirectX11::PSSetShaderResources(deferredPtr, 16, 1, &m_normalTexture->m_pSRV);


	using InstanceGroupKey = PrimitiveRenderProxy::ProxyFilter;
	std::vector<PrimitiveRenderProxy*> animatedProxies;
	std::map<std::string, std::vector<PrimitiveRenderProxy*>> shaderPSOGroups;
	std::map<InstanceGroupKey, std::vector<PrimitiveRenderProxy*>> instanceGroups;

	for (auto& proxy : renderData->m_forwardQueue)
	{
		if (proxy->m_isAnimationEnabled && HashedGuid::INVAILD_ID != proxy->m_animatorGuid)
		{
			animatedProxies.push_back(proxy);
		}
		else if (proxy->m_Material->m_shaderPSO)
		{
			shaderPSOGroups[proxy->m_Material->m_shaderPSO->m_shaderPSOName].push_back(proxy);
		}
		else
		{
			InstanceGroupKey key
			{
				proxy->m_materialGuid,
				proxy->m_Mesh->m_hashingMesh,
				proxy->m_EnableLOD,
				proxy->GetLODLevel(&camera),
			};
			instanceGroups[key].push_back(std::move(proxy));
		}
	}

	m_pso->Apply(deferredPtr);

	ForwardBuffer buffer{};
	buffer.m_envMapIntensity = m_envMapIntensity;
	buffer.m_useEnvironmentMap = m_UseEnvironmentMap;
	DirectX11::UpdateBuffer(deferredPtr, m_Buffer.Get(), &buffer);

	ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(deferredPtr, 1, &view, renderData->m_depthStencil->m_pDSV);
	DirectX11::RSSetViewports(deferredPtr, 1, &DirectX11::DeviceStates->g_Viewport);
	scene.UseModel(deferredPtr);
	DirectX11::OMSetDepthStencilState(deferredPtr, m_depthNoWrite.Get(), 1);
	float blend_factor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	UINT sample_mask = 0xffffffff;
	DirectX11::OMSetBlendState(deferredPtr, m_blendPassState.Get(), blend_factor, sample_mask);
	DirectX11::VSSetConstantBuffer(deferredPtr, 3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(deferredPtr, 1, 1, &scene.m_LightController->m_pLightBuffer);
	DirectX11::PSSetConstantBuffer(deferredPtr, 0, 1, m_materialBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(deferredPtr, 3, 1, m_Buffer.GetAddressOf());

	ID3D11ShaderResourceView* envSRVs[3] = {
		m_UseEnvironmentMap ? envMap->m_pSRV : nullptr,
		m_UseEnvironmentMap ? preFilter->m_pSRV : nullptr,
		m_UseEnvironmentMap ? brdfLut->m_pSRV : nullptr
	};
	DirectX11::PSSetShaderResources(deferredPtr, 6, 3, envSRVs);

	auto& lightManager = scene.m_LightController;
	if (lightManager->hasLightWithShadows) {
		DirectX11::PSSetShaderResources(deferredPtr, 4, 1, &renderData->m_shadowMapTexture->m_pSRV);
		DirectX11::PSSetConstantBuffer(deferredPtr, 2, 1, &lightManager->m_shadowMapBuffer);
		lightManager->PSBindCloudShadowMap(deferredPtr);
	}

	camera.UpdateBuffer(deferredPtr);

	HashedGuid currentAnimatorGuid{};
	HashedGuid currentMaterialGuid{};

	for (auto& proxy : animatedProxies)
	{
		scene.UpdateModel(proxy->m_worldMatrix, deferredPtr);

		if (proxy->m_finalTransforms && proxy->m_animatorGuid != currentAnimatorGuid)
		{
			DirectX11::UpdateBuffer(deferredPtr, m_boneBuffer.Get(), proxy->m_finalTransforms);
			currentAnimatorGuid = proxy->m_animatorGuid;
		}

		Material* mat = proxy->m_Material;
		auto matinfo = mat->m_materialInfo;
		matinfo.m_bitflag |= proxy->m_isShadowRecive ? MaterialInfomation::USE_SHADOW_RECIVE : 0;
		if (proxy->m_materialGuid != currentMaterialGuid)
		{
			DirectX11::UpdateBuffer(deferredPtr, m_materialBuffer.Get(), &matinfo);
			if (mat->m_pBaseColor) DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &mat->m_pBaseColor->m_pSRV);
			if (mat->m_pNormal) DirectX11::PSSetShaderResources(deferredPtr, 1, 1, &mat->m_pNormal->m_pSRV);
			if (mat->m_pOccRoughMetal) DirectX11::PSSetShaderResources(deferredPtr, 2, 1, &mat->m_pOccRoughMetal->m_pSRV);
			if (mat->m_AOMap) DirectX11::PSSetShaderResources(deferredPtr, 3, 1, &mat->m_AOMap->m_pSRV);
			if (mat->m_pEmissive) DirectX11::PSSetShaderResources(deferredPtr, 5, 1, &mat->m_pEmissive->m_pSRV);
			currentMaterialGuid = proxy->m_materialGuid;
		}

		proxy->Draw(deferredPtr);
	}

	m_instancePSO->Apply(deferredPtr);
	// Bind the pre-created instance buffer SRV to the vertex shader once.
	DirectX11::OMSetDepthStencilState(deferredPtr, m_depthNoWrite.Get(), 1);
	DirectX11::OMSetBlendState(deferredPtr, m_blendPassState.Get(), blend_factor, sample_mask);
	DirectX11::VSSetShaderResources(deferredPtr, 0, 1, m_instanceBufferSRV.GetAddressOf());

	for (auto const& [groupKey, proxies] : instanceGroups)
	{
		if (proxies.empty()) continue;
		assert(proxies.size() <= m_maxInstanceCount && "Exceeded maximum instance count!");

		const auto& groupMaterialGuid = groupKey.materialGuid;
		auto firstProxy = proxies.front();

		// *** THE KEY OPTIMIZATION IS HERE ***
		// --- Set material once per group ---
		// Only update material state if it has changed from the previous group.
		Material* mat = firstProxy->m_Material;
		auto matinfo = mat->m_materialInfo;
		matinfo.m_bitflag |= firstProxy->m_isShadowRecive ? MaterialInfomation::USE_SHADOW_RECIVE : 0;
		if (groupMaterialGuid != currentMaterialGuid)
		{
			DirectX11::UpdateBuffer(deferredPtr, m_materialBuffer.Get(), &matinfo);
			if (mat->m_pBaseColor) DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &mat->m_pBaseColor->m_pSRV);
			if (mat->m_pNormal) DirectX11::PSSetShaderResources(deferredPtr, 1, 1, &mat->m_pNormal->m_pSRV);
			if (mat->m_pOccRoughMetal) DirectX11::PSSetShaderResources(deferredPtr, 2, 1, &mat->m_pOccRoughMetal->m_pSRV);
			if (mat->m_AOMap) DirectX11::PSSetShaderResources(deferredPtr, 3, 1, &mat->m_AOMap->m_pSRV);
			if (mat->m_pEmissive) DirectX11::PSSetShaderResources(deferredPtr, 5, 1, &mat->m_pEmissive->m_pSRV);
		}

		// --- Update the instance data buffer using UpdateSubresource ---
		// This is safer for deferred contexts than Map/Unmap.
		std::vector<Mathf::xMatrix> instanceMatrices;
		instanceMatrices.reserve(proxies.size());
		for (const auto& proxy : proxies)
		{
			instanceMatrices.push_back(proxy->m_worldMatrix);
		}

		// Define the destination box to specify the exact region to update.
		// This prevents reading past the end of the source data.
		D3D11_BOX destBox;
		destBox.left = 0;
		destBox.right = proxies.size() * sizeof(Mathf::xMatrix);
		destBox.top = 0;
		destBox.bottom = 1;
		destBox.front = 0;
		destBox.back = 1;

		deferredPtr->UpdateSubresource(m_instanceBuffer.Get(), 0, &destBox, instanceMatrices.data(), 0, 0);

		// --- Draw all instances in one call ---
		firstProxy->DrawInstanced(deferredPtr, proxies.size());
	}

	ID3D11ShaderResourceView* nullSRVs = { nullptr };

	// --- 3.5 RENDER OBJECTS WITH CUSTOM SHADER PSO (INDIVIDUALLY) ---
	for (auto const& [psoName, proxies] : shaderPSOGroups)
	{
		if (proxies.empty()) continue;
		auto firstProxy = proxies.front();
		auto customPSO = firstProxy->m_Material->m_shaderPSO;
		if (!customPSO) continue;
		//TEST: PSO가 유효하지 않다면 ShaderSystem에서 동일 이름의 PSO를 찾아 교체
		auto& shaderPSOContainer = ShaderSystem->ShaderAssets;

		if (customPSO->IsInvalidated())
		{
			if (shaderPSOContainer.find(psoName) != shaderPSOContainer.end())
			{
				for (auto* proxy : proxies)
				{
					proxy->m_Material->SetShaderPSO(nullptr); // 기존 PSO 해제
					proxy->m_Material->SetShaderPSO(shaderPSOContainer[psoName]);
				}
				customPSO = firstProxy->m_Material->m_shaderPSO;
			}
			else
			{
				continue; // 해당 이름의 PSO가 없다면 스킵
			}
		}


		// 머티리얼은 오직 '변경된 CBuffer'만 업로드
		for (auto* proxy : proxies)
		{
			// PSO는 그룹 단위로 1회 Apply
			customPSO->Apply(deferredPtr);
			DirectX11::OMSetBlendState(deferredPtr, m_blendPassState.Get(), blend_factor, sample_mask);
			DirectX11::OMSetDepthStencilState(deferredPtr, m_depthNoWrite.Get(), 1);

			DirectX11::UpdateBuffer(deferredPtr, m_materialBuffer.Get(), &proxy->m_Material);
			if (proxy->m_Material->m_pBaseColor) DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &proxy->m_Material->m_pBaseColor->m_pSRV);
			if (proxy->m_Material->m_pNormal) DirectX11::PSSetShaderResources(deferredPtr, 1, 1, &proxy->m_Material->m_pNormal->m_pSRV);
			if (proxy->m_Material->m_pOccRoughMetal) DirectX11::PSSetShaderResources(deferredPtr, 2, 1, &proxy->m_Material->m_pOccRoughMetal->m_pSRV);
			if (proxy->m_Material->m_AOMap) DirectX11::PSSetShaderResources(deferredPtr, 3, 1, &proxy->m_Material->m_AOMap->m_pSRV);
			if (proxy->m_Material->m_pEmissive) DirectX11::PSSetShaderResources(deferredPtr, 5, 1, &proxy->m_Material->m_pEmissive->m_pSRV);

			proxy->m_Material->TrySetMatrix("PerObject", "model", proxy->m_worldMatrix);
			proxy->m_Material->TrySetMatrix("PerFrame", "view", renderData->m_frameCalculatedView);
			proxy->m_Material->TrySetMatrix("PerApplication", "projection", renderData->m_frameCalculatedProjection);
			proxy->m_Material->TrySetMaterialInfo();


			//Cbuffer를 View 전용 컨테이너로 복사
			proxy->m_Material->UpdateCBufferView();
			// 이 머티리얼이 보관하던 CBuffer 변경분만 GPU로 반영
			proxy->m_Material->ApplyShaderParams(deferredPtr);
			// 텍스처 SRV는 SetShaderPSO() 때 슬롯 고정 바인딩됨

			// 기존 forward binding
			{
				DirectX11::PSSetConstantBuffer(deferredPtr, 12, 1, m_MatrixBuffer.GetAddressOf());
				DirectX11::PSSetShaderResources(deferredPtr, 15, 1, &m_CopiedTexture->m_pSRV);
				DirectX11::PSSetShaderResources(deferredPtr, 16, 1, &m_normalTexture->m_pSRV);
				DirectX11::PSSetConstantBuffer(deferredPtr, 1, 1, &scene.m_LightController->m_pLightBuffer);
				//DirectX11::PSSetConstantBuffer(deferredPtr, 0, 1, m_materialBuffer.GetAddressOf());
				DirectX11::PSSetConstantBuffer(deferredPtr, 3, 1, m_Buffer.GetAddressOf());

				ID3D11ShaderResourceView* envSRVs[3] = {
					m_UseEnvironmentMap ? envMap->m_pSRV : nullptr,
					m_UseEnvironmentMap ? preFilter->m_pSRV : nullptr,
					m_UseEnvironmentMap ? brdfLut->m_pSRV : nullptr
				};
				DirectX11::PSSetShaderResources(deferredPtr, 6, 3, envSRVs);

				auto& lightManager = scene.m_LightController;
				if (lightManager->hasLightWithShadows) {
					DirectX11::PSSetShaderResources(deferredPtr, 4, 1, &renderData->m_shadowMapTexture->m_pSRV);
					DirectX11::PSSetConstantBuffer(deferredPtr, 2, 1, &lightManager->m_shadowMapBuffer);
					lightManager->PSBindCloudShadowMap(deferredPtr);
				}
			}
			proxy->Draw(deferredPtr);
		}

		/*DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &nullSRVs);
		DirectX11::PSSetShaderResources(deferredPtr, 1, 1, &nullSRVs);
		DirectX11::PSSetShaderResources(deferredPtr, 2, 1, &nullSRVs);
		DirectX11::PSSetShaderResources(deferredPtr, 3, 1, &nullSRVs);
		DirectX11::PSSetShaderResources(deferredPtr, 5, 1, &nullSRVs);*/
	}

	if (0 == renderData->m_index)
	{
		RenderDebugManager::GetInstance()->CaptureRenderPass(
			deferredPtr,
			renderData->m_renderTarget->GetRTV(),
			"02:FORWARD_PASS"
		);
	}

	ID3D11DepthStencilState* nullDepthStencilState = nullptr;
	ID3D11BlendState* nullBlendState = nullptr;

	DirectX11::OMSetDepthStencilState(deferredPtr, nullDepthStencilState, 1);
	DirectX11::OMSetBlendState(deferredPtr, nullBlendState, nullptr, 0xFFFFFFFF);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets(deferredPtr);


	DirectX11::PSSetShaderResources(deferredPtr, 15, 1, &nullSRV);
	DirectX11::PSSetShaderResources(deferredPtr, 16, 1, &nullSRV);

	ID3D11CommandList* commandList{};
	deferredPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void ForwardPass::CreateFoliageCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto data = RenderPassData::GetData(&camera);

	if (m_EnvironmentMap.expired() ||
		m_PreFilter.expired() ||
		m_BrdfLut.expired())
	{
		return; // Ensure environment maps are set before proceeding
	}

	auto envMap = m_EnvironmentMap.lock();
	auto preFilter = m_PreFilter.lock();
	auto brdfLut = m_BrdfLut.lock();

	ID3D11DeviceContext* deferredPtr = deferredContext;
	m_instancePSO->Apply(deferredPtr);
	// Bind the pre-created instance buffer SRV to the vertex shader once.
	DirectX11::VSSetShaderResources(deferredPtr, 0, 1, m_instanceBufferSRV.GetAddressOf());

	ForwardBuffer buffer{};
	buffer.m_envMapIntensity = m_envMapIntensity;
	buffer.m_useEnvironmentMap = m_UseEnvironmentMap;
	DirectX11::UpdateBuffer(deferredPtr, m_Buffer.Get(), &buffer);

	ID3D11RenderTargetView* view = data->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(deferredPtr, 1, &view, data->m_depthStencil->m_pDSV);
	DirectX11::RSSetViewports(deferredPtr, 1, &DirectX11::DeviceStates->g_Viewport);
	scene.UseModel(deferredPtr);
	DirectX11::OMSetDepthStencilState(deferredPtr, DirectX11::DeviceStates->g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(deferredPtr, DirectX11::DeviceStates->g_pBlendState, nullptr, 0xFFFFFFFF);
	DirectX11::PSSetConstantBuffer(deferredPtr, 1, 1, &scene.m_LightController->m_pLightBuffer);
	DirectX11::VSSetConstantBuffer(deferredPtr, 3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(deferredPtr, 0, 1, m_materialBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(deferredPtr, 3, 1, m_Buffer.GetAddressOf());

	ID3D11ShaderResourceView* envSRVs[3] = {
		m_UseEnvironmentMap ? envMap->m_pSRV : nullptr,
		m_UseEnvironmentMap ? preFilter->m_pSRV : nullptr,
		m_UseEnvironmentMap ? brdfLut->m_pSRV : nullptr
	};
	DirectX11::PSSetShaderResources(deferredPtr, 6, 3, envSRVs);

	auto& lightManager = scene.m_LightController;
	if (lightManager->hasLightWithShadows) 
	{
		DirectX11::PSSetShaderResources(deferredPtr, 4, 1, &data->m_shadowMapTexture->m_pSRV);
		DirectX11::PSSetConstantBuffer(deferredPtr, 2, 1, &lightManager->m_shadowMapBuffer);
		lightManager->PSBindCloudShadowMap(deferredPtr);
	}

	camera.UpdateBuffer(deferredPtr);

	//std::unordered_map<std::pair<HashedGuid, HashedGuid>, std::vector<FoliageInstance*>> instanceMap;
	for(auto& proxy : data->m_foliageQueue)
	{
		std::unordered_map<uint32, std::vector<FoliageInstance*>> instanceMap;
		for(auto& instance : proxy->m_foliageInstances)
		{
			uint32 key = instance.m_foliageTypeID;
			instanceMap[key].push_back(&instance);
		}

		for(auto& [key, instances] : instanceMap)
		{
			if (instances.empty()) continue;
			auto& firstInstance = instances.front();

			auto& foliageType = proxy->m_foliageTypes[key];
			Mesh* mesh		= foliageType.m_mesh;
			Material* mat	= foliageType.m_material;
			if (!mesh || !mat) continue;

			auto matinfo = mat->m_materialInfo;
			matinfo.m_bitflag |= foliageType.m_isShadowRecive ? MaterialInfomation::USE_SHADOW_RECIVE : 0;
			
			DirectX11::UpdateBuffer(deferredPtr, m_materialBuffer.Get(), &matinfo);
			
			if (mat->m_pBaseColor) DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &mat->m_pBaseColor->m_pSRV);
			if (mat->m_pNormal) DirectX11::PSSetShaderResources(deferredPtr, 1, 1, &mat->m_pNormal->m_pSRV);
			if (mat->m_pOccRoughMetal) DirectX11::PSSetShaderResources(deferredPtr, 2, 1, &mat->m_pOccRoughMetal->m_pSRV);
			if (mat->m_AOMap) DirectX11::PSSetShaderResources(deferredPtr, 3, 1, &mat->m_AOMap->m_pSRV);
			if (mat->m_pEmissive) DirectX11::PSSetShaderResources(deferredPtr, 5, 1, &mat->m_pEmissive->m_pSRV);
			
			std::vector<Mathf::xMatrix> instanceMatrices;
			instanceMatrices.reserve(instances.size());
			for (const auto& instance : instances)
			{
				instanceMatrices.push_back(instance->m_worldMatrix * proxy->m_worldMatrix);
			}

			// Define the destination box to specify the exact region to update.
			D3D11_BOX destBox;
			destBox.left = 0;
			destBox.right = instances.size() * sizeof(Mathf::xMatrix);
			destBox.top = 0;
			destBox.bottom = 1;
			destBox.front = 0;
			destBox.back = 1;

			deferredPtr->UpdateSubresource(m_instanceBuffer.Get(), 0, &destBox, instanceMatrices.data(), 0, 0);

			mesh->DrawInstanced(deferredPtr, instances.size());
		}
	}

	ID3D11DepthStencilState* nullDepthStencilState = nullptr;
	ID3D11BlendState* nullBlendState = nullptr;

	DirectX11::OMSetDepthStencilState(deferredPtr, nullDepthStencilState, 1);
	DirectX11::OMSetBlendState(deferredPtr, nullBlendState, nullptr, 0xFFFFFFFF);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets(deferredPtr);

	ID3D11CommandList* commandList{};
	deferredPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void ForwardPass::ControlPanel()
{
	ImGui::DragFloat("ior", &ior, 0.075f, -1.f, 1.f);
	ImGui::DragFloat("add", &add, 0.075f, 0.f, 1000.f);
}
