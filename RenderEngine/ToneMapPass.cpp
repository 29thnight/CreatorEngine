#include "ToneMapPass.h"
#include "ShaderSystem.h"
#include "ImGuiRegister.h"
#include "DeviceState.h"
#include "Camera.h"
#include "TimeSystem.h"
#include "ResourceAllocator.h"

ToneMapPass::ToneMapPass()
{
    m_pso = std::make_unique<PipelineStateObject>();

    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["ToneMapACES"];
    m_pAutoExposureEvalCS = &ShaderSystem->ComputeShaders["AutoExposureEval"];
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

    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(pointSampler);

	m_pACESConstantBuffer = DirectX11::CreateBuffer(
        sizeof(ToneMapACESConstant), 
        D3D11_BIND_CONSTANT_BUFFER, 
        &m_toneMapACESConstant
    );

	DirectX::SetName(m_pACESConstantBuffer, "ToneMapACESConstantBuffer");

	m_pReinhardConstantBuffer = DirectX11::CreateBuffer(
		sizeof(ToneMapReinhardConstant),
		D3D11_BIND_CONSTANT_BUFFER,
		&m_toneMapReinhardConstant
	);

	DirectX::SetName(m_pReinhardConstantBuffer, "ToneMapReinhardConstantBuffer");

    auto& device = DeviceState::g_pDevice;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    D3D11_TEXTURE2D_DESC readbackDesc = texDesc;
    readbackDesc.Usage = D3D11_USAGE_STAGING;
    readbackDesc.BindFlags = 0;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    device->CreateTexture2D(&readbackDesc, nullptr, &readbackTexture);

    PrepareDownsampleTextures(DeviceState::g_ClientRect.width, DeviceState::g_ClientRect.height);
}

ToneMapPass::~ToneMapPass()
{
	Memory::SafeDelete(m_pACESConstantBuffer);
    Memory::SafeDelete(m_pReinhardConstantBuffer);

    for (auto* tex : m_downsampleTextures)
    {
        DeallocateResource(tex);
    }
    m_downsampleTextures.clear();

	//Memory::SafeDelete(m_pAutoExposureReadBuffer);
}

void ToneMapPass::Initialize(Texture* dest)
{
    m_DestTexture = dest;
}

void ToneMapPass::ToneMapSetting(bool isAbleToneMap, ToneMapType type)
{
	m_isAbleToneMap = isAbleToneMap;
	m_toneMapType = type;
}

void ToneMapPass::Execute(RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    if (m_isAbleAutoExposure && !m_downsampleTextures.empty())
    {
        ID3D11ShaderResourceView* currentSRV = renderData->m_renderTarget->m_pSRV;
        const UINT offsets[]{ 0 };
        for (auto* tex : m_downsampleTextures)
        {
            uint32_t groupX = (tex->GetWidth() + 7) / 8;
            uint32_t groupY = (tex->GetHeight() + 7) / 8;

            DirectX11::CSSetShader(m_pAutoExposureEvalCS->GetShader(), 0, 0);
            DirectX11::CSSetShaderResources(0, 1, &currentSRV);
            DirectX11::CSSetUnorderedAccessViews(0, 1, &tex->m_pUAV, offsets);
            DirectX11::Dispatch(groupX, groupY, 1);
            ID3D11ShaderResourceView* nullSRV = nullptr;
            ID3D11UnorderedAccessView* nullUAV = nullptr;
            DirectX11::CSSetShaderResources(0, 1, &nullSRV);
            DirectX11::CSSetUnorderedAccessViews(0, 1, &nullUAV, offsets);
            currentSRV = tex->m_pSRV;
        }

        ID3D11Resource* lastResource = m_downsampleTextures.back()->m_pTexture;
        DeviceState::g_pDeviceContext->CopyResource(readbackTexture.Get(), lastResource);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(DeviceState::g_pDeviceContext->Map(readbackTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            float luminance = *reinterpret_cast<float*>(mapped.pData);
            DeviceState::g_pDeviceContext->Unmap(readbackTexture.Get(), 0);
            m_toneMapACESConstant.toneMapExposure = 0.5f / (luminance + 1e-4f);
        }
    }

	if (m_toneMapType == ToneMapType::Reinhard)
	{
        m_pso->m_pixelShader = &ShaderSystem->PixelShaders["ToneMapReinhard"];
		m_toneMapReinhardConstant.m_bUseToneMap = m_isAbleToneMap;
		DirectX11::UpdateBuffer(m_pReinhardConstantBuffer, &m_toneMapReinhardConstant);
		DirectX11::PSSetConstantBuffer(0, 1, &m_pReinhardConstantBuffer);
	}
	else if (m_toneMapType == ToneMapType::ACES)
	{
        m_pso->m_pixelShader = &ShaderSystem->PixelShaders["ToneMapACES"];
		m_toneMapACESConstant.m_bUseToneMap = m_isAbleToneMap;
		m_toneMapACESConstant.m_bUseFilmic = m_isAbleFilmic;
		DirectX11::UpdateBuffer(m_pACESConstantBuffer, &m_toneMapACESConstant);
		DirectX11::PSSetConstantBuffer(0, 1, &m_pACESConstantBuffer);

	}

    m_pso->Apply();

    ID3D11RenderTargetView* renderTargets[] = { m_DestTexture->GetRTV() };
    DirectX11::OMSetRenderTargets(1, renderTargets, nullptr);

	//m_pToneMapPostProcess->SetHDRSourceTexture(renderData->m_renderTarget->m_pSRV);
 //   m_pToneMapPostProcess->Process(DeviceState::g_pDeviceContext);

    DirectX11::PSSetShaderResources(0, 1, &renderData->m_renderTarget->m_pSRV);
    DirectX11::Draw(4, 0);

    DirectX11::CopyResource(renderData->m_renderTarget->m_pTexture, m_DestTexture->m_pTexture);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    DirectX11::PSSetShaderResources(0, 1, &nullSRV);
    DirectX11::UnbindRenderTargets();
}

void ToneMapPass::ControlPanel()
{
    ImGui::PushID(this);
    ImGui::Checkbox("Use ToneMap", &m_isAbleToneMap);
    ImGui::SetNextWindowFocus();
    ImGui::Combo("ToneMap Type", (int*)&m_toneMapType, "Reinhard\0ACES\0");
    ImGui::Separator();
	if (m_toneMapType == ToneMapType::ACES)
	{
		ImGui::Checkbox("Use uncharted2_tonemap", &m_isAbleFilmic);
        ImGui::DragFloat("ToneMap Exposure", &m_toneMapACESConstant.toneMapExposure, 0.01f, 0.0f, 5.0f);
	}
    if (m_toneMapType == ToneMapType::ACES && m_toneMapACESConstant.m_bUseFilmic)
    {
		//ImGui::DragFloat("ToneMap Exposure", &m_toneMapACESConstant.toneMapExposure, 0.01f, 0.0f, 5.0f);
    }
    ImGui::PopID();
}

void ToneMapPass::PrepareDownsampleTextures(uint32_t width, uint32_t height)
{
    for (auto* tex : m_downsampleTextures)
    {
        DeallocateResource(tex);
    }
    m_downsampleTextures.clear();

    uint32_t ratio = 2;
    while (width / ratio > 1 && height / ratio > 1)
    {
        std::string name = "AutoExposureDS" + std::to_string(ratio);
        auto* tex = Texture::Create(ratio, ratio, width, height, name, DXGI_FORMAT_R32_FLOAT,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        tex->CreateSRV(DXGI_FORMAT_R32_FLOAT);
        tex->CreateUAV(DXGI_FORMAT_R32_FLOAT);
        m_downsampleTextures.push_back(tex);
        ratio *= 2;
    }
    // ensure final 1x1 texture exists
    if (m_downsampleTextures.empty() ||
        (m_downsampleTextures.back()->GetWidth() != 1 || m_downsampleTextures.back()->GetHeight() != 1))
    {
        auto* tex = Texture::Create(1, 1, "AutoExposureDS1", DXGI_FORMAT_R32_FLOAT,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        tex->CreateSRV(DXGI_FORMAT_R32_FLOAT);
        tex->CreateUAV(DXGI_FORMAT_R32_FLOAT);
        m_downsampleTextures.push_back(tex);
    }
}

void ToneMapPass::Resize(uint32_t width, uint32_t height)
{
}
