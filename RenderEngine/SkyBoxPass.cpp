#include "SkyBoxPass.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Camera.h"
#include "ImGuiRegister.h"

Mathf::xVector forward[6] =
{
    { 1, 0, 0, 0 },
    {-1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0,-1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0,-1, 0 },
};

Mathf::xVector up[6] =
{
	{ 0, 1, 0, 0 },
	{ 0, 1, 0, 0 },
	{ 0, 0,-1, 0 },
	{ 0, 0, 1, 0 },
	{ 0, 1, 0, 0 },
	{ 0, 1, 0, 0 },
};

struct alignas(16) PrefilterCBuffer
{
	float m_roughness;
};

SkyBoxPass::SkyBoxPass()
{
	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Skybox"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Skybox"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	m_fullscreenVS = &ShaderSystem->VertexShaders["Fullscreen"];
	m_irradiancePS = &ShaderSystem->PixelShaders["IrradianceMap"];
	m_prefilterPS = &ShaderSystem->PixelShaders["SpecularPreFilter"];
	m_brdfPS = &ShaderSystem->PixelShaders["IntegrateBRDF"];
	m_rectToCubeMapPS = &ShaderSystem->PixelShaders["RectToCubeMap"];

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
	auto clampSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);
	m_pso->m_samplers.push_back(clampSampler);
}

SkyBoxPass::~SkyBoxPass()
{
}

void SkyBoxPass::Initialize(std::string_view fileName, float size)
{
	m_fileName = fileName;
	m_size = size;

    std::vector<uint32> skyboxIndices =
    {
        0,  1,  2,  0,  2,  3,
        4,  5,  6,  4,  6,  7,
        8,  9, 10,  8, 10, 11,
       12, 13, 14, 12, 14, 15,
       16, 17, 18, 16, 18, 19,
       20, 21, 22, 20, 22, 23
    };

    m_skyBoxMesh = std::make_unique<Mesh>("skyBoxMesh", PrimitiveCreator::CubeVertices(), std::move(skyboxIndices));
	m_scaleMatrix = XMMatrixScaling(m_size, m_size, m_size);

	file::path path = file::path(fileName);
    if (file::exists(path))
    {
        if (path.extension() == ".dds")
        {
			m_skyBoxCubeMap = Texture::LoadManagedFromPath(fileName);
			m_cubeMapGenerationRequired = false;
        }
        else
        {
			m_skyBoxTexture = Texture::LoadManagedFromPath(fileName);
            m_skyBoxCubeMap = Texture::CreateManagedCube(
                m_cubeMapSize,
                "CubeMap",
                DXGI_FORMAT_R16G16B16A16_FLOAT,
				D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
            );

			m_skyBoxCubeMap->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_SRV_DIMENSION_TEXTURECUBE);
			m_skyBoxCubeMap->CreateCubeRTVs(DXGI_FORMAT_R16G16B16A16_FLOAT);
			m_cubeMapGenerationRequired = true;
        }
    }

	m_EnvironmentMap = Texture::CreateSharedCube(
        m_cubeMapSize,
		"EnvironmentMap",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
	);

	m_EnvironmentMap->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_SRV_DIMENSION_TEXTURECUBE);
	m_EnvironmentMap->CreateCubeRTVs(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_SpecularMap = Texture::CreateSharedCube(
		m_cubeMapSize,
		"SpecularMap",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
		6
	);

	m_SpecularMap->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_SRV_DIMENSION_TEXTURECUBE, 6);
	m_SpecularMap->CreateCubeRTVs(DXGI_FORMAT_R16G16B16A16_FLOAT, 6);

	m_BRDFLUT = Texture::CreateShared(
		512,
		512,
		"BRDF_LUT",
        DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
	);

	m_BRDFLUT->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_BRDFLUT->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void SkyBoxPass::SetRenderTarget(Texture* renderTarget)
{
	m_RenderTarget = renderTarget;
}

void SkyBoxPass::SetBackBuffer(ID3D11RenderTargetView* backBuffer)
{
	m_backBuffer = backBuffer;
}

void SkyBoxPass::GenerateCubeMap(RenderScene& scene)
{
	if (!m_cubeMapGenerationRequired)
	{
		return;
	}

	m_scaleMatrix = XMMatrixScaling(m_size, m_size, m_size);

    auto deviceContext = DeviceState::g_pDeviceContext;
    D3D11_VIEWPORT viewport = { 0 };
    viewport.Width = m_cubeMapSize;
    viewport.Height = m_cubeMapSize;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    deviceContext->RSSetViewports(1, &viewport);

	//Camera ortho(true, true);
	DirectX11::IASetInputLayout(m_pso->m_inputLayout);
	DirectX11::VSSetShader(m_pso->m_vertexShader->GetShader(), nullptr, 0);
	DirectX11::PSSetShader(m_rectToCubeMapPS->GetShader(), nullptr, 0);
	DirectX11::PSSetShaderResources(0, 1, &m_skyBoxTexture->m_pSRV);
	scene.UseModel();

    for (int i = 0; i < 6; ++i)
    {
		ID3D11RenderTargetView* rtv = m_skyBoxCubeMap->GetRTV(i);
        DirectX11::OMSetRenderTargets(1, &rtv, nullptr);

        ortho.m_eyePosition = XMVectorSet(0, 0, 0, 0);
        ortho.m_lookAt = forward[i];
        ortho.m_up = up[i];
        ortho.m_nearPlane = 0.f;
        ortho.m_farPlane = 10.f;
        ortho.m_viewHeight = 2.f;
        ortho.m_viewWidth = 2.f;
		ortho.m_isOrthographic = true;

		ortho.UpdateBuffer();
        scene.UpdateModel(XMMatrixIdentity());

        m_skyBoxMesh->Draw();
    }

    DirectX11::InitSetUp();
	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets();
    m_cubeMapGenerationRequired = false;

    GenerateEnvironmentMap(scene);
}

void SkyBoxPass::GenerateCubeMap(std::string_view fileName, RenderScene& scene)
{
	m_fileName = fileName;
	m_skyBoxTexture.reset();

	m_skyBoxTexture = Texture::LoadManagedFromPath(fileName);
	m_skyBoxCubeMap = Texture::CreateManagedCube(
		m_cubeMapSize,
		"CubeMap",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
	);

	m_skyBoxCubeMap->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_SRV_DIMENSION_TEXTURECUBE);
	m_skyBoxCubeMap->CreateCubeRTVs(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_cubeMapGenerationRequired = true;

	GenerateCubeMap(scene);
}

Managed::SharedPtr<Texture> SkyBoxPass::GenerateEnvironmentMap(RenderScene& scene)
{
	auto deviceContext = DeviceState::g_pDeviceContext;
	D3D11_VIEWPORT viewport = { 0 };
	viewport.Width = m_cubeMapSize;
	viewport.Height = m_cubeMapSize;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	deviceContext->RSSetViewports(1, &viewport);

	//Camera ortho(true, false);
	DirectX11::IASetInputLayout(m_pso->m_inputLayout);
	DirectX11::VSSetShader(m_pso->m_vertexShader->GetShader(), nullptr, 0);
	DirectX11::PSSetShader(m_irradiancePS->GetShader(), nullptr, 0);
	DirectX11::PSSetShaderResources(0, 1, &m_skyBoxCubeMap->m_pSRV);
	deviceContext->PSSetSamplers(2, 1, &m_pso->m_samplers[2]->m_SamplerState);
	scene.UseModel();

	for (int i = 0; i < 6; ++i)
	{
		ID3D11RenderTargetView* rtv = m_EnvironmentMap->GetRTV(i);
		DirectX11::OMSetRenderTargets(1, &rtv, nullptr);

		ortho.m_eyePosition = XMVectorSet(0.f, 0.f, 0.f, 0.f);
		ortho.m_lookAt = forward[i];
		ortho.m_up = up[i];
		ortho.m_nearPlane = 0.f;
		ortho.m_farPlane = 10.f;
		ortho.m_viewHeight = 2.f;
		ortho.m_viewWidth = 2.f;
		ortho.m_isOrthographic = true;

		ortho.UpdateBuffer();
		scene.UpdateModel(XMMatrixIdentity());
		m_skyBoxMesh->Draw();
		deviceContext->Flush();
	}

	DirectX11::InitSetUp();
	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets();

	return m_EnvironmentMap;
}

Managed::SharedPtr<Texture> SkyBoxPass::GeneratePrefilteredMap(RenderScene& scene)
{
	int mapSize = m_cubeMapSize;

	ID3D11Buffer* buffer = DirectX11::CreateBuffer(
		sizeof(PrefilterCBuffer),
		D3D11_BIND_CONSTANT_BUFFER,
		nullptr
	);

	PrefilterCBuffer cBuffer;

	auto deviceContext = DeviceState::g_pDeviceContext;
	//Camera ortho(true, false);

	deviceContext->PSSetConstantBuffers(0, 1, &buffer);
	DirectX11::IASetInputLayout(m_pso->m_inputLayout);
	DirectX11::VSSetShader(m_pso->m_vertexShader->GetShader(), nullptr, 0);
	DirectX11::PSSetShader(m_prefilterPS->GetShader(), nullptr, 0);
	DirectX11::PSSetShaderResources(0, 1, &m_skyBoxCubeMap->m_pSRV);
	scene.UseModel();

	for (int i = 0; i < 6; ++i)
	{
		cBuffer.m_roughness = (float)i / 5.f;
		DirectX11::UpdateBuffer(buffer, &cBuffer);

		D3D11_VIEWPORT viewport = { 0 };
		viewport.Width = mapSize;
		viewport.Height = mapSize;
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		deviceContext->RSSetViewports(1, &viewport);

		for (int j = 0; j < 6; ++j)
		{
			ID3D11RenderTargetView* rtv = m_SpecularMap->GetRTV(i * 6 + j);
			DirectX11::OMSetRenderTargets(1, &rtv, nullptr);

			ortho.m_eyePosition = XMVectorSet(0, 0, 0, 0);
			ortho.m_lookAt = forward[j];
			ortho.m_up = up[j];
			ortho.m_nearPlane = 0;
			ortho.m_farPlane = 10;
			ortho.m_viewHeight = 2;
			ortho.m_viewWidth = 2;
			ortho.m_isOrthographic = true;

			ortho.UpdateBuffer();
			scene.UpdateModel(XMMatrixIdentity());
			m_skyBoxMesh->Draw();
		}

		mapSize /= 2;
	}

	DirectX11::InitSetUp();
	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets();
	Memory::SafeDelete(buffer);

	return m_SpecularMap;
}

Managed::SharedPtr<Texture> SkyBoxPass::GenerateBRDFLUT(RenderScene& scene)
{
	D3D11_VIEWPORT viewport = { 0 };
	viewport.Width = 512;
	viewport.Height = 512;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	auto deviceContext = DeviceState::g_pDeviceContext;
	deviceContext->RSSetViewports(1, &viewport);

	ID3D11RenderTargetView* rtv = m_BRDFLUT->GetRTV();
	DirectX11::OMSetRenderTargets(1, &rtv, nullptr);

	DirectX11::IASetInputLayout(m_pso->m_inputLayout);
	DirectX11::VSSetShader(m_fullscreenVS->GetShader(), nullptr, 0);
	DirectX11::PSSetShader(m_brdfPS->GetShader(), nullptr, 0);
	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	DirectX11::Draw(4, 0);

	DirectX11::InitSetUp();
	DirectX11::UnbindRenderTargets();

	return m_BRDFLUT;
}

void SkyBoxPass::Execute(RenderScene& scene, Camera& camera)
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

void SkyBoxPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	if (!m_abled || !RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	ID3D11DeviceContext* deferredPtr = deferredContext;

	m_pso->Apply(deferredPtr);

	ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(deferredPtr, 1, &rtv, renderData->m_depthStencil->m_pDSV);
	DirectX11::RSSetViewports(deferredPtr, 1, &DeviceState::g_Viewport);
	camera.UpdateBuffer(deferredPtr);
	scene.UseModel(deferredPtr);

	m_scaleMatrix = XMMatrixScaling(m_scale, m_scale, m_scale);
	//auto modelMatrix = XMMatrixMultiply(m_scaleMatrix, XMMatrixTranslationFromVector(scene.m_MainCamera.m_eyePosition));
	auto modelMatrix = XMMatrixMultiply(m_scaleMatrix, XMMatrixTranslationFromVector(camera.m_eyePosition));

	scene.UpdateModel(modelMatrix, deferredPtr);
	DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &m_skyBoxCubeMap->m_pSRV);
	m_skyBoxMesh->Draw(deferredPtr);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets(deferredPtr);

	ID3D11CommandList* commandList{};
	deferredPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void SkyBoxPass::ControlPanel()
{
	ImGui::Text("SkyBoxPass");
	ImGui::Checkbox("Enable", &m_abled);
	ImGui::SliderFloat("scale", &m_scale, 1.f, 1000.f);
}

void SkyBoxPass::Resize(uint32_t width, uint32_t height)
{
	m_skyBoxTexture.reset();

	m_skyBoxTexture = Texture::LoadManagedFromPath(m_fileName);
	m_skyBoxCubeMap = Texture::CreateManagedCube(
		m_cubeMapSize,
		"CubeMap",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
	);

	m_skyBoxCubeMap->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_SRV_DIMENSION_TEXTURECUBE);
	m_skyBoxCubeMap->CreateCubeRTVs(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_cubeMapGenerationRequired = true;
}
