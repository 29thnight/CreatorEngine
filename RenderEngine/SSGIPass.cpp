#include "SSGIPass.h"
#include "ShaderSystem.h"
#include "Scene.h"

//#define SSGI_Ratio 1
//#define SSGI_NumThreads SSGI_Ratio * 16
int ssratio = 4;
int ssthreads = 16;


cbuffer SSGIParams
{
    XMMATRIX view;
    XMMATRIX proj;
    XMMATRIX inverseProjection;
    float2 screenSize; // ȭ�� ũ��
    float radius; // ���ø� �ݰ�
    float thickness; // �β�
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
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "SSGICopiedTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

    m_pTempTexture2 = Texture::Create(
        ssratio * 2,
        ssratio * 2,
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "SSGICopiedTexture2",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture2->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture2->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

    m_pTempTexture3 = Texture::Create(
        ssratio * 4,
        ssratio * 4,
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "SSGICopiedTexture3",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture3->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture3->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

    /*m_pBilateralTexture = Texture::Create(
        DeviceState::g_ClientRect.width,
        DeviceState::g_ClientRect.height,
        "BilateralTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
	m_pBilateralTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_pBilateralTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);*/
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

void SSGIPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
    if (!isOn) return;

    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    // SSGI
    ID3D11ShaderResourceView* srv[4] = {
    renderData->m_depthStencil->m_pSRV,
    renderData->m_renderTarget->m_pSRV,
    m_pNormalTexture->m_pSRV,
    m_pLightEmissiveTexture->m_pSRV
    };

    ID3D11DeviceContext* deferredPtr = deferredContext;
    DirectX11::CSSetShader(deferredPtr, m_pSSGIShader->GetShader(), nullptr, 0);
    DirectX11::CSSetSamplers(deferredPtr, 0, 1, &sample->m_SamplerState); // sampler 0
    DirectX11::CSSetSamplers(deferredPtr, 1, 1, &pointSample->m_SamplerState); // sampler 1

    DirectX11::CSSetShaderResources(deferredPtr, 0, 4, srv);
    DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &m_pTempTexture->m_pUAV, nullptr);

    SSGIParams params;
    params.view = camera.CalculateView();
    params.proj = camera.CalculateProjection();
    params.inverseProjection = XMMatrixInverse(nullptr, camera.CalculateProjection());
    params.screenSize = { DeviceState::g_Viewport.Width, DeviceState::g_Viewport.Height };
    params.radius = radius;;
    params.thickness = thickness;
    params.frameIndex = Time->GetFrameCount();
    params.ratio = ssratio;
    params.intensity = intensity;

    camera.UpdateBuffer(deferredPtr);
    DirectX11::UpdateBuffer(deferredPtr, m_SSGIBuffer.Get(), &params);
    DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_SSGIBuffer.GetAddressOf());

    int ratioMulTread = ssratio * ssthreads;
    DirectX11::Dispatch(deferredPtr,
        (DeviceState::g_Viewport.Width + ratioMulTread - 1) / (ratioMulTread),
        (DeviceState::g_Viewport.Height + ratioMulTread - 1) / (ratioMulTread), 1);

    ID3D11ShaderResourceView* nullsrv[4] = { nullptr, nullptr, nullptr, nullptr };
    ID3D11UnorderedAccessView* nulluav = nullptr;
    DirectX11::CSSetShaderResources(deferredPtr, 0, 4, nullsrv);
    DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);

    CompositeParams compositeParams;
    compositeParams.inputTextureSize = { (float)DeviceState::g_Viewport.Width / (ssratio), (float)DeviceState::g_Viewport.Height / (ssratio) };
    compositeParams.ratio = ssratio;
    compositeParams.useOnlySSGI = useOnlySSGI;
    if (useDualFilteringStep > 0) {
        // Down Dual Filtering
        DirectX11::CSSetShader(deferredPtr, m_pDownDualFilteringShader->GetShader(), nullptr, 0);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture->m_pSRV);
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &m_pTempTexture2->m_pUAV, nullptr);
        DirectX11::UpdateBuffer(deferredPtr, m_CompositeBuffer.Get(), &compositeParams);
        DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_CompositeBuffer.GetAddressOf());

        float tempThread = ssratio * ssthreads;
        float temp2NumThread = tempThread * 2;
        DirectX11::Dispatch(deferredPtr,
            (DeviceState::g_Viewport.Width + temp2NumThread - 1) / temp2NumThread,
            (DeviceState::g_Viewport.Height + temp2NumThread - 1) / temp2NumThread, 1);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, nullsrv);
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);

        if (useDualFilteringStep > 1) {
            // Down Dual Filtering +
            DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture2->m_pSRV);
            DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &m_pTempTexture3->m_pUAV, nullptr);
            compositeParams.inputTextureSize = { (float)DeviceState::g_Viewport.Width / (ssratio * 2), (float)DeviceState::g_Viewport.Height / (ssratio * 2) };
            DirectX11::UpdateBuffer(deferredPtr, m_CompositeBuffer.Get(), &compositeParams);
            DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_CompositeBuffer.GetAddressOf());

            float temp3NumThread = tempThread * 4;
            DirectX11::Dispatch(deferredPtr,
                (DeviceState::g_Viewport.Width + temp2NumThread - 1) / temp2NumThread,
                (DeviceState::g_Viewport.Height + temp2NumThread - 1) / temp2NumThread, 1);
            DirectX11::CSSetShaderResources(deferredPtr, 0, 1, nullsrv);
            DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);

            // Up Dual Filtering +
            DirectX11::CSSetShader(deferredPtr, m_pUpDualFilteringShaeder->GetShader(), nullptr, 0);
            DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture3->m_pSRV);
            DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &m_pTempTexture2->m_pUAV, nullptr);
            compositeParams.inputTextureSize = { (float)DeviceState::g_Viewport.Width / (ssratio * 4), (float)DeviceState::g_Viewport.Height / (ssratio * 4) };
            DirectX11::UpdateBuffer(deferredPtr, m_CompositeBuffer.Get(), &compositeParams);
            DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_CompositeBuffer.GetAddressOf());
            DirectX11::Dispatch(deferredPtr,
                (DeviceState::g_Viewport.Width + tempThread - 1) / tempThread,
                (DeviceState::g_Viewport.Height + tempThread - 1) / tempThread, 1);
            DirectX11::CSSetShaderResources(deferredPtr, 0, 1, nullsrv);
            DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);
        }

        // Up Dual Filtering
        DirectX11::CSSetShader(deferredPtr, m_pUpDualFilteringShaeder->GetShader(), nullptr, 0);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture2->m_pSRV);
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &m_pTempTexture->m_pUAV, nullptr);
        compositeParams.inputTextureSize = { (float)DeviceState::g_Viewport.Width / (ssratio * 2), (float)DeviceState::g_Viewport.Height / (ssratio * 2) };
        DirectX11::UpdateBuffer(deferredPtr, m_CompositeBuffer.Get(), &compositeParams);
        DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_CompositeBuffer.GetAddressOf());
        DirectX11::Dispatch(deferredPtr,
            (DeviceState::g_Viewport.Width + tempThread - 1) / tempThread,
            (DeviceState::g_Viewport.Height + tempThread - 1) / tempThread, 1);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, nullsrv);
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);
    }

    //DirectX11::CopyResource(deferredPtr, renderData->m_renderTarget->m_pTexture, m_pTempTexture->m_pTexture);
    // Composite
    DirectX11::CSSetShader(deferredPtr, m_pCompositeShader->GetShader(), nullptr, 0);

    ID3D11ShaderResourceView* srv2[2] = {
        m_pTempTexture->m_pSRV,
        m_pDiffuseTexture->m_pSRV,
    };
    DirectX11::CSSetShaderResources(deferredPtr, 0, 2, srv2);


    // Set output texture
    ID3D11UnorderedAccessView* deferredUAV = renderData->m_renderTarget->m_pUAV;
    DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &deferredUAV, nullptr);
    compositeParams.inputTextureSize = { (float)DeviceState::g_Viewport.Width / (ssratio), (float)DeviceState::g_Viewport.Height / (ssratio) };
    DirectX11::UpdateBuffer(deferredPtr, m_CompositeBuffer.Get(), &compositeParams);
    DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_CompositeBuffer.GetAddressOf());
    DirectX11::Dispatch(deferredPtr, DeviceState::g_Viewport.Width / 16, DeviceState::g_Viewport.Height / 16, 1);
    // Clear resources
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    DirectX11::CSSetShaderResources(deferredPtr, 0, 2, nullSRV);
    DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);
    
   /* else {
		DirectX11::CSSetShader(deferredPtr, m_pBilateralFilterShader->GetShader(), nullptr, 0);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture->m_pSRV);
        DirectX11::CSSetShaderResources(deferredPtr, 1, 1, &m_pNormalTexture->m_pSRV);
        DirectX11::CSSetShaderResources(deferredPtr, 2, 1, &m_pDiffuseTexture->m_pSRV);
        ID3D11UnorderedAccessView* deferredUAV = renderData->m_renderTarget->m_pUAV;
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &deferredUAV, nullptr);
		BilateralParams bilateralParams;
		bilateralParams.screenSize = { DeviceState::g_Viewport.Width, DeviceState::g_Viewport.Height };
		bilateralParams.sigmaSpace = sigmaSpace;
		bilateralParams.sigmaRange = sigmaRange;
		DirectX11::UpdateBuffer(deferredPtr, m_BilateralBuffer.Get(), &bilateralParams);
		DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_BilateralBuffer.GetAddressOf());
		DirectX11::Dispatch(deferredPtr,
			(DeviceState::g_Viewport.Width + ssthreads - 1) / ssthreads,
			(DeviceState::g_Viewport.Height + ssthreads - 1) / ssthreads, 1);
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
    ImGui::Checkbox("Enable SSGI", &isOn);
	ImGui::Checkbox("Use Only SSGI", &useOnlySSGI);
	//ImGui::Checkbox("Use Bilateral Filter", &useBilateralFiltering);
	//ImGui::SliderFloat("Sigma Space", &sigmaSpace, 0.0f, 1.0f, "Sigma Space: %.2f");
	//ImGui::SliderFloat("Sigma Range", &sigmaRange, 0.0f, 1.0f, "Sigma Range: %.2f");

	ImGui::SliderInt("Use Dual Filtering", &useDualFilteringStep, 0, 2, "Step: %d");
    ImGui::SliderFloat("Radius", &radius, 0.0f, 10.0f);
    ImGui::SliderFloat("Thickness", &thickness, 0.0f, 1.0f);
	ImGui::SliderFloat("Intensity", &intensity, 0.0f, 10.0f, "Intensity: %.2f");

    if (ImGui::SliderInt("SSGI Ratio", &ssratio, 1, 4, "SSGI Ratio: %d")) {
        m_pTempTexture->SetSizeRatio({ float(ssratio), float(ssratio)});
		m_pTempTexture2->SetSizeRatio({ float(ssratio * 2), float(ssratio * 2) });
		m_pTempTexture3->SetSizeRatio({ float(ssratio * 4), float(ssratio * 4) });
        m_pTempTexture->ResizeRelease();
        m_pTempTexture2->ResizeRelease();
        m_pTempTexture3->ResizeRelease();

		m_pTempTexture->ResizeViews(DeviceState::g_Viewport.Width, DeviceState::g_Viewport.Height);
		m_pTempTexture2->ResizeViews(DeviceState::g_Viewport.Width, DeviceState::g_Viewport.Height);
		m_pTempTexture3->ResizeViews(DeviceState::g_Viewport.Width, DeviceState::g_Viewport.Height);
    }

	if (ImGui::Button("Reset")) {
		radius = 4.f; // Reset to default value
		thickness = 0.5f; // Reset to default value
		intensity = 1.f; // Reset to default value
	}

    ImGui::PopID();
}

void SSGIPass::Resize(uint32_t width, uint32_t height)
{
}
