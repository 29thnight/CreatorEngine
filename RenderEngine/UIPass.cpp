#include "UIPass.h"
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

void UIPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    if (renderData->m_UIRenderQueue.empty()) return;

    ID3D11DeviceContext* deferredPtr = deferredContext;

    m_pso->Apply(deferredPtr);

    auto spriteBatch = std::make_unique<SpriteBatch>(deferredPtr);

    ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(deferredPtr, 1, &view, renderData->m_renderTarget->m_pDSV);
    DirectX11::RSSetViewports(deferredPtr, 1, &DirectX11::DeviceStates->g_Viewport);
    DirectX11::OMSetDepthStencilState(deferredPtr, m_NoWriteDepthStencilState.Get(), 1);
    DirectX11::OMSetBlendState(deferredPtr, DirectX11::DeviceStates->g_pBlendState, nullptr, 0xFFFFFFFF);
    camera.UpdateBuffer(deferredPtr);

    DirectX11::VSSetConstantBuffer(deferredPtr, 0, 1, m_UIBuffer.GetAddressOf());

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

    DirectX11::OMSetDepthStencilState(deferredPtr, DirectX11::DeviceStates->g_pDepthStencilState, 1);
    DirectX11::OMSetBlendState(deferredPtr, nullptr, nullptr, 0xFFFFFFFF);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &nullSRV);
    DirectX11::UnbindRenderTargets(deferredPtr);

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