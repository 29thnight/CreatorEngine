#include "SSGIPass.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "../EngineEntry/RenderPassSettings.h"
#include "Scene.h"

//#define SSGI_Ratio 1
//#define SSGI_NumThreads SSGI_Ratio * 16
int ssratio = 4;
int ssthreads = 16;

cbuffer SSGIParams
{
    XMMATRIX view;
    XMMATRIX proj;
    XMMATRIX inverseView;
    XMMATRIX inverseProjection;
    float2 screenSize; // 화면 크기
    float radius; // 샘플링 반경
    float thickness; // 두께
    UINT frameIndex;
    int ratio;
    float intensity;
};

cbuffer CompositeParams{
    float2 inputTextureSize;
    int ratio;
	bool32 useOnlySSGI;
};

cbuffer BilateralParams{
    float2 screenSize;
    float sigmaSpace;
    float sigmaRange;
};

SSGIPass::SSGIPass()
{
    m_pSSGIShader = &ShaderSystem->ComputeShaders["SSGI"];
    m_pCompositeShader = &ShaderSystem->ComputeShaders["SSGIComposite"];
	//m_pBilateralFilterShader = &ShaderSystem->ComputeShaders["BiliteralFilter"];

	m_pDownDualFilteringShader = &ShaderSystem->ComputeShaders["DownDualFiltering"];
	m_pUpDualFilteringShaeder = &ShaderSystem->ComputeShaders["UpDualFiltering"];

    m_SSGIBuffer = DirectX11::CreateBuffer(sizeof(SSGIParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    m_CompositeBuffer = DirectX11::CreateBuffer(sizeof(CompositeParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	//m_BilateralBuffer = DirectX11::CreateBuffer(sizeof(BilateralParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);

    sample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
    pointSample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pTempTexture = Texture::Create(
        ssratio,
        ssratio,
        DirectX11::DeviceStates->g_ClientRect.width,
        DirectX11::DeviceStates->g_ClientRect.height,
        "SSGICopiedTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

    m_pTempTexture2 = Texture::Create(
        ssratio * 2,
        ssratio * 2,
        DirectX11::DeviceStates->g_ClientRect.width,
        DirectX11::DeviceStates->g_ClientRect.height,
        "SSGICopiedTexture2",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture2->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture2->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

    m_pTempTexture3 = Texture::Create(
        ssratio * 4,
        ssratio * 4,
        DirectX11::DeviceStates->g_ClientRect.width,
        DirectX11::DeviceStates->g_ClientRect.height,
        "SSGICopiedTexture3",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture3->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture3->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

    /*m_pBilateralTexture = Texture::Create(
        DirectX11::DeviceStates->g_ClientRect.width,
        DirectX11::DeviceStates->g_ClientRect.height,
        "BilateralTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
	m_pBilateralTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_pBilateralTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);*/
}

SSGIPass::~SSGIPass()
{
    Memory::SafeDelete(sample);
    Memory::SafeDelete(pointSample);
}

void SSGIPass::Initialize(Texture* diffuse, Texture* normal, Texture* lightEmissive, Texture* metalroughocclu, Texture* SSAO)
{
	m_pDiffuseTexture = diffuse;
	m_pNormalTexture = normal;
	m_pLightEmissiveTexture = lightEmissive;
    m_pMetalRoughOcclu = metalroughocclu;
    m_pSSAOTexture = SSAO;
}

void SSGIPass::Execute(RenderScene& scene, Camera& camera)
{
    ExecuteCommandList(scene, camera);
}

void SSGIPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
    if (!isOn) return;

    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    // RHI 이식(PHASE 3-1, 4차 슬라이스). BitMask와 같은 컴퓨트 표면을 쓴다.
    RHINativeShaderResource srv[5] = {
    renderData->m_depthStencil->m_pSRV,
    m_pDiffuseTexture->m_pSRV,
    m_pNormalTexture->m_pSRV,
    m_pLightEmissiveTexture->m_pSRV,
    m_pMetalRoughOcclu->m_pSRV
    };

    ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

    context.SetComputeShader(m_pSSGIShader->GetShader());
    RHINativeSamplerState linearSampler = sample->m_SamplerState;
    RHINativeSamplerState pointSampler = pointSample->m_SamplerState;
    context.SetComputeSamplers(0, 1, &linearSampler);
    context.SetComputeSamplers(1, 1, &pointSampler);

    context.SetComputeShaderResources(0, 5, srv);
    RHINativeUnorderedAccess uav = m_pTempTexture->m_pUAV;
    context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);

    SSGIParams params;
    params.view = renderData->GetFrameSnapshot().view;
    params.proj = renderData->GetFrameSnapshot().projection;
    params.inverseView = XMMatrixInverse(nullptr, params.view);
    params.inverseProjection = XMMatrixInverse(nullptr, params.proj);
    params.screenSize = { RHI::Device().GetFullViewport().width, RHI::Device().GetFullViewport().height };
    params.radius = radius;;
    params.thickness = thickness;
    params.frameIndex = Time->GetFrameCount();
    params.ratio = ssratio;
    params.intensity = intensity;

    renderData->BindFrameCameraBuffers(context);
    context.UpdateBuffer(m_SSGIBuffer.Get(), &params);
    RHINativeBuffer ssgiBuffer = m_SSGIBuffer.Get();
    context.SetComputeConstantBuffers(0, 1, &ssgiBuffer);

    const RHIViewport fullVp = RHI::Device().GetFullViewport();
    const uint32_t viewWidth  = static_cast<uint32_t>(fullVp.width);
    const uint32_t viewHeight = static_cast<uint32_t>(fullVp.height);

    int ratioMulTread = ssratio * ssthreads;
    context.Dispatch(
        (viewWidth + ratioMulTread - 1) / (ratioMulTread),
        (viewHeight + ratioMulTread - 1) / (ratioMulTread), 1);

    RHINativeShaderResource nullsrv[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    RHINativeUnorderedAccess nulluav = nullptr;
    context.SetComputeShaderResources(0, 5, nullsrv);
    context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);

    CompositeParams compositeParams;
    compositeParams.inputTextureSize = { fullVp.width / (ssratio), fullVp.height / (ssratio) };
    compositeParams.ratio = ssratio;
    compositeParams.useOnlySSGI = useOnlySSGI;
    RHINativeBuffer compositeBuffer = m_CompositeBuffer.Get();
    RHINativeShaderResource stepSrv = nullptr;

    if (useDualFilteringStep > 0) {
        // Down Dual Filtering
        context.SetComputeShader(m_pDownDualFilteringShader->GetShader());
        stepSrv = m_pTempTexture->m_pSRV;
        context.SetComputeShaderResources(0, 1, &stepSrv);
        uav = m_pTempTexture2->m_pUAV;
        context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);
        context.UpdateBuffer(m_CompositeBuffer.Get(), &compositeParams);
        context.SetComputeConstantBuffers(0, 1, &compositeBuffer);

        float tempThread = ssratio * ssthreads;
        float temp2NumThread = tempThread * 2;
        context.Dispatch(
            (viewWidth + temp2NumThread - 1) / temp2NumThread,
            (viewHeight + temp2NumThread - 1) / temp2NumThread, 1);
        context.SetComputeShaderResources(0, 1, nullsrv);
        context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);

        if (useDualFilteringStep > 1) {
            // Down Dual Filtering +
            stepSrv = m_pTempTexture2->m_pSRV;
            context.SetComputeShaderResources(0, 1, &stepSrv);
            uav = m_pTempTexture3->m_pUAV;
            context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);
            compositeParams.inputTextureSize = { (float)viewWidth / (ssratio * 2), (float)viewHeight / (ssratio * 2) };
            context.UpdateBuffer(m_CompositeBuffer.Get(), &compositeParams);
            context.SetComputeConstantBuffers(0, 1, &compositeBuffer);

            float temp3NumThread = tempThread * 4;
            context.Dispatch(
                (viewWidth + temp2NumThread - 1) / temp2NumThread,
                (viewHeight + temp2NumThread - 1) / temp2NumThread, 1);
            context.SetComputeShaderResources(0, 1, nullsrv);
            context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);

            // Up Dual Filtering +
            context.SetComputeShader(m_pUpDualFilteringShaeder->GetShader());
            stepSrv = m_pTempTexture3->m_pSRV;
            context.SetComputeShaderResources(0, 1, &stepSrv);
            uav = m_pTempTexture2->m_pUAV;
            context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);
            compositeParams.inputTextureSize = { (float)viewWidth / (ssratio * 4), (float)viewHeight / (ssratio * 4) };
            context.UpdateBuffer(m_CompositeBuffer.Get(), &compositeParams);
            context.SetComputeConstantBuffers(0, 1, &compositeBuffer);
            context.Dispatch(
                (viewWidth + tempThread - 1) / tempThread,
                (viewHeight + tempThread - 1) / tempThread, 1);
            context.SetComputeShaderResources(0, 1, nullsrv);
            context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);
        }

        // Up Dual Filtering
        context.SetComputeShader(m_pUpDualFilteringShaeder->GetShader());
        stepSrv = m_pTempTexture2->m_pSRV;
        context.SetComputeShaderResources(0, 1, &stepSrv);
        uav = m_pTempTexture->m_pUAV;
        context.SetComputeUnorderedAccessViews(0, 1, &uav, nullptr);
        compositeParams.inputTextureSize = { (float)viewWidth / (ssratio * 2), (float)viewHeight / (ssratio * 2) };
        context.UpdateBuffer(m_CompositeBuffer.Get(), &compositeParams);
        context.SetComputeConstantBuffers(0, 1, &compositeBuffer);
        context.Dispatch(
            (viewWidth + tempThread - 1) / tempThread,
            (viewHeight + tempThread - 1) / tempThread, 1);
        context.SetComputeShaderResources(0, 1, nullsrv);
        context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);
    }

    // Composite
    context.SetComputeShader(m_pCompositeShader->GetShader());

    RHINativeShaderResource srv2[3] = {
        m_pTempTexture->m_pSRV,
        m_pDiffuseTexture->m_pSRV,
        m_pSSAOTexture->m_pSRV
    };
    context.SetComputeShaderResources(0, 3, srv2);

    // Set output texture
    RHINativeUnorderedAccess deferredUAV = renderData->m_renderTarget->m_pUAV;
    context.SetComputeUnorderedAccessViews(0, 1, &deferredUAV, nullptr);
    compositeParams.inputTextureSize = { (float)viewWidth / (ssratio), (float)viewHeight / (ssratio) };
    context.UpdateBuffer(m_CompositeBuffer.Get(), &compositeParams);
    context.SetComputeConstantBuffers(0, 1, &compositeBuffer);
    context.Dispatch((viewWidth + 15) / 16, (viewHeight + 15) / 16, 1);
    // Clear resources
    RHINativeShaderResource nullSRV[3] = { nullptr, nullptr, nullptr };
    context.SetComputeShaderResources(0, 3, nullSRV);
    context.SetComputeUnorderedAccessViews(0, 1, &nulluav, nullptr);
    
   /* else {
		DirectX11::CSSetShader(deferredPtr, m_pBilateralFilterShader->GetShader(), nullptr, 0);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture->m_pSRV);
        DirectX11::CSSetShaderResources(deferredPtr, 1, 1, &m_pNormalTexture->m_pSRV);
        DirectX11::CSSetShaderResources(deferredPtr, 2, 1, &m_pDiffuseTexture->m_pSRV);
        ID3D11UnorderedAccessView* deferredUAV = renderData->m_renderTarget->m_pUAV;
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &deferredUAV, nullptr);
		BilateralParams bilateralParams;
		bilateralParams.screenSize = { DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height };
		bilateralParams.sigmaSpace = sigmaSpace;
		bilateralParams.sigmaRange = sigmaRange;
		DirectX11::UpdateBuffer(deferredPtr, m_BilateralBuffer.Get(), &bilateralParams);
		DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_BilateralBuffer.GetAddressOf());
		DirectX11::Dispatch(deferredPtr,
			(DirectX11::DeviceStates->g_Viewport.Width + ssthreads - 1) / ssthreads,
			(DirectX11::DeviceStates->g_Viewport.Height + ssthreads - 1) / ssthreads, 1);
		ID3D11ShaderResourceView* nullsrv[3] = { nullptr, nullptr, nullptr };
		ID3D11UnorderedAccessView* nulluav = nullptr;
		DirectX11::CSSetShaderResources(deferredPtr, 0, 3, nullsrv);
		DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);
    }*/


    ID3D11CommandList* commandList{};
    deferredPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void SSGIPass::ControlPanel()
{
    ImGui::PushID(this);
    ImGui::Text("SSGI");
    auto& setting = EngineSettingInstance->GetRenderPassSettingsRW().ssgi;
    if (ImGui::Checkbox("Enable SSGI", &isOn))
    {
        setting.isOn = isOn;
    }
    if (ImGui::Checkbox("Use Only SSGI", &useOnlySSGI))
    {
        setting.useOnlySSGI = useOnlySSGI;
    }
	//ImGui::Checkbox("Use Bilateral Filter", &useBilateralFiltering);
	//ImGui::SliderFloat("Sigma Space", &sigmaSpace, 0.0f, 1.0f, "Sigma Space: %.2f");
	//ImGui::SliderFloat("Sigma Range", &sigmaRange, 0.0f, 1.0f, "Sigma Range: %.2f");

    if (ImGui::SliderInt("Use Dual Filtering", &useDualFilteringStep, 0, 2, "Step: %d"))
    {
        setting.useDualFilteringStep = useDualFilteringStep;
    }
    if (ImGui::SliderFloat("Radius", &radius, 0.0f, 10.0f))
    {
        setting.radius = radius;
    }
    if (ImGui::SliderFloat("Thickness", &thickness, 0.0f, 1.0f))
    {
        setting.thickness = thickness;
    }
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 10.0f, "Intensity: %.2f"))
    {
        setting.intensity = intensity;
    }

    if (ImGui::SliderInt("SSGI Ratio", &ssratio, 1, 4, "SSGI Ratio: %d")) {
        m_pTempTexture->SetSizeRatio({ float(ssratio), float(ssratio)});
                m_pTempTexture2->SetSizeRatio({ float(ssratio * 2), float(ssratio * 2) });
                m_pTempTexture3->SetSizeRatio({ float(ssratio * 4), float(ssratio * 4) });
        m_pTempTexture->ResizeRelease();
        m_pTempTexture2->ResizeRelease();
        m_pTempTexture3->ResizeRelease();

                m_pTempTexture->ResizeViews(DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height);
                m_pTempTexture2->ResizeViews(DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height);
                m_pTempTexture3->ResizeViews(DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height);
        setting.ssratio = ssratio;
    }

        if (ImGui::Button("Reset")) {
                radius = 4.f; // Reset to default value
                thickness = 0.5f; // Reset to default value
                intensity = 1.f; // Reset to default value

                setting.radius = radius;
                setting.thickness = thickness;
                setting.intensity = intensity;
        }

    ImGui::PopID();
}

void SSGIPass::Resize(uint32_t width, uint32_t height)
{
}

void SSGIPass::ApplySettings(const SSGIPassSetting& setting)
{
    isOn = setting.isOn;
    useOnlySSGI = setting.useOnlySSGI;
    useDualFilteringStep = setting.useDualFilteringStep;
    radius = setting.radius;
    thickness = setting.thickness;
    intensity = setting.intensity;
    ssratio = setting.ssratio;

    m_pTempTexture->SetSizeRatio({ float(ssratio), float(ssratio) });
    m_pTempTexture2->SetSizeRatio({ float(ssratio * 2), float(ssratio * 2) });
    m_pTempTexture3->SetSizeRatio({ float(ssratio * 4), float(ssratio * 4) });
    m_pTempTexture->ResizeRelease();
    m_pTempTexture2->ResizeRelease();
    m_pTempTexture3->ResizeRelease();

    m_pTempTexture->ResizeViews(DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height);
    m_pTempTexture2->ResizeViews(DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height);
    m_pTempTexture3->ResizeViews(DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height);
}
