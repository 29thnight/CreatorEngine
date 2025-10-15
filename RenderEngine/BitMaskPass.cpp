#include "BitMaskPass.h"
#include "ShaderSystem.h"
#include "Scene.h"

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

    m_pTempTexture = Texture::Create(
        2,
        2,
        DirectX11::DeviceStates->g_ClientRect.width,
        DirectX11::DeviceStates->g_ClientRect.height,
        "BitmaskDownTexture",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS
    );
    m_pTempTexture->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_pTempTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

    m_pTempTexture2 = Texture::Create(
        DirectX11::DeviceStates->g_ClientRect.width,
        DirectX11::DeviceStates->g_ClientRect.height,
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

void BitMaskPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	if (!isOn) return; // If the pass is not enabled, skip execution

    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

	// edge filter
    ID3D11DeviceContext* deferredPtr = deferredContext;
    DirectX11::CSSetShader(deferredPtr, m_pEdgefilterShader->GetShader(), nullptr, 0);
    DirectX11::CSSetSamplers(deferredPtr, 0, 1, &sample->m_SamplerState); // sampler 0
    DirectX11::CSSetSamplers(deferredPtr, 1, 1, &pointSample->m_SamplerState); // sampler 1

    ID3D11ShaderResourceView* srv = { m_pBitmaskTexture->m_pSRV };
	DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &srv);
    DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &m_pTempTexture2->m_pUAV, nullptr);

	EdgefilterBuffer efparams{};
	for (int i = 0; i < 8; ++i)
	{
		efparams.m_color[i] = m_colors[i];
	}
	efparams.m_screenSize = { (float)DirectX11::DeviceStates->g_Viewport.Width, (float)DirectX11::DeviceStates->g_Viewport.Height };
    efparams.m_outlineVelocity = outlineVelocity;
	DirectX11::UpdateBuffer(deferredPtr, m_EdgefilterBuffer.Get(), &efparams);
	DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_EdgefilterBuffer.GetAddressOf());
	DirectX11::Dispatch(deferredPtr, DirectX11::DeviceStates->g_Viewport.Width / 16, DirectX11::DeviceStates->g_Viewport.Height / 16, 1);

    ID3D11ShaderResourceView* nullsrv = nullptr;
    ID3D11UnorderedAccessView* nulluav = nullptr;
    DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &nullsrv);
    DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);

    if (blurOutline) {
        // Down Dual Filtering
        DirectX11::CSSetShader(deferredPtr, m_pEdgefilterDownSamplingShader->GetShader(), nullptr, 0);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture2->m_pSRV);
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &m_pTempTexture->m_pUAV, nullptr);

        UownUpSamplingParams DPparams;
        DPparams.inputTextureSize = { (float)DirectX11::DeviceStates->g_Viewport.Width, (float)DirectX11::DeviceStates->g_Viewport.Height };
        DPparams.ratio = 2;
        DirectX11::UpdateBuffer(deferredPtr, m_EdgefilterSamplingBuffer.Get(), &DPparams);
        DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_EdgefilterSamplingBuffer.GetAddressOf());

        DirectX11::Dispatch(deferredPtr,
            (DirectX11::DeviceStates->g_Viewport.Width + 32 - 1) / 32,
            (DirectX11::DeviceStates->g_Viewport.Height + 32 - 1) / 32, 1);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &nullsrv);
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);

        // Up Dual Filtering
        DirectX11::CSSetShader(deferredPtr, m_pEdgefilterUpSamplingShader->GetShader(), nullptr, 0);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture->m_pSRV);
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &m_pTempTexture2->m_pUAV, nullptr);
        DPparams.inputTextureSize = { (float)DirectX11::DeviceStates->g_Viewport.Width / 2, (float)DirectX11::DeviceStates->g_Viewport.Height / 2 };
        DirectX11::UpdateBuffer(deferredPtr, m_EdgefilterSamplingBuffer.Get(), &DPparams);
        DirectX11::CSSetConstantBuffer(deferredPtr, 0, 1, m_EdgefilterSamplingBuffer.GetAddressOf());
        DirectX11::Dispatch(deferredPtr,
            (DirectX11::DeviceStates->g_Viewport.Width + 16 - 1) / 16,
            (DirectX11::DeviceStates->g_Viewport.Height + 16 - 1) / 16, 1);
        DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &nullsrv);
        DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);
    }

	//Add Color
	DirectX11::CSSetShader(deferredPtr, m_pAddColorShader->GetShader(), nullptr, 0);
    DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &m_pTempTexture2->m_pSRV);
    DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &renderData->m_renderTarget->m_pUAV, nullptr);
    DirectX11::Dispatch(deferredPtr,
        (DirectX11::DeviceStates->g_Viewport.Width + 16 - 1) / 16,
        (DirectX11::DeviceStates->g_Viewport.Height + 16 - 1) / 16, 1);
    DirectX11::CSSetShaderResources(deferredPtr, 0, 1, &nullsrv);
    DirectX11::CSSetUnorderedAccessViews(deferredPtr, 0, 1, &nulluav, nullptr);


    ID3D11CommandList* commandList{};
    deferredPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void BitMaskPass::ControlPanel()
{
    ImGui::PushID(this);
	ImGui::Text("BitMask Pass");
    ImGui::Checkbox("Enable Outline", &isOn);
    ImGui::Checkbox("Enable Outline blur", &blurOutline);
    ImGui::DragFloat("Outline Velocity", &outlineVelocity);
    for (int i = 0; i < 8; i++) {
        ImGui::PushID(i);
        ImGui::ColorEdit4(("Color" + std::to_string(i)).c_str(), &m_colors[i].x);
        ImGui::DragFloat("ColorVelocity", &m_colors[i].w, 1.f);
        ImGui::PopID();
    }
    ImGui::PopID();
}

void BitMaskPass::Resize(uint32_t width, uint32_t height)
{
}
