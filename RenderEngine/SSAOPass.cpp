#include "SSAOPass.h"
#include "AssetSystem.h"

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

	m_SSAOBuffer = DirectX11::CreateBuffer(sizeof(SSAOBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

void SSAOPass::Execute(Scene& scene)
{
}
