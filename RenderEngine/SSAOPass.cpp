#include "SSAOPass.h"
#include "AssetSystem.h"
#include "Scene.h"
#include <random>

SSAOPass::SSAOPass()
{
}

SSAOPass::~SSAOPass()
{
}

void SSAOPass::Initialize(Texture* renderTarget, ID3D11ShaderResourceView* depth, Texture* normal)
{
	m_DepthSRV = depth;
	m_NormalTexture = normal;
	m_RenderTarget = renderTarget;

	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_vertexShader = &AssetsSystems->VertexShaders["Fullscreen"];
	m_pso->m_pixelShader = &AssetsSystems->PixelShaders["SSAO"];

    m_Buffer = DirectX11::CreateBuffer(sizeof(SSAOBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);

    std::uniform_real_distribution<float> randomFloats(0.0, 1.0); // random floats between 0.0 - 1.0
    std::default_random_engine generator;

    for (int i = 0; i < 64; ++i)
    {
        XMVECTOR sample = XMVectorSet(
            randomFloats(generator) * 2.0 - 1.0,
            randomFloats(generator) * 2.0 - 1.0,
            randomFloats(generator),
            0
        );
        sample = XMVector3Normalize(sample);
        float scale = (float)i / 64.0;
        // to lerp
        XMVectorScale(sample, scale);

        XMStoreFloat4(&m_SSAOBuffer.m_SampleKernel[i], sample);
    }

    using namespace DirectX::PackedVector;
    std::vector<XMBYTEN4> rotation;
    rotation.reserve(16);
    for (int i = 0; i < 16; ++i)
    {
        XMVECTOR rot = XMVectorSet(
            randomFloats(generator) * 2.0 - 1.0,
            randomFloats(generator) * 2.0 - 1.0,
            0,
            0
        );
        XMBYTEN4 rotbyte;
        XMStoreByteN4(&rotbyte, rot);
        rotation.push_back(rotbyte);
    }

    D3D11_SUBRESOURCE_DATA data;
    data.pSysMem = rotation.data();
    data.SysMemPitch = 4 * 4; // 4 pixels width, 4 bytes (32 bits)
    Texture* tex = Texture::Create(4, 4, "Noise Tex", DXGI_FORMAT_R8G8B8A8_SNORM, D3D11_BIND_SHADER_RESOURCE, &data);
    tex->CreateSRV(DXGI_FORMAT_R8G8B8A8_SNORM);
    m_NoiseTexture = std::unique_ptr<Texture>(tex);
}

void SSAOPass::Execute(Scene& scene)
{
	auto& deviceContext = DeviceState::g_pDeviceContext;

	DirectX11::ClearRenderTargetView(m_RenderTarget->GetRTV(), Colors::Transparent);

	ID3D11RenderTargetView* rtv = m_RenderTarget->GetRTV();
	deviceContext->OMSetRenderTargets(1, &rtv, nullptr);

	PerspacetiveCamera& camera = scene.m_MainCamera;
	Mathf::xMatrix view = camera.CalculateView();
	Mathf::xMatrix proj = camera.CalculateProjection();
}
