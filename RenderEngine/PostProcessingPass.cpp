#include "PostProcessingPass.h"
#include "ShaderSystem.h"
#include "BloomSetting.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"

PostProcessingPass::PostProcessingPass()
{
    m_pso = std::make_unique<PipelineStateObject>();

    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
    auto pointSampler  = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    auto linearClampSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(pointSampler);
    m_pso->m_samplers.push_back(linearClampSampler);
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
    m_pBloomCompositePS = &ShaderSystem->PixelShaders["BloomComposite"];
    m_pBloomExtractCS   = &ShaderSystem->ComputeShaders["BloomExtract"];
    m_pBloomDownSampleCS = &ShaderSystem->ComputeShaders["BloomDownSample"];
    m_pBloomUpSampleCS  = &ShaderSystem->ComputeShaders["BloomUpSample"];

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
            &m_pso->m_rasterizerState));

    TextureInitialization();

    m_bloomParamBuffer      = DirectX11::CreateBuffer(sizeof(BloomParams), D3D11_BIND_CONSTANT_BUFFER, &m_bloomParams);
    m_downSampleBuffer      = DirectX11::CreateBuffer(sizeof(BloomDownSampleParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    m_upSampleBuffer        = DirectX11::CreateBuffer(sizeof(BloomUpSampleParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    m_bloomCompositeBuffer  = DirectX11::CreateBuffer(sizeof(BloomCompositeParams), D3D11_BIND_CONSTANT_BUFFER, &m_bloomComposite);
}

PostProcessingPass::~PostProcessingPass()
{
    delete m_CopiedTexture;
    delete m_bloomExtractTexture;
    for (auto* tex : m_bloomDownSampledTextures)
    {
        delete tex;
    }
    for (auto* tex : m_bloomUpSampledTextures)
    {
        delete tex;
    }
    delete m_BloomResult;
}

void PostProcessingPass::Execute(RenderScene& scene, Camera& camera)
{
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);
    PrepaerShaderState();

    DirectX11::CopyResource(m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);

    if (m_PostProcessingApply.m_Bloom)
    {
        BloomPass(scene, camera);
    }

    DirectX11::CopyResource(renderData->m_renderTarget->m_pTexture, m_CopiedTexture->m_pTexture);
}

void PostProcessingPass::ControlPanel()
{
    ImGui::PushID(this);
    auto& setting = EngineSettingInstance->GetRenderPassSettingsRW().bloom;

    if (ImGui::Checkbox("ApplyBloom", &m_PostProcessingApply.m_Bloom))
    {
        setting.applyBloom = m_PostProcessingApply.m_Bloom;
    }
    if (ImGui::DragFloat("Threshold", &m_bloomParams.threshHold, 0.1f, 0.f, 50.f))
    {
        setting.threshold = m_bloomParams.threshHold;
    }
    if (ImGui::DragFloat("Radius", &m_bloomParams.radius, 0.1f, 0.f, 10.f))
    {
        setting.blurSigma = m_bloomParams.radius;
    }
    if (ImGui::DragFloat("Coefficient", &m_bloomComposite.coefficient, 0.01f, 0.f, 2.f))
    {
        setting.coefficient = m_bloomComposite.coefficient;
    }
    if (ImGui::Button("Reset"))
    {
        m_bloomParams.threshHold = 5.f;
        m_bloomParams.radius = 2.f;
        m_bloomComposite.coefficient = 0.05f;
        setting.applyBloom = true;
        setting.threshold = m_bloomParams.threshHold;
        setting.blurSigma = m_bloomParams.radius;
        setting.coefficient = m_bloomComposite.coefficient;
    }
    ImGui::PopID();
}

void PostProcessingPass::ApplySettings(const BloomPassSetting& setting)
{
    m_PostProcessingApply.m_Bloom = setting.applyBloom;
    m_bloomParams.threshHold = setting.threshold;
    m_bloomParams.radius = setting.blurSigma;
    m_bloomComposite.coefficient = setting.coefficient;
}

void PostProcessingPass::PrepaerShaderState()
{
    m_pBloomCompositePS = &ShaderSystem->PixelShaders["BloomComposite"];
    m_pBloomExtractCS   = &ShaderSystem->ComputeShaders["BloomExtract"];
    m_pBloomDownSampleCS = &ShaderSystem->ComputeShaders["BloomDownSample"];
    m_pBloomUpSampleCS  = &ShaderSystem->ComputeShaders["BloomUpSample"];
}

void PostProcessingPass::TextureInitialization()
{
    m_CopiedTexture = Texture::Create(
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "CopiedTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    m_CopiedTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_CopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_CopiedTexture->m_textureType = TextureType::ImageTexture;

    m_bloomExtractTexture = Texture::Create(
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "BloomExtract",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
    m_bloomExtractTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_bloomExtractTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_bloomExtractTexture->m_textureType = TextureType::ImageTexture;

    for (uint32_t i = 0; i < BLOOM_MIP_LEVELS; ++i)
    {
        uint32_t ratio = 1u << (i + 1);
        std::string downName = "BloomDownSample" + std::to_string(i);
        m_bloomDownSampledTextures[i] = Texture::Create(
            ratio, ratio,
            DeviceState::g_ClientRect.width,
            DeviceState::g_ClientRect.height,
            downName,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        m_bloomDownSampledTextures[i]->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
        m_bloomDownSampledTextures[i]->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
        m_bloomDownSampledTextures[i]->m_textureType = TextureType::ImageTexture;

        std::string upName = "BloomUpSample" + std::to_string(i);
        m_bloomUpSampledTextures[i] = Texture::Create(
            ratio, ratio,
            DeviceState::g_ClientRect.width,
            DeviceState::g_ClientRect.height,
            upName,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        m_bloomUpSampledTextures[i]->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
        m_bloomUpSampledTextures[i]->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
        m_bloomUpSampledTextures[i]->m_textureType = TextureType::ImageTexture;
    }

    m_BloomResult = Texture::Create(
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "BloomResult",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    m_BloomResult->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_BloomResult->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_BloomResult->m_textureType = TextureType::ImageTexture;
}

void PostProcessingPass::BloomPass(RenderScene& /*scene*/, Camera& /*camera*/)
{
    const UINT offsets[]{ 0 };
    constexpr ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    constexpr ID3D11ShaderResourceView* nullSRV = nullptr;
    constexpr ID3D11UnorderedAccessView* nullUAV = nullptr;

    ID3D11SamplerState* samplers[] = {
        m_pso->m_samplers[0]->m_SamplerState,
		m_pso->m_samplers[1]->m_SamplerState,
		m_pso->m_samplers[2]->m_SamplerState
	};

	DirectX11::CSSetSamplers(0, 3, samplers);

    // Extract bright areas
    DirectX11::UpdateBuffer(m_bloomParamBuffer.Get(), &m_bloomParams);
    DirectX11::CSSetShader(m_pBloomExtractCS->GetShader(), 0, 0);
    ID3D11ShaderResourceView* extractSRVs[2] = { m_CopiedTexture->m_pSRV, m_CopiedTexture->m_pSRV };
    DirectX11::CSSetShaderResources(0, 2, extractSRVs);
    DirectX11::CSSetUnorderedAccessViews(0, 1, &m_bloomExtractTexture->m_pUAV, offsets);
    DirectX11::CSSetConstantBuffer(0, 1, m_bloomParamBuffer.GetAddressOf());
    UINT gx = (DeviceState::g_ClientRect.width + 11) / 12;
    UINT gy = (DeviceState::g_ClientRect.height + 7) / 8;
    DirectX11::Dispatch(gx, gy, 1);
    DirectX11::CSSetShaderResources(0, 2, nullSRVs);
    DirectX11::CSSetUnorderedAccessViews(0, 1, &nullUAV, offsets);

    // Downsample chain
    ID3D11ShaderResourceView* currentSRV = m_bloomExtractTexture->m_pSRV;
    for (uint32_t i = 0; i < BLOOM_MIP_LEVELS; ++i)
    {
        uint32_t inputWidth = (uint32)DeviceState::g_ClientRect.width >> i;
        uint32_t inputHeight = (uint32)DeviceState::g_ClientRect.height >> i;
        uint32_t width = inputWidth >> 1;
        uint32_t height = inputHeight >> 1;
        if (width == 0 || height == 0) break;

        m_downSampleParams.texelSize = { 1.f / static_cast<float>(inputWidth), 1.f / static_cast<float>(inputHeight) };
        m_downSampleParams.inputTextureMipLevel = 0;
        m_downSampleParams.bloomPassIndex = i;
        DirectX11::UpdateBuffer(m_downSampleBuffer.Get(), &m_downSampleParams);

        DirectX11::CSSetShader(m_pBloomDownSampleCS->GetShader(), 0, 0);
        DirectX11::CSSetShaderResources(0, 1, &currentSRV);
        DirectX11::CSSetUnorderedAccessViews(0, 1, &m_bloomDownSampledTextures[i]->m_pUAV, offsets);
        DirectX11::CSSetConstantBuffer(0, 1, m_downSampleBuffer.GetAddressOf());

        UINT dgx = (width + 11) / 12;
        UINT dgy = (height + 7) / 8;
        DirectX11::Dispatch(dgx, dgy, 1);

        DirectX11::CSSetShaderResources(0, 1, &nullSRV);
        DirectX11::CSSetUnorderedAccessViews(0, 1, &nullUAV, offsets);

        currentSRV = m_bloomDownSampledTextures[i]->m_pSRV;
    }

    // Upsample chain
    for (int i = static_cast<int>(BLOOM_MIP_LEVELS) - 1; i >= 0; --i)
    {
        m_upSampleParams.radius = m_bloomParams.radius;
        m_upSampleParams.bloomPassIndex = static_cast<uint32_t>(i);
        m_upSampleParams.inputPreviousUpSampleMipLevel = 0;
        m_upSampleParams.maxBloomPasses = BLOOM_MIP_LEVELS;
        DirectX11::UpdateBuffer(m_upSampleBuffer.Get(), &m_upSampleParams);

        ID3D11ShaderResourceView* upSRVs[2] = {
            (i == static_cast<int>(BLOOM_MIP_LEVELS) - 1) ? m_bloomDownSampledTextures[static_cast<size_t>(i)]->m_pSRV : m_bloomUpSampledTextures[static_cast<size_t>(i + 1)]->m_pSRV,
            m_bloomDownSampledTextures[static_cast<size_t>(i)]->m_pSRV };

        DirectX11::CSSetShader(m_pBloomUpSampleCS->GetShader(), 0, 0);
        DirectX11::CSSetShaderResources(0, 2, upSRVs);
        DirectX11::CSSetUnorderedAccessViews(0, 1, &m_bloomUpSampledTextures[static_cast<size_t>(i)]->m_pUAV, offsets);
        DirectX11::CSSetConstantBuffer(0, 1, m_upSampleBuffer.GetAddressOf());

        uint32_t width = (uint32)DeviceState::g_ClientRect.width >> (i + 1);
        uint32_t height = (uint32)DeviceState::g_ClientRect.height >> (i + 1);
        UINT ugx = (width + 11) / 12;
        UINT ugy = (height + 7) / 8;
        DirectX11::Dispatch(ugx, ugy, 1);

        DirectX11::CSSetShaderResources(0, 2, nullSRVs);
        DirectX11::CSSetUnorderedAccessViews(0, 1, &nullUAV, offsets);
    }

    // Final composite
    m_pso->m_pixelShader = m_pBloomCompositePS;
    m_pso->Apply();

    ID3D11RenderTargetView* rtv = m_BloomResult->GetRTV();
    DirectX11::OMSetRenderTargets(1, &rtv, nullptr);

    ID3D11ShaderResourceView* pSRVs[]{ m_CopiedTexture->m_pSRV, m_bloomUpSampledTextures[0]->m_pSRV };
    DirectX11::PSSetShaderResources(0, 2, pSRVs);
    DirectX11::UpdateBuffer(m_bloomCompositeBuffer.Get(), &m_bloomComposite);
    DirectX11::PSSetConstantBuffer(0, 1, m_bloomCompositeBuffer.GetAddressOf());
    DirectX11::Draw(4, 0);

    DirectX11::PSSetShaderResources(0, 2, nullSRVs);
    DirectX11::UnbindRenderTargets();

    DirectX11::CopyResource(m_CopiedTexture->m_pTexture, m_BloomResult->m_pTexture);
}

void PostProcessingPass::Resize(uint32_t /*width*/, uint32_t /*height*/)
{
}
