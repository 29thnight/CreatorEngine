#include "ScreenSpaceReflectionPass.h"
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

void ScreenSpaceReflectionPass::Initialize(Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive)
{
	m_DiffuseTexture = diffuse;
	m_MetalRoughTexture = metalRough;
	m_NormalTexture = normals;
	m_EmissiveTexture = emissive;

	m_CopiedTexture = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_CopiedTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_CopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_prevCopiedSSRTexture = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
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

void ScreenSpaceReflectionPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	if (!isOn) return;

	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	ID3D11DeviceContext* deferredPtr = deferredContext;

	CBData cbData;
	cbData.m_InverseProjection = camera.CalculateInverseProjection();
	cbData.m_InverseView = camera.CalculateInverseView();
	cbData.m_viewProjection = camera.CalculateView() * camera.CalculateProjection();
	cbData.m_cameraPosition = camera.m_eyePosition;
	cbData.stepSize = stepSize;
	cbData.MaxThickness = MaxThickness;
	cbData.Time = (float)Time->GetTotalSeconds();
	cbData.maxRayCount = maxRayCount;

	m_pso->Apply(deferredPtr);

	DirectX11::CopyResource(deferredPtr, m_prevCopiedSSRTexture->m_pTexture, renderData->m_SSRPrevTexture->m_pTexture);

	ID3D11RenderTargetView* view[2] = { renderData->m_renderTarget->GetRTV(), renderData->m_SSRPrevTexture->GetRTV() };
	DirectX11::OMSetRenderTargets(deferredPtr, 2, view, nullptr);
	DirectX11::RSSetViewports(deferredPtr, 1, &DeviceState::g_Viewport);
	DirectX11::PSSetConstantBuffer(deferredPtr, 0, 1, m_Buffer.GetAddressOf());

	camera.UpdateBuffer(deferredPtr);
	DirectX11::UpdateBuffer(deferredPtr, m_Buffer.Get(), &cbData);

	DirectX11::CopyResource(deferredPtr, m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

	ID3D11ShaderResourceView* srvs[5] = {
		renderData->m_depthStencil->m_pSRV,
		m_CopiedTexture->m_pSRV,//m_DiffuseTexture->m_pSRV,
		m_MetalRoughTexture->m_pSRV,
		m_NormalTexture->m_pSRV,
		m_prevCopiedSSRTexture->m_pSRV
	};
	DirectX11::PSSetShaderResources(deferredPtr, 0, 5, srvs);
	DirectX11::Draw(deferredPtr, 4, 0);
	DirectX11::PSSetShaderResources(deferredPtr, 0, 4, nullSRV);

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
