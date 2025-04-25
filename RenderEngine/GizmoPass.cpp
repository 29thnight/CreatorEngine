#include "GizmoPass.h"
#include "ShaderSystem.h"
#include "RenderScene.h"
#include "RenderableComponents.h"
#include "Scene.h"

cbuffer GizmoCameraBuffer
{
	Mathf::xMatrix VP{};
	float3 eyePosition{};
};

cbuffer GizmoPos
{
	float3 pos{};
};

cbuffer GizmoSize
{
	float size{};
};

GizmoPass::GizmoPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Gizmo_billboard"];
	m_pso->m_geometryShader = &ShaderSystem->GeometryShaders["Gizmo_billboard"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Gizmo_billboard"];

	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;

	InputLayOutContainer vertexLayoutDesc = {
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

	m_positionBuffer = DirectX11::CreateBuffer(sizeof(GizmoPos), D3D11_BIND_VERTEX_BUFFER, nullptr);
	DirectX::SetName(m_positionBuffer.Get(), "GizmoPositionBuffer");
	m_sizeBuffer = DirectX11::CreateBuffer(sizeof(GizmoSize), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	DirectX::SetName(m_sizeBuffer.Get(), "GizmoSizeBuffer");
	m_gizmoCameraBuffer = DirectX11::CreateBuffer(sizeof(GizmoCameraBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	DirectX::SetName(m_gizmoCameraBuffer.Get(), "GizmoCameraBuffer");
}

void GizmoPass::Execute(RenderScene& scene, Camera& camera)
{
	auto deviceContext = DeviceState::g_pDeviceContext;
	m_pso->Apply();

	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

	ID3D11RenderTargetView* rtv = camera.m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &rtv, camera.m_depthStencil->m_pDSV);

	deviceContext->OMSetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
	deviceContext->OMSetBlendState(DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);

	GizmoCameraBuffer cameraBuffer{ 
		.VP = XMMatrixMultiply(camera.CalculateView(), camera.CalculateProjection()), 
		.eyePosition = Mathf::Vector3(camera.m_eyePosition) 
	};

	const uint32 stride = sizeof(GizmoPos);
	const uint32 offset = 0;

	DirectX11::IASetVertexBuffers(0, 1, m_positionBuffer.GetAddressOf(), &stride, &offset);
	DirectX11::GSSetConstantBuffer(0, 1, m_gizmoCameraBuffer.GetAddressOf());
	DirectX11::GSSetConstantBuffer(1, 1, m_sizeBuffer.GetAddressOf());

	DirectX11::UpdateBuffer(m_gizmoCameraBuffer.Get(), &cameraBuffer);

	std::vector<std::shared_ptr<GameObject>> sprites;
	std::copy_if(
		scene.GetScene()->m_SceneObjects.begin(),
		scene.GetScene()->m_SceneObjects.end(),
		std::back_inserter(sprites),
		[](const std::shared_ptr<GameObject>& object)
		{
			return nullptr != object->GetComponent<SpriteRenderer>()
				&& object->GetComponent<SpriteRenderer>()->IsEnabled()
				&& object->GetComponent<SpriteRenderer>()->IsGizmoEnabled();
		}
	);

	GizmoSize size{ .size = 1.f };
	for (auto& sprite : sprites)
	{
		auto spriteRenderer = sprite->GetComponent<SpriteRenderer>();
		if (spriteRenderer == nullptr) continue;
		if (spriteRenderer->m_Sprite == nullptr) continue;

		DirectX11::PSSetShaderResources(0, 1, &spriteRenderer->m_Sprite->m_pSRV);
		GizmoPos _pos{ .pos = Mathf::Vector3(sprite->m_transform.GetWorldPosition()) };
		DirectX11::UpdateBuffer(m_positionBuffer.Get(), &_pos);
		DirectX11::UpdateBuffer(m_sizeBuffer.Get(), &size);

		DirectX11::Draw(1, 0);
	}

	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D11Buffer* nullBuffer = nullptr;
	DirectX11::IASetVertexBuffers(0, 1, &nullBuffer, &stride, &offset);
	DirectX11::GSSetConstantBuffer(0, 1, &nullBuffer);
	DirectX11::GSSetConstantBuffer(1, 1, &nullBuffer);

	m_pso->Reset();
}

void GizmoPass::ControlPanel()
{
}

void GizmoPass::Resize()
{
}
