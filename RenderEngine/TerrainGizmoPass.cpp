#include "TerrainGizmoPass.h"
#include "RHI/RHI.h"
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
        DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
            &rasterizerDesc,
            &m_pso->m_rasterizerState
        )
    );

    m_pTempTexture = Texture::Create(
        DirectX11::DeviceStates->g_ClientRect.width, 
        DirectX11::DeviceStates->g_ClientRect.height, 
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
    ExecuteCommandList(scene, camera);
}

void TerrainGizmoPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

    m_pso->Apply(deferredPtr);

    context.CopyResource(m_pTempTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

    ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
    context.SetRenderTarget(rtv, nullptr);
    {
		const RHIViewport rhiVp = RHI::Device().GetFullViewport();
		context.SetViewports(1, &rhiVp);
	}
    context.SetPixelShaderConstantBuffer(0, m_Buffer.Get());
    context.SetPixelShaderResource(0, m_pTempTexture->m_pSRV);

    camera.DeferredUpdateBuffer(deferredPtr, renderData->m_frameCalculatedView, renderData->m_frameCalculatedProjection);
    scene.UseModel(deferredPtr);
    for (auto& obj : scene.GetScene()->m_SceneObjects) 
    {
        if (obj == nullptr)continue;
        if (obj->IsDestroyMark()) continue;
        if (obj->HasComponent<TerrainComponent>()) 
        {
            auto terrain = obj->GetComponent<TerrainComponent>();
            auto terrainMesh = terrain->GetMesh();

            if (terrainMesh)
            {
                TerrainGizmoBuffer terrainGizmoBuffer = {};
                auto terrainBrush = terrain->GetCurrentBrush();
                if (terrainBrush && terrainBrush->m_isEditMode)
                {
					uint32_t maskID = terrainBrush->m_maskID;
					ID3D11ShaderResourceView* nullSRV = nullptr;
                    if (maskID!= 0xFFFFFFFF)
                    {
						auto& mask = terrainBrush->m_masks[maskID];
						if (mask.m_maskSRV)
						{
							context.SetPixelShaderResource(1, mask.m_maskSRV);
							terrainGizmoBuffer.maskWidth = mask.m_maskWidth;
							terrainGizmoBuffer.maskHeight = mask.m_maskHeight;
						}
                        else 
                        {
                            context.SetPixelShaderResource(1, nullptr);
							terrainGizmoBuffer.maskWidth = 0;
							terrainGizmoBuffer.maskHeight = 0;
                        }
                    }
                    else 
                    {
                        context.SetPixelShaderResource(1, nullptr);
						terrainGizmoBuffer.maskWidth = 0;
						terrainGizmoBuffer.maskHeight = 0;
                    }
                    terrainGizmoBuffer.gBrushPosition = terrain->GetCurrentBrush()->m_center;
                    terrainGizmoBuffer.gBrushRadius = terrain->GetCurrentBrush()->m_radius;
					terrainGizmoBuffer.isEditMode = terrain->GetCurrentBrush()->m_isEditMode;
                    terrainGizmoBuffer.view = renderData->m_frameCalculatedView;
                    terrainGizmoBuffer.proj = renderData->m_frameCalculatedProjection;
                    context.UpdateBuffer(m_Buffer.Get(), &terrainGizmoBuffer);

                    scene.UpdateModel(obj->m_transform.GetWorldMatrix(), deferredPtr);
                    terrainMesh->Draw(deferredPtr);
                }
            }
        }

    }
    ID3D11RenderTargetView* nullrtv = nullptr;
    context.SetRenderTarget(nullrtv, nullptr);

    ID3D11CommandList* commandList{};
    deferredPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void TerrainGizmoPass::ControlPanel()
{
}

void TerrainGizmoPass::Resize(uint32_t width, uint32_t height)
{
}
