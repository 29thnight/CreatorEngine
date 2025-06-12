#include "DeferredPass.h"
#include "Scene.h"
#include "LightController.h"
#include "ShaderSystem.h"
#include "ImGuiRegister.h"

struct alignas(16) DeferredBuffer
{
    Mathf::xMatrix m_InverseProjection;
    Mathf::xMatrix m_InverseView;
    bool32 m_useAmbientOcclusion{};
    bool32 m_useEnvironmentMap{};
	float m_envMapIntensity{ 1.f };
};

ID3D11ShaderResourceView* nullSRV[10] = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};

DeferredPass::DeferredPass()
{
    m_pso = std::make_unique<PipelineStateObject>();
    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Deferred"];
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

    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(pointSampler);

    m_Buffer = DirectX11::CreateBuffer(sizeof(DeferredBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    m_shadowcamBuffer = DirectX11::CreateBuffer(sizeof(cameraView), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

DeferredPass::~DeferredPass()
{
}

void DeferredPass::Initialize(Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive)
{
    m_DiffuseTexture = diffuse;
    m_MetalRoughTexture = metalRough;
    m_NormalTexture = normals;
    m_EmissiveTexture = emissive;
}

void DeferredPass::UseAmbientOcclusion(Texture* aoMap)
{
    m_AmbientOcclusionTexture = aoMap;
    //m_UseAmbientOcclusion = true;
}

void DeferredPass::UseEnvironmentMap(Texture* envMap, Texture* preFilter, Texture* brdfLut)
{
    m_EnvironmentMap = envMap;
    m_PreFilter = preFilter;
    m_BrdfLut = brdfLut;
    //m_UseEnvironmentMap = true;
}

void DeferredPass::DisableAmbientOcclusion()
{
    m_AmbientOcclusionTexture = nullptr;
    m_UseAmbientOcclusion = false;
}

void DeferredPass::Execute(RenderScene& scene, Camera& camera)
{
    m_pso->Apply();
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    auto& lightManager = scene.m_LightController;

    cameraView cameraview{};
    cameraview.cameraView = camera.CalculateView();

    DeferredBuffer buffer{};
    buffer.m_InverseProjection      = XMMatrixInverse(nullptr, camera.CalculateProjection());
    buffer.m_InverseView            = XMMatrixInverse(nullptr, cameraview.cameraView);
    buffer.m_useAmbientOcclusion    = m_UseAmbientOcclusion;
    buffer.m_useEnvironmentMap      = m_UseEnvironmentMap;
	buffer.m_envMapIntensity        = m_envMapIntensity;
        
    bool isShadowMapRender = lightManager->hasLightWithShadows && m_UseLightWithShadows;

    ID3D11ShaderResourceView* srvs[10] = {
        renderData->m_depthStencil->m_pSRV,
        m_DiffuseTexture->m_pSRV,
        m_MetalRoughTexture->m_pSRV,
        m_NormalTexture->m_pSRV,
        (isShadowMapRender)     ? renderData->m_shadowMapTexture->m_pSRV : nullptr,
        m_UseAmbientOcclusion   ? m_AmbientOcclusionTexture->m_pSRV      : nullptr,
        m_UseEnvironmentMap     ? m_EnvironmentMap->m_pSRV               : nullptr,
        m_UseEnvironmentMap     ? m_PreFilter->m_pSRV                    : nullptr,
        m_UseEnvironmentMap     ? m_BrdfLut->m_pSRV                      : nullptr,
        m_EmissiveTexture->m_pSRV
    };

    auto rtv = renderData->m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(1, &rtv, nullptr);
    DirectX11::PSSetConstantBuffer(1, 1, &lightManager->m_pLightBuffer);
    DirectX11::PSSetConstantBuffer(11, 1, &lightManager->m_pLightCountBuffer);
    DirectX11::PSSetConstantBuffer(3, 1, m_Buffer.GetAddressOf());
    DirectX11::PSSetConstantBuffer(10, 1, m_shadowcamBuffer.GetAddressOf());
    DirectX11::PSSetShaderResources(0, 10, srvs);

    if (lightManager->hasLightWithShadows)
    {
        DirectX11::PSSetConstantBuffer(2, 1, &lightManager->m_shadowMapBuffer);
    }

    DirectX11::UpdateBuffer(m_Buffer.Get(), &buffer);
    DirectX11::UpdateBuffer(m_shadowcamBuffer.Get(), &cameraview);
    DirectX11::UpdateBuffer(lightManager->m_shadowMapBuffer, &camera.m_shadowMapConstant);
    DirectX11::Draw(4, 0);

    DirectX11::PSSetShaderResources(0, 10, nullSRV);
    //DirectX11::UnbindRenderTargets();
    ID3D11RenderTargetView* nullview[2] = { nullptr, nullptr };
    DirectX11::OMSetRenderTargets(2, nullview, nullptr);
}

void DeferredPass::ControlPanel()
{
	ImGui::Checkbox("Use Ambient Occlusion", &m_UseAmbientOcclusion);
	ImGui::Checkbox("Use Light With Shadows", &m_UseLightWithShadows);
	ImGui::Checkbox("Use Environment Map", &m_UseEnvironmentMap);
	ImGui::SliderFloat("EnvMap Intensity", &m_envMapIntensity, 0.f, 10.f);
}

void DeferredPass::UseLightAndEmissiveRTV(Texture* lightEmissive)
{
    m_LightEmissiveTexture = lightEmissive;
}
