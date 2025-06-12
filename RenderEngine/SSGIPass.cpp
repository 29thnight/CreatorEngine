#include "SSGIPass.h"
#include "ShaderSystem.h"
#include "Scene.h"

cbuffer SSGIParams
{
    XMMATRIX inverseProjection;
    float2 screenSize; // 화면 크기
    float radius; // 샘플링 반경
    float thickness; // 두께
};

SSGIPass::SSGIPass()
{
    m_pso = std::make_unique<PipelineStateObject>();
    //m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
    //m_pso->m_pixelShader = &ShaderSystem->PixelShaders["SSGI"];
	m_pso->m_computeShader = &ShaderSystem->ComputeShaders["SSGI"];
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

    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(pointSampler);

    m_pTempTexture = Texture::Create(
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "CopiedTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
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
    m_pso->Apply();

    auto& deviceContext = DeviceState::g_pDeviceContext;
    ID3D11ShaderResourceView* srv[4] = {
        camera.m_depthStencil->m_pSRV,
        camera.m_renderTarget->m_pSRV,
        m_pNormalTexture->m_pSRV,
        m_pLightEmissiveTexture->m_pSRV
    };
	SSGIParams params;
    params.inverseProjection = camera.CalculateInverseProjection();
	params.screenSize = { DeviceState::g_ClientRect.width, DeviceState::g_ClientRect.height };
    params.radius = radius;;
	params.thickness = thickness;
	DirectX11::UpdateBuffer(m_Buffer.Get(), &params);

    DirectX11::CSSetShaderResources(0, 4, srv);
	DirectX11::CSSetConstantBuffer(0, 1, m_Buffer.GetAddressOf());
	DirectX11::CSSetUnorderedAccessViews(0, 1, &m_pTempTexture->m_pUAV, nullptr);
    
    DirectX11::Dispatch(DeviceState::g_ClientRect.width / 16, DeviceState::g_ClientRect.height / 16, 1);

    ID3D11ShaderResourceView* nullsrv[4] = { nullptr, nullptr, nullptr, nullptr };
    ID3D11UnorderedAccessView* nulluav = nullptr;
    DirectX11::CSSetShaderResources(0, 4, nullsrv);
    DirectX11::CSSetUnorderedAccessViews(0, 1, &nulluav, nullptr);

	DirectX11::CopyResource(camera.m_renderTarget->m_pTexture, m_pTempTexture->m_pTexture);
}

void SSGIPass::ControlPanel()
{
}

void SSGIPass::Resize(uint32_t width, uint32_t height)
{
}
