#include "VignettePass.h"
#include "ShaderSystem.h"
#include "../EngineEntry/RenderPassSettings.h"

struct alignas(16) CBData {
	float radius;
	float softness;
};

VignettePass::VignettePass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Vignette"];
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

VignettePass::~VignettePass()
{
}

void VignettePass::Execute(RenderScene& scene, Camera& camera)
{
	if (!isOn) return;
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	m_pso->Apply();
	ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &view, nullptr);

	camera.UpdateBuffer();
	CBData cbData;
	cbData.radius = radius;
	cbData.softness = softness;

	DirectX11::UpdateBuffer(m_Buffer.Get(), &cbData);
	DirectX11::PSSetConstantBuffer(0, 1, m_Buffer.GetAddressOf());

	DirectX11::CopyResource(m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

	DirectX11::PSSetShaderResources(0, 1, &m_CopiedTexture->m_pSRV);

	DirectX11::Draw(4, 0);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
	//DirectX11::UnbindRenderTargets();
}

void VignettePass::ControlPanel()
{
        ImGui::PushID(this);
        auto& setting = EngineSettingInstance->GetRenderPassSettings().vignette;
        if (ImGui::Checkbox("Vignette", &isOn))
        {
                setting.isOn = isOn;
        }
        if (ImGui::SliderFloat("Radius", &radius, 0.0f, 1.0f))
        {
                setting.radius = radius;
        }
        if (ImGui::SliderFloat("Softness", &softness, 0.0f, 1.0f))
        {
                setting.softness = softness;
        }

        if (ImGui::Button("Reset")) {
                radius = 0.75f;
                softness = 0.5f;

                setting.radius = radius;
                setting.softness = softness;
        }

	ImGui::PopID();
}

void VignettePass::ApplySettings(const VignettePassSetting& setting)
{
    isOn = setting.isOn;
    radius = setting.radius;
    softness = setting.softness;
}
