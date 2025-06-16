#include "SSGIPass.h"
#include "ShaderSystem.h"
#include "Scene.h"

cbuffer SSGIParams
{
    XMMATRIX invVP;
    XMMATRIX proj;
    XMMATRIX inverseProjection;
    float2 screenSize; // 화면 크기
    float radius; // 샘플링 반경
    float thickness; // 두께
    UINT frameIndex;
};

SSGIPass::SSGIPass()
{
    //m_pso = std::make_unique<PipelineStateObject>();
    //m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
    //m_pso->m_pixelShader = &ShaderSystem->PixelShaders["SSGI"];
    m_pSSGIShader = &ShaderSystem->ComputeShaders["SSGI"];
    //m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

    m_Buffer = DirectX11::CreateBuffer(sizeof(SSGIParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);

    //InputLayOutContainer vertexLayoutDesc = {
    //    { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    //    { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    //    { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    //    { "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    //    { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    //    { "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    //    { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    //    { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    //};
    //m_pso->CreateInputLayout(std::move(vertexLayoutDesc));
    //CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
    //
    //DirectX11::ThrowIfFailed(
    //    DeviceState::g_pDevice->CreateRasterizerState(
    //        &rasterizerDesc,
    //        &m_pso->m_rasterizerState
    //    )
    //);

    sample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
    pointSample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pTempTexture = Texture::Create(
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "CopiedTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
}

SSGIPass::~SSGIPass()
{
}

void SSGIPass::Initialize(Texture* diffuse, Texture* normal, Texture* lightEmissive)
{
	m_pDiffuseTexture = diffuse;
	m_pNormalTexture = normal;
	m_pLightEmissiveTexture = lightEmissive;
}

void SSGIPass::Execute(RenderScene& scene, Camera& camera)
{
 //   if (!RenderPassData::VaildCheck(&camera)) return;
 //   auto renderData = RenderPassData::GetData(&camera);

 //   auto& deviceContext = DeviceState::g_pDeviceContext;
	//DirectX11::CSSetShader(m_pSSGIShader->GetShader(), nullptr, 0);
 //   DeviceState::g_pDeviceContext->CSSetSamplers(0, 1, &sample->m_SamplerState); // sampler 0
 //   DeviceState::g_pDeviceContext->CSSetSamplers(1, 1, &pointSample->m_SamplerState); // sampler 1
 //   ID3D11ShaderResourceView* srv[4] = {
 //       renderData->m_depthStencil->m_pSRV,
 //       renderData->m_renderTarget->m_pSRV,
 //       m_pNormalTexture->m_pSRV,
 //       m_pLightEmissiveTexture->m_pSRV
 //   };
	//SSGIParams params;
 //   params.invVP = camera.CalculateInverseView() * camera.CalculateInverseProjection();
 //   params.proj = camera.CalculateProjection();
 //   params.inverseProjection = XMMatrixInverse(nullptr, camera.CalculateProjection());
	//params.screenSize = { DeviceState::g_ClientRect.width, DeviceState::g_ClientRect.height };
 //   params.radius = radius;;
	//params.thickness = thickness;
 //   params.frameIndex = Time->GetFrameCount();
	//DirectX11::UpdateBuffer(m_Buffer.Get(), &params);

 //   DirectX11::CSSetShaderResources(0, 4, srv);
	//DirectX11::CSSetConstantBuffer(0, 1, m_Buffer.GetAddressOf());
	//DirectX11::CSSetUnorderedAccessViews(0, 1, &m_pTempTexture->m_pUAV, nullptr);
 //   
 //   DirectX11::Dispatch(DeviceState::g_ClientRect.width / 32, DeviceState::g_ClientRect.height / 32, 1);

 //   ID3D11ShaderResourceView* nullsrv[4] = { nullptr, nullptr, nullptr, nullptr };
 //   ID3D11UnorderedAccessView* nulluav = nullptr;
 //   DirectX11::CSSetShaderResources(0, 4, nullsrv);
 //   DirectX11::CSSetUnorderedAccessViews(0, 1, &nulluav, nullptr);

	//DirectX11::CopyResource(renderData->m_renderTarget->m_pTexture, m_pTempTexture->m_pTexture);

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

void SSGIPass::CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    ID3D11ShaderResourceView* srv[4] = {
    renderData->m_depthStencil->m_pSRV,
    renderData->m_renderTarget->m_pSRV,
    m_pNormalTexture->m_pSRV,
    m_pLightEmissiveTexture->m_pSRV
    };

    ID3D11DeviceContext* defferdPtr = defferdContext;
    DirectX11::CSSetShader(defferdPtr, m_pSSGIShader->GetShader(), nullptr, 0);
    DirectX11::CSSetSamplers(defferdPtr, 0, 1, &sample->m_SamplerState); // sampler 0
    DirectX11::CSSetSamplers(defferdPtr, 1, 1, &pointSample->m_SamplerState); // sampler 1

    DirectX11::CSSetShaderResources(defferdPtr, 0, 4, srv);
    DirectX11::CSSetConstantBuffer(defferdPtr, 0, 1, m_Buffer.GetAddressOf());
    DirectX11::CSSetUnorderedAccessViews(defferdPtr, 0, 1, &m_pTempTexture->m_pUAV, nullptr);

    SSGIParams params;
    params.invVP = camera.CalculateInverseView() * camera.CalculateInverseProjection();
    params.proj = camera.CalculateProjection();
    params.inverseProjection = XMMatrixInverse(nullptr, camera.CalculateProjection());
    params.screenSize = { DeviceState::g_ClientRect.width, DeviceState::g_ClientRect.height };
    params.radius = radius;;
    params.thickness = thickness;
    params.frameIndex = Time->GetFrameCount();

    camera.UpdateBuffer(defferdPtr);
    DirectX11::UpdateBuffer(defferdPtr, m_Buffer.Get(), &params);

    DirectX11::Dispatch(defferdPtr, DeviceState::g_ClientRect.width / 64, DeviceState::g_ClientRect.height / 64, 1);

    ID3D11ShaderResourceView* nullsrv[4] = { nullptr, nullptr, nullptr, nullptr };
    ID3D11UnorderedAccessView* nulluav = nullptr;
    DirectX11::CSSetShaderResources(defferdPtr, 0, 4, nullsrv);
    DirectX11::CSSetUnorderedAccessViews(defferdPtr, 0, 1, &nulluav, nullptr);

    DirectX11::CopyResource(defferdPtr, renderData->m_renderTarget->m_pTexture, m_pTempTexture->m_pTexture);

    ID3D11CommandList* commandList{};
    defferdPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void SSGIPass::ControlPanel()
{
    ImGui::PushID(this);
    ImGui::Text("SSGI");
    ImGui::SliderFloat("Radius", &radius, 0.0f, 10.0f);
    ImGui::SliderFloat("Thickness", &thickness, 0.0f, 1.0f);
    ImGui::PopID();
}

void SSGIPass::Resize(uint32_t width, uint32_t height)
{
}
