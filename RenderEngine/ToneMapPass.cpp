#include "ToneMapPass.h"
#include "ShaderSystem.h"
#include "ImGuiRegister.h"
#include "DeviceState.h"
#include "Camera.h"
#include "TimeSystem.h"

ToneMapPass::ToneMapPass()
{
    m_pso = std::make_unique<PipelineStateObject>();

    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["ToneMapACES"];
	m_pAutoExposureHistogramCS = &ShaderSystem->ComputeShaders["AutoExposureHistogram"];
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

    D3D11_BUFFER_DESC cbDesc0 = {};
    cbDesc0.ByteWidth = sizeof(LuminanceHistogramData);
    cbDesc0.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc0.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc0.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    device->CreateBuffer(&cbDesc0, nullptr, &m_pAutoExposureConstantBuffer);

    D3D11_BUFFER_DESC bufDesc = {};
    bufDesc.ByteWidth = sizeof(UINT) * NUM_BINS;
    bufDesc.Usage = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufDesc.StructureByteStride = sizeof(UINT);

    device->CreateBuffer(&bufDesc, nullptr, &m_pHistogramBuffer);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc0 = {};
    uavDesc0.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc0.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc0.Buffer.NumElements = 256;

    device->CreateUnorderedAccessView(m_pHistogramBuffer, &uavDesc0, &m_exposureUAV);

    D3D11_BUFFER_DESC cbDesc1 = {};
    cbDesc1.ByteWidth = sizeof(LuminanceAverageData);
    cbDesc1.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc1.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc1.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    device->CreateBuffer(&cbDesc1, nullptr, &m_pLuminanceAverageBuffer);

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    device->CreateTexture2D(&texDesc, nullptr, &luminanceTexture);

    // UAV 생성
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc1 = {};
    uavDesc1.Format = texDesc.Format;
    uavDesc1.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

    device->CreateUnorderedAccessView(luminanceTexture.Get(), &uavDesc1, &luminanceUAV);

    D3D11_TEXTURE2D_DESC readbackDesc = texDesc;
    readbackDesc.Usage = D3D11_USAGE_STAGING;
    readbackDesc.BindFlags = 0;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    device->CreateTexture2D(&readbackDesc, nullptr, &readbackTexture);
}

ToneMapPass::~ToneMapPass()
{
	Memory::SafeDelete(m_pACESConstantBuffer);
    Memory::SafeDelete(m_pReinhardConstantBuffer);

	Memory::SafeDelete(m_pHistogramBuffer);
	Memory::SafeDelete(m_pAutoExposureConstantBuffer);
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

    DirectX11::PSSetShaderResources(0, 1, &renderData->m_renderTarget->m_pSRV);
    DirectX11::Draw(4, 0);

    DirectX11::CopyResource(renderData->m_renderTarget->m_pTexture, m_DestTexture->m_pTexture);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    DirectX11::PSSetShaderResources(0, 1, &nullSRV);
    DirectX11::UnbindRenderTargets();
}

void ToneMapPass::ControlPanel()
{
    ImGui::Checkbox("Use ToneMap", &m_isAbleToneMap);
    ImGui::SetNextWindowFocus();
    ImGui::Combo("ToneMap Type", (int*)&m_toneMapType, "Reinhard\0ACES\0");
    ImGui::Separator();
	if (m_toneMapType == ToneMapType::ACES)
	{
		ImGui::Checkbox("Use Filmic", &m_isAbleFilmic);
        ImGui::DragFloat("ToneMap Exposure", &m_toneMapACESConstant.toneMapExposure, 0.01f, 0.0f, 5.0f);
	}
    if (m_toneMapType == ToneMapType::ACES && m_toneMapACESConstant.m_bUseFilmic)
    {
		//ImGui::DragFloat("ToneMap Exposure", &m_toneMapACESConstant.toneMapExposure, 0.01f, 0.0f, 5.0f);
    }
}

void ToneMapPass::Resize(uint32_t width, uint32_t height)
{
}
