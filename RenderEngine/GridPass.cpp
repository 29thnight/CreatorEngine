#include "GridPass.h"
#include "ShaderSystem.h"
#include "DeviceState.h"
#include "Scene.h"
#include "ImGuiRegister.h"

GridVertex vertices[] =
{
    { XMFLOAT3(-10000.0f, 0.0f, -10000.0f) },
    { XMFLOAT3(-10000.0f, 0.0f, 10000.0f) },
    { XMFLOAT3(10000.0f, 0.0f, 10000.0f) },
    { XMFLOAT3(10000.0f, 0.0f, -10000.0f) },
};

uint32 indices[] =
{
	0, 1, 2,
	0, 2, 3,
};

struct alignas(16) CameraPos {
	Mathf::xVector camPos;
};

GridPass::GridPass()
{
    m_pso = std::make_unique<PipelineStateObject>();

    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Grid"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Grid"];
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    InputLayOutContainer vertexLayoutDesc =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    m_pso->CreateInputLayout(std::move(vertexLayoutDesc));

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
        sizeof(GridVertex) * 4,
        D3D11_BIND_VERTEX_BUFFER,
        vertices
    );

    DirectX::SetName(m_pVertexBuffer, "GridVertexBuffer");

	m_pIndexBuffer = DirectX11::CreateBuffer(
		sizeof(uint32) * 6,
		D3D11_BIND_INDEX_BUFFER,
		indices
	);

	DirectX::SetName(m_pIndexBuffer, "GridIndexBuffer");

	m_pUniformBuffer = DirectX11::CreateBuffer(
		sizeof(GridUniformBuffer),
		D3D11_BIND_CONSTANT_BUFFER,
		&m_gridUniform
	);

	m_pCamPosBuffer = DirectX11::CreateBuffer(
		sizeof(CameraPos),
		D3D11_BIND_CONSTANT_BUFFER,
		nullptr
	);

	DirectX::SetName(m_pUniformBuffer, "GridUniformBuffer");
}

GridPass::~GridPass()
{
}

void GridPass::PrepareCameraType(Camera* camera)
{
	m_pEditorCamera = camera;
}

void GridPass::Execute(RenderScene& scene, Camera& camera)
{
	if (camera.m_applyRenderPipelinePass.m_GridPass == false)
	{
		return;
	}

    auto deviceContext = DeviceState::g_pDeviceContext;

	m_gridConstant.world = XMMatrixIdentity();
	m_gridConstant.view = camera.CalculateView();
	m_gridConstant.projection = camera.CalculateProjection();

    float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // 블렌드 팩터 (사용되지 않음)
    UINT sampleMask = 0xffffffff; // 샘플 마스크 (모든 샘플 활성화)
	DirectX11::OMSetBlendState(DeviceState::g_pBlendState, blendFactor, sampleMask);

	CameraPos camPos = { camera.m_eyePosition };
    DirectX11::UpdateBuffer(m_pCamPosBuffer.Get(), &camPos);
    DirectX11::VSSetConstantBuffer(0, 1, m_pGridConstantBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(0, 1, m_pUniformBuffer.GetAddressOf());
	DirectX11::VSSetConstantBuffer(1, 1, m_pCamPosBuffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(1, 1, m_pCamPosBuffer.GetAddressOf());
    DirectX11::UpdateBuffer(m_pGridConstantBuffer.Get(), &m_gridConstant);
	DirectX11::UpdateBuffer(m_pUniformBuffer.Get(), &m_gridUniform);

    m_pso->Apply();

    ID3D11RenderTargetView* rtv = camera.m_renderTarget->GetRTV();
    DeviceState::g_pDeviceContext->OMSetRenderTargets(1, &rtv, camera.m_depthStencil->m_pDSV);

    UINT stride = sizeof(GridVertex);
    UINT offset = 0;

    deviceContext->IASetVertexBuffers(0, 1,
        m_pVertexBuffer.GetAddressOf(), &stride, &offset);

	deviceContext->IASetIndexBuffer(m_pIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

	deviceContext->DrawIndexed(6, 0, 0);

    ID3D11ShaderResourceView* nullSRV[3] = { nullptr, nullptr, nullptr };
    DirectX11::PSSetShaderResources(0, 3, nullSRV);
    DirectX11::UnbindRenderTargets();

    DirectX11::OMSetBlendState(nullptr, nullptr, sampleMask);

}

void GridPass::ControlPanel()
{
    ImGui::DragFloat("Fade Start", &m_gridUniform.fadeStart, 0.f, 1.f);
    ImGui::DragFloat("Fade End", &m_gridUniform.fadeEnd, 0.f, 1000.f);
}

void GridPass::GridSetting()
{
    ImGui::ColorEdit4("Grid Color", &m_gridUniform.gridColor.x);
    ImGui::ColorEdit4("Checker Color", &m_gridUniform.checkerColor.x);
    ImGui::DragFloat("Unit Size", &m_gridUniform.unitSize, 1.f, 100.f);
    ImGui::DragFloat("Major Line Thickness", &m_gridUniform.majorLineThickness, 0.1f, 10.f);
    ImGui::DragFloat("Minor Line Thickness", &m_gridUniform.minorLineThickness, 0.1f, 10.f);
    ImGui::DragFloat("Minor Line Alpha", &m_gridUniform.minorLineAlpha, 0.f, 1.f);
    ImGui::DragFloat3("Center Offset", &m_gridUniform.centerOffset.x, -1000.f, 1000.f);
    ImGui::DragInt("Subdivisions", &m_gridUniform.subdivisions, 1, 100);
}

void GridPass::Resize(uint32_t width, uint32_t height)
{
}
