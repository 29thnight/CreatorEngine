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

struct alignas(16) ForwardBuffer
{
	bool32 m_useEnvironmentMap{};
	float m_envMapIntensity{ 1.f };
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

	D3D11_BUFFER_DESC instanceBufferDesc{};
	instanceBufferDesc.ByteWidth = sizeof(Mathf::xMatrix) * MAX_INSTANCES;
	instanceBufferDesc.Usage = D3D11_USAGE_DEFAULT; // Changed for UpdateSubresource
	instanceBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	instanceBufferDesc.CPUAccessFlags = 0; // No direct CPU access needed
	instanceBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	instanceBufferDesc.StructureByteStride = sizeof(Mathf::xMatrix);

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateBuffer(&instanceBufferDesc, nullptr, &m_instanceBuffer)
	);

	// Create a shader resource view for the instance buffer
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = MAX_INSTANCES;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateShaderResourceView(m_instanceBuffer.Get(), &srvDesc, &m_instanceBufferSRV)
	);
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

	using InstanceGroupKey = PrimitiveRenderProxy::ProxyFilter;
	std::vector<PrimitiveRenderProxy*> animatedProxies;
	std::map<InstanceGroupKey, std::vector<PrimitiveRenderProxy*>> instanceGroups;

	for (auto& proxy : renderData->m_forwardQueue)
	{
		if (proxy->m_isAnimationEnabled && HashedGuid::INVAILD_ID != proxy->m_animatorGuid)
		{
			animatedProxies.push_back(proxy);
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
	DirectX11::RSSetViewports(deferredPtr, 1, &DeviceState::g_Viewport);
	scene.UseModel(deferredPtr);
	DirectX11::OMSetDepthStencilState(deferredPtr, DeviceState::g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(deferredPtr, DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);
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
	DirectX11::RSSetViewports(deferredPtr, 1, &DeviceState::g_Viewport);
	scene.UseModel(deferredPtr);
	DirectX11::OMSetDepthStencilState(deferredPtr, DeviceState::g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(deferredPtr, DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);
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
}
