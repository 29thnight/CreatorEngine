#include "ScreenSpaceReflectionPass.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "RenderScene.h"
#include "LightController.h"
#include "TimeSystem.h"

struct alignas(16) CBData
{
	Mathf::Matrix m_InverseProjection;
	Mathf::Matrix m_InverseView;
	Mathf::Matrix m_viewProjection;
	Mathf::Vector4 m_cameraPosition;
	float stepSize;
	float MaxThickness;
	float Time;
	int maxRayCount;

	Mathf::Vector2 screenSize;
};

ID3D11ShaderResourceView* nullSRV[4] = {
	nullptr,
	nullptr,
	nullptr,
	nullptr
};

ScreenSpaceReflectionPass::ScreenSpaceReflectionPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["SSR"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

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

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);
	m_Buffer = DirectX11::CreateBuffer(sizeof(CBData), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

ScreenSpaceReflectionPass::~ScreenSpaceReflectionPass()
{
}

void ScreenSpaceReflectionPass::Initialize(Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive, Texture* bitflag)
{
	m_DiffuseTexture = diffuse;
	m_MetalRoughTexture = metalRough;
	m_NormalTexture = normals;
	m_EmissiveTexture = emissive;
	m_BitflagTexture = bitflag;

	m_CopiedTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_CopiedTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_CopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_prevCopiedSSRTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"PreviousCopiedSSRTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_prevCopiedSSRTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_prevCopiedSSRTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void ScreenSpaceReflectionPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void ScreenSpaceReflectionPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	if (!isOn) return;

	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

	CBData cbData;
	cbData.m_InverseProjection = renderData->m_frameCalculatedInverseProjection;
	cbData.m_InverseView = renderData->m_frameCalculatedInverseView;
	cbData.m_viewProjection = renderData->m_frameCalculatedView * renderData->m_frameCalculatedProjection;
	cbData.m_cameraPosition = renderData->m_frameEyePosition;
	cbData.stepSize = stepSize;
	cbData.MaxThickness = MaxThickness;
	cbData.Time = (float)Time->GetTotalSeconds();
	cbData.maxRayCount = maxRayCount;

	// RHI 이식(PHASE 3-1, 4차 슬라이스). 히스토리 텍스처 복사 2건이 CopyResource
	// 표면의 첫 사용처다 — 렌더그래프(3-5)에서 복사 노드가 될 지점.

	m_pso->Apply(deferredPtr);

	context.CopyResource(m_prevCopiedSSRTexture->m_pTexture, renderData->m_SSRPrevTexture->m_pTexture);

	RHINativeRenderTarget view[2] = { renderData->m_renderTarget->GetRTV(), renderData->m_SSRPrevTexture->GetRTV() };
	context.SetRenderTargets(2, view, nullptr);

	const RHIViewport viewport = RHI::Device().GetFullViewport();
	context.SetViewports(1, &viewport);

	RHINativeBuffer constantBuffer = m_Buffer.Get();
	context.SetPixelShaderConstantBuffers(0, 1, &constantBuffer);

	renderData->BindFrameCameraBuffers(context);
	context.UpdateBuffer(m_Buffer.Get(), &cbData);

	context.CopyResource(m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

	RHINativeShaderResource srvs[5] = {
		renderData->m_depthStencil->m_pSRV,
		m_CopiedTexture->m_pSRV,//m_DiffuseTexture->m_pSRV,
		m_MetalRoughTexture->m_pSRV,
		m_NormalTexture->m_pSRV,
		m_prevCopiedSSRTexture->m_pSRV
	};
	context.SetPixelShaderResources(0, 5, srvs);
	context.Draw(4, 0);

	RHINativeShaderResource nullSrvs[4] = { nullptr, nullptr, nullptr, nullptr };
	context.SetPixelShaderResources(0, 4, nullSrvs);

	ID3D11CommandList* commandList{};
	deferredPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void ScreenSpaceReflectionPass::ControlPanel()
{
	ImGui::PushID(this);
	ImGui::Text("Screen Space Reflection");
	ImGui::Checkbox("Enable SSR", &isOn);
	ImGui::SliderFloat("Step Size", &stepSize, 0.0f, 1.0f);
	ImGui::SliderFloat("Max Thickness", &MaxThickness, 0.0f, 0.02f, "%.5f");
	ImGui::SliderInt("Max Ray Count", &maxRayCount, 1, 100);
	ImGui::Text("Time: %f", Time);

	if (ImGui::Button("Reset")) {
		stepSize = 0.114f;
		MaxThickness = 0.00416f;
		maxRayCount = 20;
	}
	ImGui::PopID();
}
