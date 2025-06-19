#include "PostProcessingPass.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"
#include "ResourceAllocator.h"

#pragma warning(disable: 2398)
PostProcessingPass::PostProcessingPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
	auto pointSampler  = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
	m_pBloomDownSampledCS = &ShaderSystem->ComputeShaders["BloomThresholdDownsample"];
	m_pGaussianBlurCS = &ShaderSystem->ComputeShaders["GaussianBlur"];
	m_pBloomCompositePS = &ShaderSystem->PixelShaders["BloomComposite"];

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

	TextureInitialization();
	GaussianBlurComputeKernel();

	m_bloomThresholdBuffer	= DirectX11::CreateBuffer(sizeof(ThresholdParams), D3D11_BIND_CONSTANT_BUFFER, &m_bloomThreshold);
	m_bloomBlurBuffer		= DirectX11::CreateBuffer(sizeof(BlurParams), D3D11_BIND_CONSTANT_BUFFER, &m_bloomBlur);
	m_bloomCompositeBuffer	= DirectX11::CreateBuffer(sizeof(CompositeParams), D3D11_BIND_CONSTANT_BUFFER, &m_bloomComposite);
}

PostProcessingPass::~PostProcessingPass()
{
	//Memory::SafeDelete(m_CopiedTexture);
    DeallocateResource(m_CopiedTexture);
}

void PostProcessingPass::Execute(RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);
	// Copy the back buffer to a texture
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
	ImGui::Checkbox("ApplyBloom",	&m_PostProcessingApply.m_Bloom);
	ImGui::DragFloat("Threshold",	&m_bloomThreshold.threshold);
	ImGui::DragFloat("Knee",		&m_bloomThreshold.knee);
	ImGui::DragFloat("Coeddicient", &m_bloomComposite.coeddicient);

	if (ImGui::Button("Reset")) {
		m_bloomThreshold.threshold = 0.3f;
		m_bloomThreshold.knee = 0.5f;
		m_bloomComposite.coeddicient = 0.3f;
	}
	ImGui::PopID();

}

void PostProcessingPass::PrepaerShaderState()
{
	m_pBloomDownSampledCS = &ShaderSystem->ComputeShaders["BloomThresholdDownsample"];
	m_pGaussianBlurCS = &ShaderSystem->ComputeShaders["GaussianBlur"];
	m_pBloomCompositePS = &ShaderSystem->PixelShaders["BloomComposite"];
}

void PostProcessingPass::TextureInitialization()
{
	//Bloom Pass
	BloomBufferWidth = DeviceState::g_ClientRect.width / 2;
	BloomBufferHeight = DeviceState::g_ClientRect.height / 2;

	m_CopiedTexture = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_CopiedTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_CopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_BloomFilterSRV1 = Texture::Create(
		2u,
		2u,
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"BloomFilterSRV1",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
	);
	m_BloomFilterSRV1->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_BloomFilterSRV1->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_BloomFilterSRV2 = Texture::Create(
		2u,
		2u,
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"BloomFilterSRV2",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
	);
	m_BloomFilterSRV2->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_BloomFilterSRV2->CreateUAV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_BloomResult = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_BloomResult->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_BloomResult->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

}

void PostProcessingPass::BloomPass(RenderScene& scene, Camera& camera)
{
	//m_pso->m_vertexShader = m_pFullScreenVS;
	m_pso->m_pixelShader = m_pBloomCompositePS;

	m_pso->Apply();

	// Downsample the copied texture
	const UINT offsets[]{ 0 };
	constexpr ID3D11RenderTargetView* nullRTV = nullptr;
	constexpr ID3D11ShaderResourceView* nullSRV = nullptr;
	constexpr ID3D11UnorderedAccessView* nullUAV = nullptr;
	{
		DirectX11::UpdateBuffer(m_bloomThresholdBuffer.Get(), &m_bloomThreshold);

		DirectX11::CSSetShader(m_pBloomDownSampledCS->GetShader(), 0, 0);
		DirectX11::CSSetShaderResources(0, 1, &m_CopiedTexture->m_pSRV);
		DirectX11::CSSetUnorderedAccessViews(0, 1, &m_BloomFilterSRV1->m_pUAV, offsets);
		DirectX11::CSSetConstantBuffer(0, 1, m_bloomThresholdBuffer.GetAddressOf());

		DirectX11::Dispatch(DeviceState::g_Viewport.Width / 16, DeviceState::g_Viewport.Height / 16, 1);

		DirectX11::CSSetShaderResources(0, 1, &nullSRV);
		DirectX11::CSSetUnorderedAccessViews(0, 1, &nullUAV, offsets);
	}

	// Blur the downsampled texture
	{
		DirectX11::CSSetShader(m_pGaussianBlurCS->GetShader(), 0, 0);
		DirectX11::CSSetConstantBuffer(0, 1, m_bloomBlurBuffer.GetAddressOf());

		ID3D11ShaderResourceView* csSRVs[]{ m_BloomFilterSRV1->m_pSRV, m_BloomFilterSRV2->m_pSRV };
		ID3D11UnorderedAccessView* csUAVs[]{ m_BloomFilterSRV2->m_pUAV, m_BloomFilterSRV1->m_pUAV };

		for (uint32 direction = 0; direction < 2; ++direction)
		{
			m_bloomBlur.direction = direction;
			DirectX11::UpdateBuffer(m_bloomBlurBuffer.Get(), &m_bloomBlur);

			DirectX11::CSSetShaderResources(0, 1, &csSRVs[direction]);
			DirectX11::CSSetUnorderedAccessViews(0, 1, &csUAVs[direction], offsets);

			DirectX11::Dispatch(DeviceState::g_Viewport.Width / 16, DeviceState::g_Viewport.Height / 16, 1);

			DirectX11::CSSetShaderResources(0, 1, &nullSRV);
			DirectX11::CSSetUnorderedAccessViews(0, 1, &nullUAV, offsets);
		}
	}

	// Composite the blurred texture with the original texture
	{
		ID3D11RenderTargetView* rtv = m_BloomResult->GetRTV();
		DirectX11::OMSetRenderTargets(1, &rtv, nullptr);

		//DirectX11::VSSetShader(m_pFullScreenVS->GetShader(), 0, 0);
		DirectX11::PSSetShader(m_pBloomCompositePS->GetShader(), 0, 0);

		ID3D11ShaderResourceView* pSRVs[]{ m_CopiedTexture->m_pSRV, m_BloomFilterSRV1->m_pSRV };
		DirectX11::PSSetShaderResources(0, 2, pSRVs);

		DirectX11::UpdateBuffer(m_bloomCompositeBuffer.Get(), &m_bloomComposite);
		DirectX11::PSSetConstantBuffer(0, 1, m_bloomCompositeBuffer.GetAddressOf());

		DirectX11::Draw(4, 0);

		DirectX11::PSSetShaderResources(0, 1, &nullSRV);
		DirectX11::UnbindRenderTargets();

		DirectX11::CopyResource(m_CopiedTexture->m_pTexture, m_BloomResult->m_pTexture);
	}
}

void PostProcessingPass::GaussianBlurComputeKernel()
{
	float sigma = 5.f;
	float sigmaRcp = 1.f / sigma;
	float twoSigmaSq = 2 * sigma * sigma;

	float sum = 0.f;
	for (size_t i = 0; i <= GAUSSIAN_BLUR_RADIUS; ++i)
	{
		m_bloomBlur.coefficients[i] = (1.f / sigma) * std::expf(-static_cast<float>(i * i) / twoSigmaSq);
		sum += 2 * m_bloomBlur.coefficients[i];
	}
	sum -= m_bloomBlur.coefficients[0];
	float normalizationFactor = 1.f / sum;
	for (size_t i = 0; i <= GAUSSIAN_BLUR_RADIUS; ++i)
	{
		m_bloomBlur.coefficients[i] *= normalizationFactor;
	}
}

void PostProcessingPass::Resize(uint32_t width, uint32_t height)
{
}
