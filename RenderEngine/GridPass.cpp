#include "GridPass.h"
#include "AssetSystem.h"
#include "DeviceState.h"
#include "Scene.h"

// XZ 평면에 위치한 쿼드 (예: -100 ~ +100 범위)
// (원하는 영역만큼 확장할 수 있습니다.)
GridVertex vertices[] =
{
    { XMFLOAT3(-100.0f, 0.0f, -100.0f) },
    { XMFLOAT3(-100.0f, 0.0f,  100.0f) },
    { XMFLOAT3(100.0f, 0.0f,  100.0f) },

    { XMFLOAT3(-100.0f, 0.0f, -100.0f) },
    { XMFLOAT3(100.0f, 0.0f,  100.0f) },
    { XMFLOAT3(100.0f, 0.0f, -100.0f) },
};

GridPass::GridPass()
{
    m_pso = std::make_unique<PipelineStateObject>();

    m_pso->m_vertexShader = &AssetsSystems->VertexShaders["Grid"];
    m_pso->m_pixelShader = &AssetsSystems->PixelShaders["Grid"];
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

    CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.AntialiasedLineEnable = true;

    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateRasterizerState(
            &rasterizerDesc,
            &m_pso->m_rasterizerState
        )
    );

    m_pGridConstantBuffer = DirectX11::CreateBuffer(
        sizeof(GridConstantBuffer),
        D3D11_BIND_CONSTANT_BUFFER,
        &m_gridConstant
    );

    DirectX::SetName(m_pGridConstantBuffer, "GridConstantBuffer");

    m_pVertexBuffer = DirectX11::CreateBuffer(
        sizeof(vertices),
        D3D11_BIND_VERTEX_BUFFER,
        vertices
    );

    DirectX::SetName(m_pVertexBuffer, "GridVertexBuffer");
}

GridPass::~GridPass()
{
}

void GridPass::Initialize(Texture* color, Texture* grid)
{
    m_colorTexture = color;
    m_gridTexture = grid;
}

void GridPass::Execute(Scene& scene)
{
    auto deviceContext = DeviceState::g_pDeviceContext;
    //copyResource
    deviceContext->CopyResource(m_gridTexture->m_pTexture, m_colorTexture->m_pTexture);

    XMMATRIX viewProj = scene.m_MainCamera.CalculateView() * scene.m_MainCamera.CalculateProjection();

    m_gridConstant.m_viewProj = XMMatrixTranspose(viewProj);

    DirectX11::VSSetConstantBuffer(0, 1, m_pGridConstantBuffer.GetAddressOf());
    DirectX11::UpdateBuffer(m_pGridConstantBuffer.Get(), &m_gridConstant);

    m_pso->Apply();

    ID3D11RenderTargetView* rtv = m_gridTexture->GetRTV();
    DeviceState::g_pDeviceContext->OMSetRenderTargets(1, &rtv, DeviceState::g_pDepthStencilView);

    UINT stride = sizeof(GridVertex);
    UINT offset = 0;

    deviceContext->IASetVertexBuffers(0, 1,
        m_pVertexBuffer.GetAddressOf(), &stride, &offset);

    DirectX11::Draw(6, 0);

    ID3D11ShaderResourceView* nullSRV[3] = { nullptr, nullptr, nullptr };
    DirectX11::PSSetShaderResources(0, 3, nullSRV);
    DirectX11::UnbindRenderTargets();
}
