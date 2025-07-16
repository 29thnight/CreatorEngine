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
	auto clampSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(pointSampler);
	m_pso->m_samplers.push_back(clampSampler);
}

void TerrainGizmoPass::Execute(RenderScene& scene, Camera& camera)
{
    auto cmdQueuePtr = GetCommandQueue(camera.m_cameraIndex);

    if (nullptr != cmdQueuePtr)
    {
        while (!cmdQueuePtr->empty())
        {
            ID3D11CommandList* CommandJob;
            if (cmdQueuePtr->try_pop(CommandJob))
            {
                DirectX11::ExecuteCommandList(CommandJob, true);
                Memory::SafeDelete(CommandJob);
            }
        }
    }
}

void TerrainGizmoPass::CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    ID3D11DeviceContext* defferdPtr = defferdContext;

    m_pso->Apply(defferdPtr);

    DirectX11::CopyResource(defferdPtr, m_pTempTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

    ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(defferdPtr, 1, &rtv, nullptr);
    DirectX11::RSSetViewports(defferdPtr, 1, &DeviceState::g_Viewport);
    DirectX11::PSSetConstantBuffer(defferdPtr, 0, 1, m_Buffer.GetAddressOf());
    DirectX11::PSSetShaderResources(defferdPtr, 0, 1, &m_pTempTexture->m_pSRV);

    camera.UpdateBuffer(defferdPtr);
    scene.UseModel(defferdPtr);
    for (auto& obj : scene.GetScene()->m_SceneObjects) 
    {
        if (obj->IsDestroyMark()) continue;
        if (obj->HasComponent<TerrainComponent>()) 
        {

            auto terrain = obj->GetComponent<TerrainComponent>();
            auto terrainMesh = terrain->GetMesh();

            if (terrainMesh)
            {
                TerrainGizmoBuffer terrainGizmoBuffer = {};
                auto terrainBrush = terrain->GetCurrentBrush();
                if (terrainBrush != nullptr) 
                {
					uint32_t maskID = terrainBrush->m_maskID;
					ID3D11ShaderResourceView* nullSRV = nullptr;
                    if (maskID!= 0xFFFFFFFF)
                    {
						auto& mask = terrainBrush->m_masks[maskID];
						if (mask.m_maskSRV)
						{
							DirectX11::PSSetShaderResources(defferdPtr, 1, 1, &mask.m_maskSRV);
							terrainGizmoBuffer.maskWidth = mask.m_maskWidth;
							terrainGizmoBuffer.maskHeight = mask.m_maskHeight;
						}
                        else 
                        {
                            DirectX11::PSSetShaderResources(defferdPtr, 1, 1, &nullSRV);
							terrainGizmoBuffer.maskWidth = 0;
							terrainGizmoBuffer.maskHeight = 0;
                        }
                    }
                    else 
                    {
                        DirectX11::PSSetShaderResources(defferdPtr, 1, 1, &nullSRV);
						terrainGizmoBuffer.maskWidth = 0;
						terrainGizmoBuffer.maskHeight = 0;
                    }
                    terrainGizmoBuffer.gBrushPosition = terrain->GetCurrentBrush()->m_center;
                    terrainGizmoBuffer.gBrushRadius = terrain->GetCurrentBrush()->m_radius;
					terrainGizmoBuffer.isEditMode = terrain->GetCurrentBrush()->m_isEditMode;
                    DirectX11::UpdateBuffer(defferdPtr, m_Buffer.Get(), &terrainGizmoBuffer);


                    scene.UpdateModel(obj->m_transform.GetWorldMatrix(), defferdPtr);
                    terrainMesh->Draw(defferdPtr);
                }
            }
        }

    }
    ID3D11RenderTargetView* nullrtv = nullptr;
    DirectX11::OMSetRenderTargets(defferdPtr, 1, &nullrtv, nullptr);

    ID3D11CommandList* commandList{};
    defferdPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void TerrainGizmoPass::ControlPanel()
{
}

void TerrainGizmoPass::Resize(uint32_t width, uint32_t height)
{
}
