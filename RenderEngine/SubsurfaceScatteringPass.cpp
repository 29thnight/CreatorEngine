#include "SubsurfaceScatteringPass.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "RenderScene.h"
#include "LightController.h"
#include "TimeSystem.h"

struct alignas(16) SubsurfaceScatteringBuffer
{
	float2 direction;
	float strength;
	float width;
	float CameraFOV;
};

SubsurfaceScatteringPass::SubsurfaceScatteringPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["SSS"];
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
	m_Buffer = DirectX11::CreateBuffer(sizeof(SubsurfaceScatteringBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

SubsurfaceScatteringPass::~SubsurfaceScatteringPass()
{
}

void SubsurfaceScatteringPass::Initialize(Texture* diffuse, Texture* metalRough)
{
	m_DiffuseTexture = diffuse;
	m_MetalRoughTexture = metalRough;

	m_CopiedTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_CopiedTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_CopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void SubsurfaceScatteringPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void SubsurfaceScatteringPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	if (!isOn) return;
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

	// RHI 이식(PHASE 3-1, 4차 슬라이스). 가로/세로 두 번의 블러가 각각
	// "결과 복사 → 다시 입력" 사이클이라 CopyResource가 두 번 나온다.

	m_pso->Apply(deferredPtr);
	RHINativeRenderTarget view = renderData->m_renderTarget->GetRTV();
	context.SetRenderTargets(1, &view, nullptr);

	const D3D11_VIEWPORT& vp = DirectX11::DeviceStates->g_Viewport;
	const RHIViewport viewport{ vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, vp.MinDepth, vp.MaxDepth };
	context.SetViewports(1, &viewport);

	RHINativeBuffer constantBuffer = m_Buffer.Get();
	context.SetPixelShaderConstantBuffers(0, 1, &constantBuffer);

	camera.DeferredUpdateBuffer(deferredPtr, renderData->m_frameCalculatedView, renderData->m_frameCalculatedProjection);

	SubsurfaceScatteringBuffer buffer{};
	buffer.CameraFOV	= camera.m_fov;
	buffer.strength		= strength;
	buffer.width		= width;
	buffer.direction	= { 1.f, 0.f };//direction;

	context.UpdateBuffer(m_Buffer.Get(), &buffer);

	context.CopyResource(m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

	RHINativeShaderResource srvs[3] = {
		renderData->m_depthStencil->m_pSRV,
		m_CopiedTexture->m_pSRV,
		m_MetalRoughTexture->m_pSRV,
	};
	context.SetPixelShaderResources(0, 3, srvs);

	context.Draw(4, 0);

	buffer.direction = { 0.f, 1.f };
	context.UpdateBuffer(m_Buffer.Get(), &buffer);

	context.CopyResource(m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

	context.SetPixelShaderResources(0, 3, srvs);

	context.Draw(4, 0);

	RHINativeShaderResource nullSRV[3] = { nullptr, nullptr, nullptr };
	context.SetPixelShaderResources(0, 3, nullSRV);
	context.UnbindRenderTargets();

	ID3D11CommandList* commandList{};
	deferredPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void SubsurfaceScatteringPass::ControlPanel()
{
	ImGui::PushID(this);
	ImGui::Checkbox("Enable Subsurface Scattering", &isOn);
	ImGui::SliderFloat2("Direction", &direction.x, -1.f, 1.f);
	ImGui::SliderFloat("Strength", &strength, 0.f, 1.f);
	ImGui::SliderFloat("Width", &width, 0.f, 1.f);
	ImGui::PopID();
}