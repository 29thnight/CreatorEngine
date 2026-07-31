#include "UIPass.h"
#include "RHI/RHI.h"
#include <DirectXTK/SpriteBatch.h>
#include "ShaderSystem.h"
#include "RenderPassData.h"
#include "RenderScene.h"
#include "../ScriptBinder/ImageComponent.h"

UIPass::UIPass()
{
    m_pso = std::make_unique<PipelineStateObject>();
    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["UI"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["UI"];
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    InputLayOutContainer vertexLayoutDesc =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    DirectX11::ThrowIfFailed(
        DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(
            &depthStencilDesc,
            &m_NoWriteDepthStencilState
        )
    );

    m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();
    m_pso->m_blendState = DirectX11::DeviceStates->g_pBlendState;

    m_UIBuffer = DirectX11::CreateBuffer(sizeof(ImageInfo), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

void UIPass::Initialize(Texture* renderTargetView)
{
    m_renderTarget = renderTargetView;
    m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(DirectX11::DeviceStates->g_pDeviceContext);
    m_commonStates = std::make_unique<DirectX::CommonStates>(DirectX11::DeviceStates->g_pDevice);
}

void UIPass::Execute(RenderScene& scene, Camera& camera)
{
    ExecuteCommandList(scene, camera);
}

void UIPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    if (renderData->m_UIRenderQueue.empty()) return;

    ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

    // RHI 이식(PHASE 3-1). 커맨드 빌드 시스템이 아직 네이티브 deferred 컨텍스트를
    // 넘기므로 여기서 감싼다 — 서명이 RHICommandContext&로 바뀌면 이 줄이 사라진다.
    // PSO Apply·SpriteBatch·FinishCommandList는 DX11 고유 경로로 남는다(각각 3-4·이식 후반).

    m_pso->Apply(deferredPtr);

    auto spriteBatch = std::make_unique<SpriteBatch>(deferredPtr);

    RHINativeRenderTarget view = renderData->m_renderTarget->GetRTV();
    context.SetRenderTargets(1, &view, renderData->m_renderTarget->m_pDSV);

    const RHIViewport viewport = RHI::Device().GetFullViewport();
    context.SetViewports(1, &viewport);

    context.SetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
    context.SetBlendState(RHI::Device().GetDefaultBlendState(), nullptr, 0xFFFFFFFF);
    camera.DeferredUpdateBuffer(deferredPtr, renderData->m_frameCalculatedView, renderData->m_frameCalculatedProjection);

    RHINativeBuffer uiBuffer = m_UIBuffer.Get();
    context.SetVertexShaderConstantBuffers(0, 1, &uiBuffer);

    for (auto* proxy : renderData->m_UIRenderQueue)
    {
        if (proxy->isCustomShader())
        {
            auto customShaderFunc = [=]()
            {
                auto* pixelShader = proxy->GetCustomPixelShader()->GetShader();
				deferredPtr->PSSetShader(pixelShader, nullptr, 0);
                proxy->UpdateShaderBuffer(deferredPtr);
            };

            spriteBatch->Begin(DirectX::SpriteSortMode_FrontToBack,
                m_commonStates->NonPremultiplied(), m_commonStates->LinearClamp()
                , nullptr, nullptr, customShaderFunc);
        }
        else
        {
            spriteBatch->Begin(DirectX::SpriteSortMode_FrontToBack,
				m_commonStates->NonPremultiplied(), m_commonStates->LinearClamp());
        }

        proxy->Draw(spriteBatch);

        spriteBatch->End();
    }

    context.SetDepthStencilState(RHI::Device().GetDefaultDepthStencilState(), 1);
    context.SetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    RHINativeShaderResource nullSRV = nullptr;
    context.SetPixelShaderResources(0, 1, &nullSRV);
    context.UnbindRenderTargets();

    ID3D11CommandList* commandList{};
    DirectX11::ThrowIfFailed(
        deferredPtr->FinishCommandList(FALSE, &commandList)
    );
    PushQueue(camera.m_cameraIndex, commandList);

    renderData->ClearUIRenderQueue();
}

bool UIPass::compareLayer(int a, int b)
{
    return a < b;
}

void UIPass::ControlPanel()
{
}

void UIPass::Resize(uint32_t width, uint32_t height)
{
}