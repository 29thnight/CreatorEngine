#include "SSGIPass.h"
#include "ShaderSystem.h"
#include "Scene.h"

//#define SSGI_Ratio 1
//#define SSGI_NumThreads SSGI_Ratio * 16
int ssratio = 4;
int ssthreads = 16;


cbuffer SSGIParams
{
    XMMATRIX invVP;
    XMMATRIX proj;
    XMMATRIX inverseProjection;
    float2 screenSize; // 화면 크기
    float radius; // 샘플링 반경
    float thickness; // 두께
    UINT frameIndex;
    int ratio;
};

cbuffer CompositeParams{
    float2 inputTextureSize;
    int ratio;
};

SSGIPass::SSGIPass()
{
    m_pSSGIShader = &ShaderSystem->ComputeShaders["SSGI"];
    m_pCompositeShader = &ShaderSystem->ComputeShaders["SSGIComposite"];

    m_SSGIBuffer = DirectX11::CreateBuffer(sizeof(SSGIParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    m_CompositeBuffer = DirectX11::CreateBuffer(sizeof(CompositeParams), D3D11_BIND_CONSTANT_BUFFER, nullptr);

    sample = new Sampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_CLAMP);
    pointSample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pTempTexture = Texture::Create(
        4u,
        4u,
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

    // SSGI
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
    DirectX11::CSSetUnorderedAccessViews(defferdPtr, 0, 1, &m_pTempTexture->m_pUAV, nullptr);

    SSGIParams params;
    params.invVP = camera.CalculateInverseView() * camera.CalculateInverseProjection();
    params.proj = camera.CalculateProjection();
    params.inverseProjection = XMMatrixInverse(nullptr, camera.CalculateProjection());
    params.screenSize = { DeviceState::g_Viewport.Width, DeviceState::g_Viewport.Height };
    params.radius = radius;;
    params.thickness = thickness;
    params.frameIndex = Time->GetFrameCount();
	params.ratio = ssratio;

    camera.UpdateBuffer(defferdPtr);
    DirectX11::UpdateBuffer(defferdPtr, m_SSGIBuffer.Get(), &params);
    DirectX11::CSSetConstantBuffer(defferdPtr, 0, 1, m_SSGIBuffer.GetAddressOf());

	int ratioMulTread = ssratio * ssthreads;
    DirectX11::Dispatch(defferdPtr, 
        (DeviceState::g_Viewport.Width + ratioMulTread - 1) / (ratioMulTread),
        (DeviceState::g_Viewport.Height + ratioMulTread - 1) / (ratioMulTread), 1);

    ID3D11ShaderResourceView* nullsrv[4] = { nullptr, nullptr, nullptr, nullptr };
    ID3D11UnorderedAccessView* nulluav = nullptr;
    DirectX11::CSSetShaderResources(defferdPtr, 0, 4, nullsrv);
    DirectX11::CSSetUnorderedAccessViews(defferdPtr, 0, 1, &nulluav, nullptr);



    //DirectX11::CopyResource(defferdPtr, renderData->m_renderTarget->m_pTexture, m_pTempTexture->m_pTexture);
    // Composite
	DirectX11::CSSetShader(defferdPtr, m_pCompositeShader->GetShader(), nullptr, 0);

    ID3D11ShaderResourceView* srv2[2] = {
		m_pTempTexture->m_pSRV,
		m_pDiffuseTexture->m_pSRV,
	};
	DirectX11::CSSetShaderResources(defferdPtr, 0, 2, srv2);
	CompositeParams compositeParams;
	compositeParams.inputTextureSize = { (float)DeviceState::g_Viewport.Width / ssratio, (float)DeviceState::g_Viewport.Height / ssratio };
    compositeParams.ratio = ssratio;
    DirectX11::UpdateBuffer(defferdPtr, m_CompositeBuffer.Get(), &compositeParams);
	DirectX11::CSSetConstantBuffer(defferdPtr, 0, 1, m_CompositeBuffer.GetAddressOf());


	// Set output texture
	ID3D11UnorderedAccessView* defferdUAV = renderData->m_renderTarget->m_pUAV;
	DirectX11::CSSetUnorderedAccessViews(defferdPtr, 0, 1, &defferdUAV, nullptr);
	DirectX11::Dispatch(defferdPtr, DeviceState::g_Viewport.Width / 16, DeviceState::g_Viewport.Height / 16, 1);
	// Clear resources
	ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
	DirectX11::CSSetShaderResources(defferdPtr, 0, 2, nullSRV);
    DirectX11::CSSetUnorderedAccessViews(defferdPtr, 0, 1, &nulluav, nullptr);



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

    if (ImGui::SliderInt("SSGI Ratio", &ssratio, 1, 4, "SSGI Ratio: %d")) {
        m_pTempTexture->SetSizeRatio({ float(ssratio), float(ssratio)});
    }

	if (ImGui::Button("Reset")) {
		radius = 4.f; // Reset to default value
		thickness = 0.5f; // Reset to default value
	}

    ImGui::PopID();
}

void SSGIPass::Resize(uint32_t width, uint32_t height)
{
}
