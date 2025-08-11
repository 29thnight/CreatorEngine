#include "ToneMapPass.h"
#include "ShaderSystem.h"
#include "ToneMapPassSetting.h"
#include "ImGuiRegister.h"
#include "DeviceState.h"
#include "Camera.h"
#include "TimeSystem.h"

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

    m_pToneMapConstantBuffer = DirectX11::CreateBuffer(
        sizeof(ToneMapConstant),
        D3D11_BIND_CONSTANT_BUFFER, 
        &m_toneMapConstant
    );

	DirectX::SetName(m_pToneMapConstantBuffer, "ToneMapConstantBuffer");

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

    for(int i = 0; i < 2; ++i)
    {
        device->CreateTexture2D(&readbackDesc, nullptr, &m_readbackTexture[i]);
    }

    PrepareDownsampleTextures(DeviceState::g_ClientRect.width, DeviceState::g_ClientRect.height);
}

ToneMapPass::~ToneMapPass()
{
	Memory::SafeDelete(m_pToneMapConstantBuffer);

    for (auto* tex : m_downsampleTextures)
    {
        delete tex;
    }
    m_downsampleTextures.clear();
}

void ToneMapPass::Initialize(Managed::SharedPtr<Texture> dest)
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

    float deltaTime = Time->GetElapsedSeconds();
	static float targetExposure = 1.0f;
	static float currentExposure = 1.0f;

	if (!camera.m_avoidRenderPass.Test((flag)RenderPipelinePass::AutoExposurePass))
    {
        DirectX11::CSSetShader(m_pAutoExposureEvalCS->GetShader(), 0, 0);

        if (m_isAbleAutoExposure && !m_downsampleTextures.empty())
        {
            ID3D11ShaderResourceView* currentSRV = renderData->m_renderTarget->m_pSRV;
            const UINT offsets[]{ 0 };
            for (auto* tex : m_downsampleTextures)
            {
                uint32_t groupX = (tex->GetWidth() + 31) / 32;
                uint32_t groupY = (tex->GetHeight() + 31) / 32;

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
            DeviceState::g_pDeviceContext->CopyResource(m_readbackTexture[m_writeIndex].Get(), lastResource);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(DeviceState::g_pDeviceContext->Map(
                m_readbackTexture[m_readIndex].Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                const float lumEpsilon = 0.05f;
                float luminance = *reinterpret_cast<float*>(mapped.pData);
                DeviceState::g_pDeviceContext->Unmap(m_readbackTexture[m_readIndex].Get(), 0);

                float EV100 = log2((m_fNumber * m_fNumber) / m_shutterTime * (100.0f / m_ISO));
                float exposureManual = 1.0f / pow(2.0f, EV100 + m_exposureCompensation);
                float targetLuminance = 0.5f; // 중간 회색 기준값 (원하는 값으로 조정 가능)
                float exposureAuto = targetLuminance / (luminance + 1e-4f);

                float exposureFinal = exposureManual * exposureAuto;

                if (fabs(exposureFinal - targetExposure) > lumEpsilon)
                {
                    targetExposure = exposureFinal;
                }
            }

            std::swap(m_readIndex, m_writeIndex);
        }
        else
        {
            targetExposure = m_toneMapConstant.toneMapExposure;
        }
    }

    if (m_isAbleAutoExposure)
    {
        constexpr float epsilon = 0.01f; // Small value to avoid oscillation
        float diff = fabs(targetExposure - m_toneMapConstant.toneMapExposure);

        if (diff > epsilon)
        {
            float speed = (targetExposure > m_toneMapConstant.toneMapExposure) ? m_speedBrightness : m_speedDarkness;
            float t = std::clamp(speed * deltaTime, 0.0f, 1.0f);
            currentExposure = Mathf::Lerp(m_toneMapConstant.toneMapExposure, targetExposure, t);
        }
        else
        {
            currentExposure = targetExposure; // Snap to target if close enough
        }
    }
    else
    {
        currentExposure = m_toneMapConstant.toneMapExposure;
	}

    m_toneMapConstant.toneMapExposure = currentExposure;
	m_toneMapConstant.toneMapExposure = std::max(m_toneMapConstant.toneMapExposure, 0.01f); // Ensure exposure is not zero
    m_toneMapConstant.operatorType = (int)m_toneMapType;

    DirectX11::UpdateBuffer(m_pToneMapConstantBuffer, &m_toneMapConstant);
    DirectX11::PSSetConstantBuffer(0, 1, &m_pToneMapConstantBuffer);

    m_pso->Apply();

    ID3D11RenderTargetView* renderTargets[] = { m_DestTexture.lock()->GetRTV() };
    DirectX11::OMSetRenderTargets(1, renderTargets, nullptr);

    DirectX11::PSSetShaderResources(0, 1, &renderData->m_renderTarget->m_pSRV);
    DirectX11::Draw(4, 0);

    DirectX11::CopyResource(renderData->m_renderTarget->m_pTexture, m_DestTexture.lock()->m_pTexture);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    DirectX11::PSSetShaderResources(0, 1, &nullSRV);
    DirectX11::UnbindRenderTargets();
}

void ToneMapPass::ControlPanel()
{
    ImGui::PushID(this);
    auto& setting = EngineSettingInstance->GetRenderPassSettingsRW().toneMap;
    if (ImGui::Checkbox("Use ToneMap", &m_isAbleToneMap))
    {
        setting.isAbleToneMap = m_isAbleToneMap;
    }
    ImGui::SetNextWindowFocus();
    if (ImGui::Combo("ToneMap Type", (int*)&m_toneMapType, "Reinhard\0ACES\0Uncharted2\0HDR10\0ACESFlim"))
    {
        setting.toneMapType = (int)m_toneMapType;
    }
    ImGui::Separator();
    ImGui::Text("Auto Exposure Settings");
    if (ImGui::Checkbox("Use Auto Exposure", &m_isAbleAutoExposure))
    {
        setting.isAbleAutoExposure = m_isAbleAutoExposure;
    }
    if (ImGui::DragFloat("ToneMap Exposure", &m_toneMapConstant.toneMapExposure, 0.01f, 0.0f, 5.0f, "%.3f", m_isAbleAutoExposure? ImGuiSliderFlags_NoInput : ImGuiSliderFlags_None))
    {
        setting.toneMapExposure = m_toneMapConstant.toneMapExposure;
    }
    ImGui::Separator();
	ImGui::Text("Auto Exposure Settings");
    if (ImGui::DragFloat("fNumber", &m_fNumber, 0.01f, 1.0f, 32.0f))
            setting.fNumber = m_fNumber;
    if (ImGui::DragFloat("Shutter Time", &m_shutterTime, 0.001f, 0.000125f, 30.0f))
            setting.shutterTime = m_shutterTime;
    if (ImGui::DragFloat("ISO", &m_ISO, 50.0f, 50.0f, 6400.0f))
            setting.ISO = m_ISO;
    if (ImGui::DragFloat("Exposure Compensation", &m_exposureCompensation, 0.01f, -5.0f, 5.0f))
            setting.exposureCompensation = m_exposureCompensation;
    if (ImGui::DragFloat("Speed Brightness", &m_speedBrightness, 0.01f, 0.1f, 10.0f))
            setting.speedBrightness = m_speedBrightness;
    if (ImGui::DragFloat("Speed Darkness", &m_speedDarkness, 0.01f, 0.1f, 10.0f))
            setting.speedDarkness = m_speedDarkness;
    ImGui::PopID();
}

void ToneMapPass::PrepareDownsampleTextures(uint32_t width, uint32_t height)
{
    for (auto* tex : m_downsampleTextures)
    {
        delete tex;
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

void ToneMapPass::ApplySettings(const ToneMapPassSetting& setting)
{
    m_isAbleAutoExposure                = setting.isAbleAutoExposure;
    m_isAbleToneMap                     = setting.isAbleToneMap;
    m_fNumber                           = setting.fNumber;
    m_shutterTime                       = setting.shutterTime;
    m_ISO                               = setting.ISO;
    m_exposureCompensation              = setting.exposureCompensation;
    m_speedBrightness                   = setting.speedBrightness;
    m_speedDarkness                     = setting.speedDarkness;
    m_toneMapType                       = static_cast<ToneMapType>(setting.toneMapType);
    m_toneMapConstant.filmSlope         = setting.filmSlope;
    m_toneMapConstant.filmToe           = setting.filmToe;
    m_toneMapConstant.filmShoulder      = setting.filmShoulder;
    m_toneMapConstant.filmBlackClip     = setting.filmBlackClip;
    m_toneMapConstant.filmWhiteClip     = setting.filmWhiteClip;
    m_toneMapConstant.toneMapExposure   = setting.toneMapExposure;
}
