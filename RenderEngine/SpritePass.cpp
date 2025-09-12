#include "SpritePass.h"
#include "ShaderSystem.h"
#include "RenderScene.h"

SpritePass::SpritePass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Sprite"];

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

    CD3D11_DEPTH_STENCIL_DESC depthStencilDesc{ CD3D11_DEFAULT() };
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(
			&depthStencilDesc,
			&m_NoWriteDepthStencilState
		)
	);

	m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();
	m_pso->m_blendState = DirectX11::DeviceStates->g_pBlendState;
}

SpritePass::~SpritePass()
{
}

void SpritePass::Execute(RenderScene& scene, Camera& camera)
{
    ExecuteCommandList(scene, camera);
}

void SpritePass::ControlPanel()
{
}

void SpritePass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
    if (camera.m_avoidRenderPass.Test((flag)RenderPipelinePass::SpritePass))
    {
        return;
    }

    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);
    ID3D11DeviceContext* deferredPtr = deferredContext;

    m_pso->Apply(deferredPtr);
    ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(deferredPtr, 1, &rtv, renderData->m_depthStencil->m_pDSV);

    deferredPtr->OMSetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
    deferredPtr->OMSetBlendState(DirectX11::DeviceStates->g_pBlendState, nullptr, 0xFFFFFFFF);
    DirectX11::RSSetViewports(deferredPtr, 1, &DirectX11::DeviceStates->g_Viewport);

    camera.UpdateBuffer(deferredPtr);
    scene.UseModel(deferredPtr);

    for (auto& proxy : renderData->m_spriteRenderQueue)
    {
        if (!proxy || !proxy->m_quadMesh || !proxy->m_spriteTexture) continue;
        if (proxy->m_customPSO)
        {
            proxy->m_customPSO->Apply(deferredPtr);
        }
        else
        {
            m_pso->Apply(deferredPtr);
        }

        scene.UpdateModel(proxy->m_worldMatrix, deferredPtr);
        ID3D11ShaderResourceView* srv = proxy->m_spriteTexture->m_pSRV;
        DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &srv);
        proxy->m_quadMesh->Draw(deferredPtr);
    }

    ID3D11ShaderResourceView* nullSRV = nullptr;
    DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &nullSRV);
    ID3D11RenderTargetView* nullRTV = nullptr;
    deferredPtr->OMSetRenderTargets(1, &nullRTV, nullptr);
    ID3D11BlendState* nullBlend = nullptr;
    DirectX11::OMSetBlendState(deferredPtr, nullBlend, nullptr, 0xFFFFFFFF);
    DirectX11::OMSetDepthStencilState(deferredPtr, DirectX11::DeviceStates->g_pDepthStencilState, 1);

    ID3D11CommandList* commandList{};
    deferredPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void SpritePass::Resize(uint32_t width, uint32_t height)
{
}
