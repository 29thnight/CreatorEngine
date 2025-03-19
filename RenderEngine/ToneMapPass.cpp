#include "ToneMapPass.h"
#include "AssetSystem.h"
#include "ImGuiRegister.h"
#include "DeviceState.h"

ToneMapPass::ToneMapPass()
{
    m_pso = std::make_unique<PipelineStateObject>();

    m_pso->m_vertexShader = &AssetsSystems->VertexShaders["Fullscreen"];
    m_pso->m_pixelShader = &AssetsSystems->PixelShaders["ToneMapACES"];
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

    D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateInputLayout(
            vertexLayoutDesc,
            _countof(vertexLayoutDesc),
            m_pso->m_vertexShader->GetBufferPointer(),
            m_pso->m_vertexShader->GetBufferSize(),
            &m_pso->m_inputLayout
        )
    );

    CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };

    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateRasterizerState(
            &rasterizerDesc,
            &m_pso->m_rasterizerState
        )
    );

    m_pso->m_samplers.emplace_back(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    m_pso->m_samplers.emplace_back(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

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

}

ToneMapPass::~ToneMapPass()
{
}

void ToneMapPass::Initialize(Texture* color, Texture* dest)
{
    m_ColorTexture = color;
    m_DestTexture = dest;
}

void ToneMapPass::ToneMapSetting(bool isAbleToneMap, ToneMapType type)
{
	m_isAbleToneMap = isAbleToneMap;
	m_toneMapType = type;
}

void ToneMapPass::Execute(Scene& scene)
{

	if (m_toneMapType == ToneMapType::Reinhard)
	{
        m_pso->m_pixelShader = &AssetsSystems->PixelShaders["ToneMapReinhard"];
		m_toneMapReinhardConstant.m_bUseToneMap = m_isAbleToneMap;
		DirectX11::UpdateBuffer(m_pReinhardConstantBuffer, &m_toneMapReinhardConstant);
		DirectX11::PSSetConstantBuffer(0, 1, &m_pReinhardConstantBuffer);
	}
	else if (m_toneMapType == ToneMapType::ACES)
	{
        m_pso->m_pixelShader = &AssetsSystems->PixelShaders["ToneMapACES"];
		m_toneMapACESConstant.m_bUseToneMap = m_isAbleToneMap;
		DirectX11::UpdateBuffer(m_pACESConstantBuffer, &m_toneMapACESConstant);
		DirectX11::PSSetConstantBuffer(0, 1, &m_pACESConstantBuffer);
	}

    m_pso->Apply();

    ID3D11RenderTargetView* renderTargets[] = { m_DestTexture->GetRTV() };
    DirectX11::OMSetRenderTargets(1, renderTargets, nullptr);

    DirectX11::PSSetShaderResources(0, 1, &m_ColorTexture->m_pSRV);
    DirectX11::Draw(4, 0);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    DirectX11::PSSetShaderResources(0, 1, &nullSRV);
    DirectX11::UnbindRenderTargets();
}

void ToneMapPass::ControlPanel()
{
    ImGui::Checkbox("ToneMap", &m_isAbleToneMap);
    ImGui::Combo("ToneMap Type", (int*)&m_toneMapType, "Reinhard\0ACES\0");
    ImGui::Separator();
    if (m_toneMapType == ToneMapType::ACES)
    {
        ImGui::SliderFloat("Film Slope", &m_toneMapACESConstant.filmSlope, 0.0f, 1.0f);
        ImGui::SliderFloat("Film Toe", &m_toneMapACESConstant.filmToe, 0.0f, 1.0f);
        ImGui::SliderFloat("Film Shoulder", &m_toneMapACESConstant.filmShoulder, 0.0f, 1.0f);
        ImGui::SliderFloat("Film Black Clip", &m_toneMapACESConstant.filmBlackClip, 0.0f, 1.0f);
        ImGui::SliderFloat("Film White Clip", &m_toneMapACESConstant.filmWhiteClip, 0.0f, 1.0f);
    }
}
