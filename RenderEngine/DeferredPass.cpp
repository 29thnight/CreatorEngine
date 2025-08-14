#include "DeferredPass.h"
#include "Scene.h"
#include "../EngineEntry/RenderPassSettings.h"
#include "LightController.h"
#include "ShaderSystem.h"
#include "ImGuiRegister.h"
#include "RenderDebugManager.h"

struct alignas(16) DeferredBuffer
{
    Mathf::xMatrix m_InverseProjection;
    Mathf::xMatrix m_InverseView;
    bool32 m_useAmbientOcclusion{};
    bool32 m_useEnvironmentMap{};
	float m_envMapIntensity{ 1.f };
};

ID3D11ShaderResourceView* nullSRV10[10] = {
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

ID3D11RenderTargetView* nullRTVs[2] = { nullptr, nullptr };

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

void DeferredPass::Initialize(
    Managed::SharedPtr<Texture> diffuse, 
    Managed::SharedPtr<Texture> metalRough, 
    Managed::SharedPtr<Texture> normals, 
    Managed::SharedPtr<Texture> emissive, 
    Managed::SharedPtr<Texture> bitmask)
{
    m_DiffuseTexture    = diffuse;
    m_MetalRoughTexture = metalRough;
    m_NormalTexture     = normals;
    m_EmissiveTexture   = emissive;
    m_BitmaskTexture    = bitmask; 
}

void DeferredPass::UseAmbientOcclusion(Managed::SharedPtr<Texture> aoMap)
{
    m_AmbientOcclusionTexture = aoMap;
    //m_UseAmbientOcclusion = true;
}

void DeferredPass::UseEnvironmentMap(
    Managed::SharedPtr<Texture> envMap, 
    Managed::SharedPtr<Texture> preFilter, 
    Managed::SharedPtr<Texture> brdfLut)
{
    m_EnvironmentMap    = envMap;
    m_PreFilter         = preFilter;
    m_BrdfLut           = brdfLut;
    //m_UseEnvironmentMap = true;
}

void DeferredPass::DisableAmbientOcclusion()
{
    m_AmbientOcclusionTexture.reset();
    m_UseAmbientOcclusion = false;
}

void DeferredPass::Execute(RenderScene& scene, Camera& camera)
{
    ExecuteCommandList(scene, camera);
}

void DeferredPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    if (m_DiffuseTexture.expired()          || 
        m_MetalRoughTexture.expired()       || 
        m_NormalTexture.expired()           || 
        m_EmissiveTexture.expired()         || 
        m_AmbientOcclusionTexture.expired() ||
        m_EnvironmentMap.expired()          || 
        m_PreFilter.expired()               || 
        m_BrdfLut.expired())
    {
        return; // If any texture is expired, skip execution
	}

	auto diffuseTexture = m_DiffuseTexture.lock();
	auto metalRoughTexture = m_MetalRoughTexture.lock();
	auto normalTexture = m_NormalTexture.lock();
	auto emissiveTexture = m_EmissiveTexture.lock();
	auto bitmaskTexture = m_BitmaskTexture.lock();
	auto ambientOcclusionTexture = m_AmbientOcclusionTexture.lock();
	auto environmentMap = m_EnvironmentMap.lock();
	auto preFilter = m_PreFilter.lock();
	auto brdfLut = m_BrdfLut.lock();
	auto LightEmissiveTexture = m_LightEmissiveTexture.lock();

    ID3D11DeviceContext* deferredPtr = deferredContext;

    auto& lightManager = scene.m_LightController;

    cameraView cameraview{};
    cameraview.cameraView = camera.CalculateView();

    DeferredBuffer buffer{};
    buffer.m_InverseProjection = XMMatrixInverse(nullptr, camera.CalculateProjection());
    buffer.m_InverseView = XMMatrixInverse(nullptr, cameraview.cameraView);
    buffer.m_useAmbientOcclusion = m_UseAmbientOcclusion;
    buffer.m_useEnvironmentMap = m_UseEnvironmentMap;
    buffer.m_envMapIntensity = m_envMapIntensity;

    bool isShadowMapRender = lightManager->hasLightWithShadows && m_UseLightWithShadows;

    ID3D11ShaderResourceView* srvs[10] = 
    {
        renderData->m_depthStencil->m_pSRV,
        diffuseTexture->m_pSRV,
        metalRoughTexture->m_pSRV,
        normalTexture->m_pSRV,
        (isShadowMapRender) ? renderData->m_shadowMapTexture->m_pSRV : nullptr,
        m_UseAmbientOcclusion ? ambientOcclusionTexture->m_pSRV : nullptr,
        m_UseEnvironmentMap ? environmentMap->m_pSRV : nullptr,
        m_UseEnvironmentMap ? preFilter->m_pSRV : nullptr,
        m_UseEnvironmentMap ? brdfLut->m_pSRV : nullptr,
        emissiveTexture->m_pSRV
    };

    m_pso->Apply(deferredPtr);
    ID3D11RenderTargetView* emissiveRtv = LightEmissiveTexture->GetRTV();

    ID3D11RenderTargetView* rtv[2] = { renderData->m_renderTarget->GetRTV(), LightEmissiveTexture->GetRTV() };
    DirectX11::OMSetRenderTargets(deferredPtr, 2, rtv, nullptr);
    DirectX11::RSSetViewports(deferredPtr, 1, &DeviceState::g_Viewport);

    camera.UpdateBuffer(deferredPtr);
    DirectX11::UpdateBuffer(deferredPtr, m_Buffer.Get(), &buffer);
    DirectX11::UpdateBuffer(deferredPtr, m_shadowcamBuffer.Get(), &cameraview);
    DirectX11::UpdateBuffer(deferredPtr, lightManager->m_shadowMapBuffer, &renderData->m_shadowCamera.m_shadowMapConstant);

    DirectX11::PSSetConstantBuffer(deferredPtr, 1, 1, &lightManager->m_pLightBuffer);
    DirectX11::PSSetConstantBuffer(deferredPtr, 11, 1, &lightManager->m_pLightCountBuffer);
    DirectX11::PSSetConstantBuffer(deferredPtr, 3, 1, m_Buffer.GetAddressOf());
    DirectX11::PSSetConstantBuffer(deferredPtr, 10, 1, m_shadowcamBuffer.GetAddressOf());
    DirectX11::PSSetShaderResources(deferredPtr, 0, 10, srvs);
	DirectX11::PSSetShaderResources(deferredPtr, 11, 1, &bitmaskTexture->m_pSRV);

    if (lightManager->hasLightWithShadows)
    {
        DirectX11::PSSetConstantBuffer(deferredPtr, 2, 1, &lightManager->m_shadowMapBuffer);
        lightManager->PSBindCloudShadowMap(deferredPtr);
    }
    DirectX11::Draw(deferredPtr, 4, 0);

    if (0 == renderData->m_index)
    {
        RenderDebugManager::GetInstance()->CaptureRenderPass(
            deferredPtr, 
            renderData->m_renderTarget->GetRTV(),
            "01:DEFERRED_PASS"
		);
    }

    DirectX11::PSSetShaderResources(deferredPtr, 0, 10, nullSRV10);
    DirectX11::OMSetRenderTargets(deferredPtr, 1, nullRTVs, nullptr);

    ID3D11CommandList* commandList{};
    deferredPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void DeferredPass::ControlPanel()
{
    ImGui::PushID(this);
    auto& setting = EngineSettingInstance->GetRenderPassSettingsRW().deferred;

    if (ImGui::Checkbox("Use Ambient Occlusion", &m_UseAmbientOcclusion))
    {
        setting.useAmbientOcclusion = m_UseAmbientOcclusion;
    }
    if (ImGui::Checkbox("Use Light With Shadows", &m_UseLightWithShadows))
    {
        setting.useLightWithShadows = m_UseLightWithShadows;
    }
    if (ImGui::Checkbox("Use Environment Map", &m_UseEnvironmentMap))
    {
        setting.useEnvironmentMap = m_UseEnvironmentMap;
    }
    if (ImGui::SliderFloat("EnvMap Intensity", &m_envMapIntensity, 0.f, 10.f))
    {
        setting.envMapIntensity = m_envMapIntensity;
    }

    if (ImGui::Button("Reset")) 
    {
        m_UseAmbientOcclusion = true;
        m_UseEnvironmentMap = true;
        m_UseLightWithShadows = true;
        m_envMapIntensity = 1.f;

        setting.useAmbientOcclusion = m_UseAmbientOcclusion;
        setting.useEnvironmentMap = m_UseEnvironmentMap;
        setting.useLightWithShadows = m_UseLightWithShadows;
        setting.envMapIntensity = m_envMapIntensity;
    }
    ImGui::PopID();
}

void DeferredPass::UseLightAndEmissiveRTV(Managed::SharedPtr<Texture> lightEmissive)
{
    m_LightEmissiveTexture = lightEmissive;
}

void DeferredPass::ApplySettings(const DeferredPassSetting& setting)
{
    m_UseAmbientOcclusion = setting.useAmbientOcclusion;
    m_UseEnvironmentMap = setting.useEnvironmentMap;
    m_UseLightWithShadows = setting.useLightWithShadows;
    m_envMapIntensity = setting.envMapIntensity;
}
