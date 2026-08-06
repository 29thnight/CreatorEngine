#include "BitMaskPass.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "BitMaskPassSetting.h"

cbuffer EdgefilterBuffer
{
	float4 m_color[8];
	float2 m_screenSize;
    float  m_outlineVelocity;
};

cbuffer UownUpSamplingParams{
    float2 inputTextureSize;
    int ratio;
};

BitMaskPass::BitMaskPass()
{
	m_pEdgefilterShader = &ShaderSystem->ComputeShaders["Edgefilter"];
	m_pEdgefilterDownSamplingShader = &ShaderSystem->ComputeShaders["DownDualFiltering"];
	m_pEdgefilterUpSamplingShader = &ShaderSystem->ComputeShaders["UpDualFiltering"];
	m_pAddColorShader = &ShaderSystem->ComputeShaders["AddTextureColor"];

	m_EdgefilterBuffer = DirectX11::CreateBuffer(sizeof(EdgefilterBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    m_EdgefilterSamplingBuffer = DirectX11::CreateBuffer(sizeof(UownUpSamplingParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);

    sample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
    pointSample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pTempTexture = Texture::CreateScreenSized(
        "BitmaskDownTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS,
        2, 2
    );
    m_pTempTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

    m_pTempTexture2 = Texture::CreateScreenSized(
        "BitmaskUpTexture2",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture2->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture2->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
}

BitMaskPass::~BitMaskPass()
{
	delete sample;
    delete pointSample;
}

void BitMaskPass::Initialize(Texture* bitmask)
{
	m_pBitmaskTexture = bitmask;
}

void BitMaskPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void BitMaskPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	if (!isOn) return; // If the pass is not enabled, skip execution

    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    // RHI 이식(PHASE 3-1, 3차 슬라이스). 순수 컴퓨트 패스라 새로 넓힌 컴퓨트
    // 표면(셰이더·샘플러·SRV/UAV·상수 버퍼·Dispatch)의 전체를 이 패스가 지나간다.
    ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

    const RHIViewport fullVp = RHI::Device().GetFullViewport();
    const uint32_t viewWidth  = static_cast<uint32_t>(fullVp.width);
    const uint32_t viewHeight = static_cast<uint32_t>(fullVp.height);

    // edge filter
    context.SetComputeShader(m_pEdgefilterShader->GetShader());
    RHINativeSamplerState linearSampler = sample->m_SamplerState;
    RHINativeSamplerState pointSampler = pointSample->m_SamplerState;
    context.SetComputeSamplers(0, 1, &linearSampler);
    context.SetComputeSamplers(1, 1, &pointSampler);

    RHINativeShaderResource srv = m_pBitmaskTexture->m_pSRV;
    context.SetComputeShaderResources(0, 1, &srv);
    RHINativeUnorderedAccess uav = m_pTempTexture2->m_pUAV;
    context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);

	EdgefilterBuffer efparams{};
	for (int i = 0; i < 8; ++i)
	{
		efparams.m_color[i] = m_colors[i];
	}
	efparams.m_screenSize = { (float)viewWidth, (float)viewHeight };
    efparams.m_outlineVelocity = outlineVelocity;
    context.UpdateBuffer(m_EdgefilterBuffer.Get(), &efparams);
    RHINativeBuffer edgeBuffer = m_EdgefilterBuffer.Get();
    context.SetComputeConstantBuffers(0, 1, &edgeBuffer);
    context.Dispatch(viewWidth / 16, viewHeight / 16, 1);

    RHINativeShaderResource nullsrv = nullptr;
    RHINativeUnorderedAccess nulluav = nullptr;
    context.SetComputeShaderResources(0, 1, &nullsrv);
    context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);

    if (blurOutline) {
        // Down Dual Filtering
        context.SetComputeShader(m_pEdgefilterDownSamplingShader->GetShader());
        srv = m_pTempTexture2->m_pSRV;
        context.SetComputeShaderResources(0, 1, &srv);
        uav = m_pTempTexture->m_pUAV;
        context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);

        UownUpSamplingParams DPparams;
        DPparams.inputTextureSize = { (float)viewWidth, (float)viewHeight };
        DPparams.ratio = 2;
        context.UpdateBuffer(m_EdgefilterSamplingBuffer.Get(), &DPparams);
        RHINativeBuffer samplingBuffer = m_EdgefilterSamplingBuffer.Get();
        context.SetComputeConstantBuffers(0, 1, &samplingBuffer);

        context.Dispatch((viewWidth + 32 - 1) / 32, (viewHeight + 32 - 1) / 32, 1);
        context.SetComputeShaderResources(0, 1, &nullsrv);
        context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);

        // Up Dual Filtering
        context.SetComputeShader(m_pEdgefilterUpSamplingShader->GetShader());
        srv = m_pTempTexture->m_pSRV;
        context.SetComputeShaderResources(0, 1, &srv);
        uav = m_pTempTexture2->m_pUAV;
        context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);
        DPparams.inputTextureSize = { (float)viewWidth / 2, (float)viewHeight / 2 };
        context.UpdateBuffer(m_EdgefilterSamplingBuffer.Get(), &DPparams);
        context.SetComputeConstantBuffers(0, 1, &samplingBuffer);
        context.Dispatch((viewWidth + 16 - 1) / 16, (viewHeight + 16 - 1) / 16, 1);
        context.SetComputeShaderResources(0, 1, &nullsrv);
        context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);
    }

	//Add Color
    context.SetComputeShader(m_pAddColorShader->GetShader());
    srv = m_pTempTexture2->m_pSRV;
    context.SetComputeShaderResources(0, 1, &srv);
    uav = renderData->m_renderTarget->m_pUAV;
    context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);
    context.Dispatch((viewWidth + 16 - 1) / 16, (viewHeight + 16 - 1) / 16, 1);
    context.SetComputeShaderResources(0, 1, &nullsrv);
    context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);

    ID3D11CommandList* commandList{};
    deferredPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void BitMaskPass::ControlPanel()
{
    ImGui::PushID(this);
    auto& setting = EngineSettingInstance->GetRenderPassSettingsRW().bitMask;
	ImGui::Text("BitMask Pass");
    if (ImGui::Checkbox("Enable Outline", &isOn)) {
        setting.isOn = isOn;
    }
    if (ImGui::Checkbox("Enable Outline blur", &blurOutline)) {
        setting.blurOutline = blurOutline;
    }
    if (ImGui::DragFloat("Outline Velocity", &outlineVelocity)) {
        setting.outlineVelocity = outlineVelocity;
    }
    for (int i = 0; i < 8; i++) {
        ImGui::PushID(i);
        if (ImGui::ColorEdit4(("Color" + std::to_string(i)).c_str(), &m_colors[i].x)) {
            switch (i) {
            case 0:
                setting.m_color1 = m_colors[i];
                break;
            case 1:
                setting.m_color2 = m_colors[i];
                break;
            case 2:
                setting.m_color3 = m_colors[i];
                break;
            case 3:
                setting.m_color4 = m_colors[i];
                break;
            case 4:
                setting.m_color5 = m_colors[i];
                break;
            case 5:
                setting.m_color6 = m_colors[i];
                break;
            case 6:
                setting.m_color7 = m_colors[i];
                break;
            case 7:
                setting.m_color8 = m_colors[i];
                break;
            }
        }
        if (ImGui::DragFloat("ColorVelocity", &m_colors[i].w, 1.f)) {
            switch (i) {
            case 0:
                setting.m_color1.w = m_colors[i].w;
                break;
            case 1:
                setting.m_color2.w = m_colors[i].w;
                break;
            case 2:
                setting.m_color3.w = m_colors[i].w;
                break;
            case 3:
                setting.m_color4.w = m_colors[i].w;
                break;
            case 4:
                setting.m_color5.w = m_colors[i].w;
                break;
            case 5:
                setting.m_color6.w = m_colors[i].w;
                break;
            case 6:
                setting.m_color7.w = m_colors[i].w;
                break;
            case 7:
                setting.m_color8.w = m_colors[i].w;
                break;
            }
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

void BitMaskPass::Resize(uint32_t width, uint32_t height)
{
}

void BitMaskPass::ApplySettings(const BitMaskPassSetting& settings)
{
    isOn = settings.isOn;
    blurOutline = settings.blurOutline;
    outlineVelocity = settings.outlineVelocity;
    m_colors[0] = settings.m_color1;
    m_colors[1] = settings.m_color2;
    m_colors[2] = settings.m_color3;
    m_colors[3] = settings.m_color4;
    m_colors[4] = settings.m_color5;
    m_colors[5] = settings.m_color6;
    m_colors[6] = settings.m_color7;
    m_colors[7] = settings.m_color8;
}
