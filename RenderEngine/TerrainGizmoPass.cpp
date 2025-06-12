#include "TerrainGizmoPass.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Terrain.h"

TerrainGizmoPass::TerrainGizmoPass()
{
    m_pso = std::make_unique<PipelineStateObject>();
    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["TerrainDebugBrush"];
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
    CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };

    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateRasterizerState(
            &rasterizerDesc,
            &m_pso->m_rasterizerState
        )
    );

    m_pTempTexture = Texture::Create(
        DeviceState::g_ClientRect.width, 
        DeviceState::g_ClientRect.height, 
        "copy", 
        DXGI_FORMAT_R16G16B16A16_FLOAT, 
        D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);

    m_pTempTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_Buffer = DirectX11::CreateBuffer(
		sizeof(TerrainGizmoBuffer),
		D3D11_BIND_CONSTANT_BUFFER,
		nullptr
	);

    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(pointSampler);
}

void TerrainGizmoPass::Execute(RenderScene& scene, Camera& camera)
{
    m_pso->Apply();
    
	DirectX11::CopyResource(m_pTempTexture->m_pTexture, camera.m_renderTarget->m_pTexture);

    auto& deviceContext = DeviceState::g_pDeviceContext;
    ID3D11RenderTargetView* rtv = camera.m_renderTarget->GetRTV();
	deviceContext->OMSetRenderTargets(1, &rtv, nullptr);

    for (auto& obj : scene.GetScene()->m_SceneObjects) {
        if (obj->IsDestroyMark()) continue;
        if (obj->HasComponent<TerrainComponent>()) {

            auto terrain = obj->GetComponent<TerrainComponent>();
            auto terrainMesh = terrain->GetMesh();

            if (terrainMesh)
            {
                TerrainGizmoBuffer terrainGizmoBuffer = {};
                auto terrainBrush = terrain->GetCurrentBrush();
                if (terrainBrush != nullptr) {
                    terrainGizmoBuffer.gBrushPosition = terrain->GetCurrentBrush()->m_center;
                    terrainGizmoBuffer.gBrushRadius = terrain->GetCurrentBrush()->m_radius;
                    DirectX11::UpdateBuffer(m_Buffer.Get(), &terrainGizmoBuffer);
                    DirectX11::PSSetConstantBuffer(0, 1, m_Buffer.GetAddressOf());
					DirectX11::PSSetShaderResources(0, 1, &m_pTempTexture->m_pSRV);

                    scene.UpdateModel(obj->m_transform.GetWorldMatrix());
                    terrainMesh->Draw();
                }
            }
        }
        
    }
    ID3D11RenderTargetView* nullrtv = nullptr;
    deviceContext->OMSetRenderTargets(1, &nullrtv, nullptr);


}

void TerrainGizmoPass::ControlPanel()
{
}

void TerrainGizmoPass::Resize(uint32_t width, uint32_t height)
{
}
