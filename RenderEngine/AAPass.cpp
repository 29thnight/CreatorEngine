#include "AAPass.h"
#include "ShaderSystem.h"
#include "../EngineEntry/RenderPassSettings.h"
#include "Material.h"
#include "Skeleton.h"
#include "Scene.h"
#include "Mesh.h"
#include "ImGuiRegister.h"

AAPass::AAPass()
{
	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_computeShader = &ShaderSystem->ComputeShaders["FXAA"];

	m_FXAAParameters.TextureSize.x = (int)DeviceState::g_ClientRect.width;
	m_FXAAParameters.TextureSize.y = (int)DeviceState::g_ClientRect.height;

	m_FXAAParametersBuffer = DirectX11::CreateBuffer(sizeof(FXAAParametersBuffer), D3D11_BIND_CONSTANT_BUFFER, &m_FXAAParameters);

	m_AntiAliasingTexture = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"AntiAliasing",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
	);
	m_AntiAliasingTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_CopiedTexture = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE
	);
	m_CopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
}

AAPass::~AAPass()
{
}

void AAPass::Execute(RenderScene& scene, Camera& camera)
{
	if (!m_isApply) return;
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	const UINT offsets[]{ 0 };
	m_pso->Apply();

	DirectX11::UnbindRenderTargets();

	DirectX11::CopyResource(m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

	DirectX11::UpdateBuffer(m_FXAAParametersBuffer.Get(), &m_FXAAParameters);

	ID3D11UnorderedAccessView* uavs[] = { m_AntiAliasingTexture->m_pUAV };
	ID3D11UnorderedAccessView* nullUAVs[]{ nullptr };
	DirectX11::CSSetUnorderedAccessViews(0, 1, uavs, offsets);

	ID3D11ShaderResourceView* srvs[]{ m_CopiedTexture->m_pSRV };
	ID3D11ShaderResourceView* nullSRVs[]{ nullptr };

	DirectX11::CSSetShaderResources(0, 1, srvs);

	DirectX11::CSSetConstantBuffer(0, 1, m_FXAAParametersBuffer.GetAddressOf());

	uint32 threadGroupCountX = (uint32)ceilf(DeviceState::g_ClientRect.width / 16.0f);
	uint32 threadGroupCountY = (uint32)ceilf(DeviceState::g_ClientRect.height / 16.0f);

	DirectX11::Dispatch(threadGroupCountX, threadGroupCountY, 1);

	DirectX11::CSSetUnorderedAccessViews(0, 1, nullUAVs, offsets);
	DirectX11::CSSetShaderResources(0, 1, nullSRVs);

	DirectX11::CopyResource(renderData->m_renderTarget->m_pTexture, m_AntiAliasingTexture->m_pTexture);
}

void AAPass::ControlPanel()
{
	ImGui::PushID(this);
	ImGui::Checkbox("Apply AntiAliasing", &m_isApply);
	if (m_isApply)
	{
		ImGui::SliderFloat("Bias", &m_FXAAParameters.Bias, 0.0f, 50.0f);
		ImGui::SliderFloat("BiasMin", &m_FXAAParameters.BiasMin, 0.0f, 50.0f);
		ImGui::SliderFloat("SpanMax", &m_FXAAParameters.SpanMax, 0.0f, 100.0f);
	}
	if (ImGui::Button("Reset")) {
		m_FXAAParameters.Bias = 0.688f;
		m_FXAAParameters.BiasMin = 0.021f;
		m_FXAAParameters.SpanMax = 8.0f;
	}
	ImGui::PopID();
}

void AAPass::ApplySettings(const AAPassSetting& setting)
{
    m_isApply = setting.isApply;
    m_FXAAParameters.Bias = setting.bias;
    m_FXAAParameters.BiasMin = setting.biasMin;
    m_FXAAParameters.SpanMax = setting.spanMax;
}
