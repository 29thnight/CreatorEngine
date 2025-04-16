#include "SpritePass.h"
#include "ShaderSystem.h"
#include "RenderScene.h"
#include "RenderableComponents.h"
#include "Scene.h"

SpritePass::SpritePass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Sprite"];

	InputLayOutContainer vertexLayoutDesc = {
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	m_QuadMesh = std::make_unique<Mesh>(
		"Quad", 
		PrimitiveCreator::QuadVertices(), 
		PrimitiveCreator::QuadIndices()
	);

	CD3D11_DEPTH_STENCIL_DESC depthStencilDesc{ CD3D11_DEFAULT() };
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilState(
			&depthStencilDesc,
			&m_NoWriteDepthStencilState
		)
	);

	m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();
	m_pso->m_blendState = DeviceState::g_pBlendState;
}

SpritePass::~SpritePass()
{
}

void SpritePass::Initialize(Texture* renderTarget)
{
	m_RenderTarget = renderTarget;
}

void SpritePass::Execute(RenderScene& scene, Camera& camera)
{
	if (false == camera.m_applyRenderPipelinePass.m_SpritePass)
	{
		return;
	}

	auto deviceContext = DeviceState::g_pDeviceContext;
	m_pso->Apply();

	ID3D11RenderTargetView* rtv = camera.m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &rtv, camera.m_depthStencil->m_pDSV);

	deviceContext->OMSetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
	deviceContext->OMSetBlendState(DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);


	camera.UpdateBuffer();
    scene.UseModel();

    std::vector<std::shared_ptr<GameObject>> sprites;
	std::copy_if(
		scene.GetScene()->m_SceneObjects.begin(),
		scene.GetScene()->m_SceneObjects.end(),
		std::back_inserter(sprites),
		[](const std::shared_ptr<GameObject>& object)
		{
			return nullptr != object->GetComponent<SpriteRenderer>() && object->GetComponent<SpriteRenderer>()->IsEnabled();
		}
	);

	std::vector<std::tuple<Transform*, SpriteRenderer*, float>> spriteInfos;
	Mathf::xVector& eyePosition = camera.m_eyePosition;
	std::transform(
		sprites.begin(),
		sprites.end(),
		std::back_inserter(spriteInfos),
		[&eyePosition](const std::shared_ptr<GameObject>& object)
		{
			Transform* pTransform = &(object->m_transform);
			float squaredLength{};
			XMVectorGetByIndexPtr(&squaredLength, XMVector3LengthSq(eyePosition - pTransform->GetWorldPosition()), 0);
			return std::make_tuple(pTransform, object->GetComponent<SpriteRenderer>(), squaredLength);
		}
	);

	std::sort(
		spriteInfos.begin(),
		spriteInfos.end(),
		[](auto& lhs, auto& rhs)
		{
			return std::get<float>(lhs) < std::get<float>(rhs);
		}
	);

	for (auto& [transform, spriteRenderer, squaredLength] : spriteInfos)
	{
		scene.UpdateModel(transform->GetWorldMatrix());
		DirectX11::PSSetShaderResources(0,1,&spriteRenderer->m_Sprite->m_pSRV);
		m_QuadMesh->Draw();
	}

	DirectX11::OMSetDepthStencilState(DeviceState::g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets();
}

void SpritePass::ControlPanel()
{
}

void SpritePass::Resize()
{
}
