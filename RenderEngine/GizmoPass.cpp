#include "GizmoPass.h"
#include "ShaderSystem.h"
#include "RenderScene.h"
#include "RenderableComponents.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "Scene.h"
#include "GizmoCbuffer.h"

constexpr int MAIN_LIGHT_INDEX = 0;

GizmoPass::GizmoPass()
{
    file::path iconpath = PathFinder::IconPath();

    MainLightIcon = Texture::LoadFormPath(iconpath.string() + "MainLightGizmo.png");
    PointLightIcon = Texture::LoadFormPath(iconpath.string() + "PointLightGizmo.png");
    SpotLightIcon = Texture::LoadFormPath(iconpath.string() + "SpotLightGizmo.png");
    DirectionalLightIcon = Texture::LoadFormPath(iconpath.string() + "DirectionalLightGizmo.png");
    CameraIcon = Texture::LoadFormPath(iconpath.string() + "CameraGizmo.png");

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
		DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
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
		DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(
			&depthStencilDesc,
			&m_NoWriteDepthStencilState
		)
	);

	m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();
	m_pso->m_blendState = DirectX11::DeviceStates->g_pBlendState;

	m_positionBuffer = DirectX11::CreateBuffer(sizeof(GizmoPos), D3D11_BIND_VERTEX_BUFFER, nullptr);
	DirectX::SetName(m_positionBuffer.Get(), "GizmoPositionBuffer");
	m_sizeBuffer = DirectX11::CreateBuffer(sizeof(GizmoSize), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	DirectX::SetName(m_sizeBuffer.Get(), "GizmoSizeBuffer");
	m_gizmoCameraBuffer = DirectX11::CreateBuffer(sizeof(GizmoCameraBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	DirectX::SetName(m_gizmoCameraBuffer.Get(), "GizmoCameraBuffer");
}

GizmoPass::~GizmoPass()
{
	delete MainLightIcon;
	delete PointLightIcon;
	delete SpotLightIcon;
	delete DirectionalLightIcon;
	delete CameraIcon;
}

void GizmoPass::Execute(RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	m_pso->Apply();

	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

	ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &rtv, nullptr);

	DirectX11::OMSetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
	DirectX11::OMSetBlendState(DirectX11::DeviceStates->g_pBlendState, nullptr, 0xFFFFFFFF);

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

	std::vector<std::pair<int, std::shared_ptr<GameObject>>> gizmoTargetComponent;
    gizmoTargetComponent.reserve(scene.GetScene()->m_SceneObjects.size());

    for (auto& object : scene.GetScene()->m_SceneObjects)
    {
        if (object->GetComponent<CameraComponent>() != nullptr)
        {
            gizmoTargetComponent.emplace_back(0, object);
        }
        else if (object->GetComponent<LightComponent>() != nullptr)
        {
            gizmoTargetComponent.emplace_back(1, object);
        }
    }

	GizmoSize size{ .size = 1.f };
    Texture* icon = nullptr;
	for (auto& [type, object] : gizmoTargetComponent)
	{
		switch (type)
		{
		case 0:
		{
			icon = CameraIcon;
			break;
		}
		case 1:
		{
			bool isMainLight = false;
			auto lightComponent = object->GetComponent<LightComponent>();
			if (!lightComponent) continue;

			isMainLight = (MAIN_LIGHT_INDEX == lightComponent->m_lightIndex);
			icon = GetLightIcon(lightComponent->m_lightType, isMainLight);
			break;
		}
		default:
			continue;
		}

		if (nullptr != icon)
		{
			DirectX11::PSSetShaderResources(0, 1, &icon->m_pSRV);
		}
		GizmoPos _pos{ .pos = Mathf::Vector3(object->m_transform.GetWorldPosition()) };
		_pos.pos.y -= 0.5f;
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

void GizmoPass::Resize(uint32_t width, uint32_t height)
{
}

Texture* GizmoPass::GetLightIcon(int lightType, bool isMainLight) const
{
    switch (lightType)
    {
    case DirectionalLight:
        return isMainLight ? DataSystems->MainLightIcon : DataSystems->DirectionalLightIcon;
    case PointLight:
        return DataSystems->PointLightIcon;
    case SpotLight:
        return DataSystems->SpotLightIcon;
    default:
        return nullptr;
    }
}
