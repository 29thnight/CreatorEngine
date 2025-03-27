#include "PostProcessingPass.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"

#pragma warning(disable: 2398)
PostProcessingPass::PostProcessingPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	m_pBloomDownSampledCS = &AssetsSystems->ComputeShaders["BloomDownSampled"];
	m_pGaussianBlur6x6_AxisX_CS = &AssetsSystems->ComputeShaders["GaussianBlur6x6_AxisX"];
	m_pGaussianBlur6x6_AxisY_CS = &AssetsSystems->ComputeShaders["GaussianBlur6x6_AxisY"];
	m_pGaussianBlur11x11_AxisX_CS = &AssetsSystems->ComputeShaders["GaussianBlur11x11_AxisX"];
	m_pGaussianBlur11x11_AxisY_CS = &AssetsSystems->ComputeShaders["GaussianBlur11x11_AxisY"];
	
	m_BloomBuffer = DirectX11::CreateBuffer(sizeof(BloomBuffer), D3D11_BIND_CONSTANT_BUFFER, &m_BloomConstant);
	m_DownSampledBuffer = DirectX11::CreateBuffer(sizeof(DownSampledBuffer), D3D11_BIND_CONSTANT_BUFFER, &m_DownSampledConstant);
	m_BlurBuffer = DirectX11::CreateBuffer(sizeof(BlurBuffer), D3D11_BIND_CONSTANT_BUFFER, &m_BlurConstant);
	m_UpsampleBuffer = DirectX11::CreateBuffer(sizeof(UpsampleBuffer), D3D11_BIND_CONSTANT_BUFFER, &m_UpsampleConstant);

	TextureInitialization();

}

PostProcessingPass::~PostProcessingPass()
{
	Memory::SafeDelete(m_BloomDownSampledTexture1_StepX);
	Memory::SafeDelete(m_BloomDownSampledTexture1_StepY);
	Memory::SafeDelete(m_BloomDownSampledTexture2_StepX);
	Memory::SafeDelete(m_BloomDownSampledTexture2_StepY);
	Memory::SafeDelete(m_BloomDownSampledTextureFinal_StepX);
	Memory::SafeDelete(m_BloomDownSampledTextureFinal_StepY);
	Memory::SafeDelete(m_CopiedTexture);
}

void PostProcessingPass::Execute(Scene& scene, Camera& camera)
{
	if (m_PostProcessingApply.m_Bloom)
	{
		BloomPass(scene, camera);
	}
}

void PostProcessingPass::ControlPanel()
{
}

void PostProcessingPass::TextureInitialization()
{
	m_CopiedTexture = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE
	);

	//Bloom Pass
	m_ApplyBloomCurveTexture = Texture::Create(
		BloomBufferWidth,
		BloomBufferHeight,
		"BloomCurveTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE
	);

	m_BloomDownSampledTexture1_StepX = Texture::Create(
		BloomBufferWidth,
		BloomBufferHeight,
		"BloomDownSampledTexture1",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
	);

	m_BloomDownSampledTexture1_StepY = Texture::Create(
		BloomBufferWidth,
		BloomBufferHeight,
		"BloomDownSampledTexture2",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
	);

	m_BloomDownSampledTexture2_StepX = Texture::Create(
		BloomBufferWidth / 2,
		BloomBufferHeight / 2,
		"BloomDownSampledTexture3",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
	);

	m_BloomDownSampledTexture2_StepY = Texture::Create(
		BloomBufferWidth / 2,
		BloomBufferHeight / 2,
		"BloomDownSampledTexture4",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
	);

	m_BloomDownSampledTextureFinal_StepX = Texture::Create(
		BloomBufferWidth / 4,
		BloomBufferHeight / 4,
		"BloomDownSampledTextureFinal",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
	);

	m_BloomDownSampledTextureFinal_StepY = Texture::Create(
		BloomBufferWidth / 4,
		BloomBufferHeight / 4,
		"BloomDownSampledTextureFinal",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
	);
}

void PostProcessingPass::BloomPass(Scene& scene, Camera& camera)
{
	DirectX11::CopyResource(m_CopiedTexture->m_pTexture, camera.m_renderTarget->m_pTexture);

	DirectX11::CSSetShader(m_pBloomDownSampledCS->GetShader(), nullptr, 0);
	DirectX11::CSSetShaderResources(0, 1, &m_CopiedTexture->m_pSRV);
	DirectX11::CSSetUnorderedAccessViews(0, 1, &m_ApplyBloomCurveTexture->m_pUAV, nullptr);

	m_DownSampledConstant.inputSize = { (int)DeviceState::g_ClientRect.width, (int)DeviceState::g_ClientRect.height };
	m_DownSampledConstant.outputSize = { (int)BloomBufferWidth, (int)BloomBufferHeight };

	DirectX11::UpdateBuffer(m_DownSampledBuffer.Get(), &m_DownSampledConstant);

	uint32 threadGroupCountX = (uint32)ceilf(BloomBufferHeight + 7 / 8.0f);
	uint32 threadGroupCountY = (uint32)ceilf(BloomBufferWidth + 7 / 8.0f);

	DirectX11::Dispatch(threadGroupCountX, threadGroupCountY, 1);

	DirectX11::CSSetUnorderedAccessViews(0, 1, nullptr, nullptr);
	DirectX11::CSSetShaderResources(0, 1, nullptr);

	DirectX11::CopyResource(m_BloomDownSampledTexture1_StepX->m_pTexture, m_ApplyBloomCurveTexture->m_pTexture);
	//Step 1 ----------------------------------------------

	DirectX11::CSSetShader(m_pGaussianBlur6x6_AxisX_CS->GetShader(), nullptr, 0);
	DirectX11::CSSetShaderResources(0, 1, &m_BloomDownSampledTexture1_StepX->m_pSRV);
	DirectX11::CSSetUnorderedAccessViews(0, 1, &m_BloomDownSampledTexture1_StepY->m_pUAV, nullptr);

	m_BlurConstant.texSize = { int(BloomBufferWidth / 2), int(BloomBufferHeight / 2) };

	DirectX11::UpdateBuffer(m_BlurBuffer.Get(), &m_BlurConstant);

	threadGroupCountX = (uint32)ceilf((BloomBufferHeight / 2.f) + 7 / 8.0f);
	threadGroupCountY = (uint32)ceilf((BloomBufferWidth / 2.f) + 7 / 8.0f);

	DirectX11::Dispatch(threadGroupCountX, threadGroupCountY, 1);

	DirectX11::CSSetUnorderedAccessViews(0, 1, nullptr, nullptr);
	DirectX11::CSSetShaderResources(0, 1, nullptr);

	DirectX11::CSSetShader(m_pGaussianBlur6x6_AxisY_CS->GetShader(), nullptr, 0);
	DirectX11::CSSetShaderResources(0, 1, &m_BloomDownSampledTexture1_StepY->m_pSRV);
	DirectX11::CSSetUnorderedAccessViews(0, 1, &m_BloomDownSampledTexture1_StepX->m_pUAV, nullptr);
	
	m_BlurConstant.texSize = { int(BloomBufferWidth / 2), int(BloomBufferHeight / 2) };

	DirectX11::UpdateBuffer(m_BlurBuffer.Get(), &m_BlurConstant);

	threadGroupCountX = (uint32)ceilf((BloomBufferHeight / 2.f) + 7 / 8.0f);
	threadGroupCountY = (uint32)ceilf((BloomBufferWidth / 2.f) + 7 / 8.0f);

	DirectX11::Dispatch(threadGroupCountX, threadGroupCountY, 1);

	DirectX11::CSSetUnorderedAccessViews(0, 1, nullptr, nullptr);
	DirectX11::CSSetShaderResources(0, 1, nullptr);

	//Step 2 ----------------------------------------------

	DirectX11::CSSetShader(m_pGaussianBlur6x6_AxisX_CS->GetShader(), nullptr, 0);
	DirectX11::CSSetShaderResources(0, 1, &m_BloomDownSampledTexture1_StepX->m_pSRV);
	DirectX11::CSSetUnorderedAccessViews(0, 1, &m_BloomDownSampledTexture2_StepY->m_pUAV, nullptr);



}
