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
}

void ScreenSpaceReflectionPass::Execute(RenderScene& scene, Camera& camera)
{
	if (!isOn) return;
	m_pso->Apply();
	ID3D11RenderTargetView* view = camera.m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &view, nullptr);

	camera.UpdateBuffer();
	CBData cbData;
	cbData.m_InverseProjection = camera.CalculateInverseProjection();
	cbData.m_InverseView = camera.CalculateInverseView();
	cbData.m_viewProjection = camera.CalculateView() * camera.CalculateProjection();
	cbData.m_cameraPosition = camera.m_eyePosition;
	cbData.stepSize = stepSize;
	cbData.MaxThickness = MaxThickness;
	cbData.Time = (float)Time->GetTotalSeconds();
	cbData.maxRayCount = maxRayCount;

	DirectX11::UpdateBuffer(m_Buffer.Get(), &cbData);
	DirectX11::PSSetConstantBuffer(0, 1, m_Buffer.GetAddressOf());

	DirectX11::CopyResource(m_CopiedTexture->m_pTexture, camera.m_renderTarget->m_pTexture);

	ID3D11ShaderResourceView* srvs[4] = {
		camera.m_depthStencil->m_pSRV,
		m_CopiedTexture->m_pSRV,//m_DiffuseTexture->m_pSRV,
		m_MetalRoughTexture->m_pSRV,
		m_NormalTexture->m_pSRV
	};
	DirectX11::PSSetShaderResources(0, 4, srvs);

	DirectX11::Draw(4, 0);

	ID3D11ShaderResourceView* nullSRV[4] = {
		nullptr,
		nullptr,
		nullptr,
		nullptr
	};
	DirectX11::PSSetShaderResources(0, 4, nullSRV);
	//DirectX11::UnbindRenderTargets();
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
	ImGui::PopID();
}
