#include "BlitPass.h"
#include "ShaderSystem.h"
#include "Camera.h"
#include "RHI/RHI.h"

BlitPass::BlitPass()
{
	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Blit"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    D3D11_INPUT_ELEMENT_DESC a{};
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

    CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };

    DirectX11::ThrowIfFailed(
        DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
            &rasterizerDesc,
            &m_pso->m_rasterizerState
        )
    );

    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(pointSampler);
}

BlitPass::~BlitPass()
{
}

void BlitPass::Initialize(ID3D11RenderTargetView* backBufferRTV)
{
	m_backBufferRTV = backBufferRTV;
}

// RHI로 이식된 첫 패스(PHASE 3-1). 커맨드 기록이 전역 DirectX11:: 호출 대신
// RHI 컨텍스트를 통한다 — DX12(EnhancedSceneRenderer)에서는 같은 코드가 커맨드
// 리스트에 기록된다. PSO Apply는 아직 DX11 고유 경로다(PSO 추상화는 3-4).
void BlitPass::Execute(RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    if(camera.m_avoidRenderPass.Test((flag)RenderPipelinePass::BlitPass)) return;

    m_pso->Apply();

    RHICommandContext& context = RHI::Immediate();

    const RHIViewport viewport = RHI::Device().GetFullViewport();
    context.SetViewports(1, &viewport);

    RHINativeRenderTarget rtv = RHI::Device().GetBackBufferRenderTarget();
    if (!rtv)
    {
        rtv = m_backBufferRTV;
    }

    if (!rtv)
    {
        Debug->LogError("[BlitPass] -> Back buffer RTV is not available");
        return;
    }

    RHINativeRenderTarget rtvHandle = rtv;
    context.SetRenderTargets(1, &rtvHandle, nullptr);

    RHINativeShaderResource srvHandle = renderData->m_renderTarget->m_pSRV;
    context.SetPixelShaderResources(0, 1, &srvHandle);
    context.Draw(4, 0);

    RHINativeShaderResource nullSRV = nullptr;
    context.SetPixelShaderResources(0, 1, &nullSRV);
    context.UnbindRenderTargets();
}
