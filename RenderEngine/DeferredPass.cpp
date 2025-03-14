#include "DeferredPass.h"
#include "Scene.h"
#include "Light.h"
#include "AssetSystem.h"

struct alignas(16) DeferredBuffer
{
    Mathf::xMatrix m_InverseProjection;
    Mathf::xMatrix m_InverseView;
    bool32 m_useAmbientOcclusion{};
    bool32 m_useEnvironmentMap{};
};

DeferredPass::DeferredPass()
{
    m_pso = std::make_unique<PipelineStateObject>();
    m_pso->m_vertexShader = &AssetsSystems->VertexShaders["Fullscreen"];
    m_pso->m_pixelShader = &AssetsSystems->PixelShaders["Deferred"];
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

    D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateInputLayout(
            vertexLayoutDesc,
            _countof(vertexLayoutDesc),
            m_pso->m_vertexShader->GetBufferPointer(),
            m_pso->m_vertexShader->GetBufferSize(),
            &m_pso->m_inputLayout
        )
    );

    m_pso->m_samplers.emplace_back(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    m_pso->m_samplers.emplace_back(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_Buffer = DirectX11::CreateBuffer(sizeof(DeferredBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

DeferredPass::~DeferredPass()
{
}

void DeferredPass::Initialize(Texture* renderTarget, Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive)
{
    m_RenderTarget = renderTarget;
    m_DiffuseTexture = diffuse;
    m_MetalRoughTexture = metalRough;
    m_NormalTexture = normals;
    m_EmissiveTexture = emissive;
}

void DeferredPass::UseAmbientOcclusion(Texture* aoMap)
{
    m_AmbientOcclusionTexture = aoMap;
    m_UseAmbientOcclusion = true;
}

void DeferredPass::UseEnvironmentMap(Texture* envMap, Texture* preFilter, Texture* brdfLut)
{
    m_EnvironmentMap = envMap;
    m_PreFilter = preFilter;
    m_BrdfLut = brdfLut;
    m_UseEnvironmentMap = true;
}

void DeferredPass::DisableAmbientOcclusion()
{
    m_AmbientOcclusionTexture = nullptr;
    m_UseAmbientOcclusion = false;
}

void DeferredPass::Execute(Scene& scene)
{
    m_pso->Apply();

    DirectX11::ClearRenderTargetView(m_RenderTarget->GetRTV(), Colors::Transparent);
    ID3D11RenderTargetView* view = m_RenderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(1, &view, nullptr);

    auto& lightManager = scene.m_LightController;

    DirectX11::PSSetConstantBuffer(1, 1, &lightManager.m_pLightBuffer);
    if (lightManager.hasLightWithShadows)
    {
        DirectX11::PSSetConstantBuffer(2, 1, &lightManager.m_shadowMapBuffer);
    }

    auto& camera = scene.m_MainCamera;

    DeferredBuffer buffer{};
    buffer.m_InverseProjection = XMMatrixInverse(nullptr, camera.CalculateProjection());
    buffer.m_InverseView = XMMatrixInverse(nullptr, camera.CalculateView());
    buffer.m_useAmbientOcclusion = m_UseAmbientOcclusion;
    buffer.m_useEnvironmentMap = m_UseEnvironmentMap;

    DirectX11::PSSetConstantBuffer(3, 1, m_Buffer.GetAddressOf());
    DirectX11::UpdateBuffer(m_Buffer.Get(), &buffer);

    ID3D11ShaderResourceView* srvs[10] = {
        DeviceState::g_depthStancilSRV,
        m_DiffuseTexture->m_pSRV,
        m_MetalRoughTexture->m_pSRV,
        m_NormalTexture->m_pSRV,
        lightManager.hasLightWithShadows ? lightManager.GetShadowMapTexture()->m_pSRV : nullptr,
        m_UseAmbientOcclusion ? m_AmbientOcclusionTexture->m_pSRV : nullptr,
        m_UseEnvironmentMap ? m_EnvironmentMap->m_pSRV : nullptr,
        m_UseEnvironmentMap ? m_PreFilter->m_pSRV : nullptr,
        m_UseEnvironmentMap ? m_BrdfLut->m_pSRV : nullptr,
        m_EmissiveTexture->m_pSRV
    };

    DirectX11::PSSetShaderResources(0, 10, srvs);

    DirectX11::Draw(4, 0);

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

    DirectX11::PSSetShaderResources(0, 10, nullSRV);
    DirectX11::UnbindRenderTargets();
}
