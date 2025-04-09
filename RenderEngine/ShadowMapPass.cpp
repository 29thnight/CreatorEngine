#include "ShadowMapPass.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"
#include "Renderer.h"
#include "Light.h"

ShadowMapPass::ShadowMapPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["ShadowMap"];

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
	/*
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_BACK;
		rasterizerDesc.DepthBias = 1500;
		rasterizerDesc.SlopeScaledDepthBias = 1.0f;
		rasterizerDesc.DepthBiasClamp = 0.0f;    */

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);


	shadowViewport.TopLeftX = 0;
	shadowViewport.TopLeftY = 0;
	shadowViewport.Width = 2048;
	shadowViewport.Height = 2048;
	shadowViewport.MinDepth = 0.0f;
	shadowViewport.MaxDepth = 1.0f;

}

void ShadowMapPass::Initialize(uint32 width, uint32 height)
{
	Texture* shadowMapTexture = Texture::CreateArray(width, height, "Shadow Map",
		DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 3);


	for (int i = 0; i < cascadeCount; i++)
	{
		CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
		depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		depthStencilViewDesc.Texture2DArray.ArraySize = 1;
		depthStencilViewDesc.Texture2DArray.FirstArraySlice = i;
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateDepthStencilView(
				shadowMapTexture->m_pTexture,
				&depthStencilViewDesc,
				&m_shadowMapDSVarr[i]
			)
		);
	}

	//안에서 배열은 3으로 고정중 필요하면 수정
	shadowMapTexture->CreateSRV(DXGI_FORMAT_R32_FLOAT, D3D11_SRV_DIMENSION_TEXTURE2DARRAY);
	m_shadowMapTexture = std::unique_ptr<Texture>(shadowMapTexture);
	m_shadowCamera.m_isOrthographic = true;
}

void ShadowMapPass::Execute(RenderScene& scene, Camera& camera)
{
	m_pso->Apply();
	if (!m_abled)
	{
		for (int i = 0; i < cascadeCount; i++)
		{
			DirectX11::ClearDepthStencilView(m_shadowMapDSVarr[i], D3D11_CLEAR_DEPTH, 1.0f, 0);
		}
		return;
	}
	Mathf::Vector4 lightdir = scene.m_LightController->GetLight(0).m_direction;
	std::vector<float> cascadeEnd = devideCascadeEnd(camera, { 0.15,0.5 });
	std::vector<ShadowInfo> cascadeinfo = devideShadowInfo(camera, cascadeEnd, lightdir);

	camera.CalculateProjection();
	scene.m_LightController->m_shadowMapConstant.devideShadow = shadowMapConstant2.devideShadow;
	scene.m_LightController->m_shadowMapConstant._epsilon = shadowMapConstant2._epsilon;

	ShadowMapConstant shadowMapConstant = scene.m_LightController->m_shadowMapConstant;
	auto projMatrix = camera.CalculateProjection();
	Mathf::Vector4 transformedCascade;


	shadowMapConstant.m_casCadeEnd1 = Mathf::Vector4::Transform({ 0, 0, cascadeEnd[1], 1.f }, camera.CalculateProjection()).z;
	shadowMapConstant.m_casCadeEnd2 = Mathf::Vector4::Transform({ 0, 0, cascadeEnd[2], 1.f }, camera.CalculateProjection()).z;
	shadowMapConstant.m_casCadeEnd3 = Mathf::Vector4::Transform({ 0, 0, cascadeEnd[3], 1.f }, camera.CalculateProjection()).z;


	for (int i = 0; i < cascadeCount; i++)
	{
		DirectX11::ClearDepthStencilView(m_shadowMapDSVarr[i], D3D11_CLEAR_DEPTH, 1.0f, 0);
		DirectX11::OMSetRenderTargets(0, nullptr, m_shadowMapDSVarr[i]);
		auto desc = scene.m_LightController->m_shadowMapRenderDesc;
		//m_shadowCamera.m_eyePosition = XMLoadFloat4(&(scene.m_LightController->GetLight(0).m_direction)) * -50;
		//m_shadowCamera.m_lookAt = desc.m_lookAt;
		m_shadowCamera.m_eyePosition = cascadeinfo[i].m_eyePosition;
		m_shadowCamera.m_lookAt = cascadeinfo[i].m_lookAt;
		m_shadowCamera.m_viewHeight = cascadeinfo[i].m_viewHeight;
		m_shadowCamera.m_viewWidth = cascadeinfo[i].m_viewWidth;
		m_shadowCamera.m_nearPlane = cascadeinfo[i].m_nearPlane;
		m_shadowCamera.m_farPlane = cascadeinfo[i].m_farPlane;
		DeviceState::g_pDeviceContext->RSSetViewports(1, &shadowViewport);
		DirectX11::PSSetShader(NULL, NULL, 0);

		shadowMapConstant.m_shadowMapWidth = desc.m_textureWidth;
		shadowMapConstant.m_shadowMapHeight = desc.m_textureHeight;
		//shadowMapConstant.m_lightViewProjection[i] = m_shadowCamera.CalculateView() * m_shadowCamera.CalculateProjection(true);
		shadowMapConstant.m_lightViewProjection[i] = cascadeinfo[i].m_lightViewProjection;
		m_shadowCamera.UpdateBuffer(true);
		scene.UseModel();
		for (auto& obj : scene.GetScene()->m_SceneObjects)
		{

			if (obj->ToString() == "Cube" || obj->ToString() == "Plane")
				continue;
			MeshRenderer* meshRenderer = obj->GetComponent<MeshRenderer>();
			if (nullptr == meshRenderer) continue;
			if (!meshRenderer->IsEnabled()) continue;

			scene.UpdateModel(obj->m_transform.GetWorldMatrix());

			meshRenderer->m_Mesh->Draw();
		}
		DirectX11::UpdateBuffer(scene.m_LightController->m_shadowMapBuffer, &shadowMapConstant);
		DeviceState::g_pDeviceContext->RSSetViewports(1, &DeviceState::g_Viewport);
		DirectX11::UnbindRenderTargets();
	}
	scene.m_LightController->m_shadowMapConstant = shadowMapConstant;
}

void ShadowMapPass::ControlPanel()
{
	ImGui::Text("ShadowPass");
	ImGui::Checkbox("Enable2", &m_abled);
	ImGui::Image((ImTextureID)m_shadowMapTexture->m_pSRV, ImVec2(512, 512));
	ImGui::SliderFloat("epsilon", &shadowMapConstant2._epsilon, 0.0001f, 0.03f);
	ImGui::SliderInt("devideShadow", &shadowMapConstant2.devideShadow, 1, 9);
}

void ShadowMapPass::ReloadShaders()
{
	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["ShadowMap"];
	m_pso->m_inputLayout->Release();

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
}

void ShadowMapPass::Resize()
{

}

std::vector<float> devideCascadeEnd(Camera& camera, std::vector<float> ratios)
{
	std::vector<float> cascadeEnds;
	cascadeEnds.reserve(ratios.size() + 2);


	cascadeEnds.push_back(camera.m_nearPlane);

	float distanceZ = camera.m_farPlane - camera.m_nearPlane;

	for (float ratio : ratios)
	{
		cascadeEnds.push_back(ratio * distanceZ);
	}

	cascadeEnds.push_back(camera.m_farPlane);

	return cascadeEnds;
}

std::vector<ShadowInfo> devideShadowInfo(Camera& camera, std::vector<float> cascadeEnd, Mathf::Vector4 LightDir)
{
	auto Fullfrustum = camera.GetFrustum();

	XMFLOAT3 FullfrustumCorners[8];
	Fullfrustum.GetCorners(FullfrustumCorners);
	float nearZ = camera.m_nearPlane;
	float farZ = camera.m_farPlane;


	DirectX::BoundingFrustum frustum(camera.CalculateProjection());
	float frustumDistnace = (frustum.Far - frustum.Near);

	Mathf::Matrix cameraview = camera.CalculateView();
	Mathf::Matrix viewInverse = camera.CalculateInverseView();
	Mathf::Vector3 forwardVec;
	cameraview.Forward(forwardVec);
	Mathf::Vector3 adjustTranslate = forwardVec * frustumDistnace;

	std::array<std::array<	Mathf::Vector3, 8>, cascadeCount> sliceFrustums;

	for (size_t i = 0; i < sliceFrustums.size(); ++i)
	{
		std::array<	Mathf::Vector3, 8>& sliceFrustum = sliceFrustums[i];
		float curEnd = cascadeEnd[i];
		float nextEnd = cascadeEnd[i + 1];

		sliceFrustum[0] = Mathf::Vector3::Transform({ frustum.RightSlope * curEnd, frustum.TopSlope * curEnd, curEnd }, viewInverse) + adjustTranslate;
		sliceFrustum[1] = Mathf::Vector3::Transform({ frustum.RightSlope * curEnd, frustum.BottomSlope * curEnd, curEnd }, viewInverse) + adjustTranslate;
		sliceFrustum[2] = Mathf::Vector3::Transform({ frustum.LeftSlope * curEnd, frustum.TopSlope * curEnd, curEnd }, viewInverse) + adjustTranslate;
		sliceFrustum[3] = Mathf::Vector3::Transform({ frustum.LeftSlope * curEnd, frustum.BottomSlope * curEnd, curEnd }, viewInverse) + adjustTranslate;

		sliceFrustum[4] = Mathf::Vector3::Transform({ frustum.RightSlope * nextEnd, frustum.TopSlope * nextEnd, nextEnd }, viewInverse) + adjustTranslate;
		sliceFrustum[5] = Mathf::Vector3::Transform({ frustum.RightSlope * nextEnd, frustum.BottomSlope * nextEnd, nextEnd }, viewInverse) + adjustTranslate;
		sliceFrustum[6] = Mathf::Vector3::Transform({ frustum.LeftSlope * nextEnd, frustum.TopSlope * nextEnd, nextEnd }, viewInverse) + adjustTranslate;
		sliceFrustum[7] = Mathf::Vector3::Transform({ frustum.LeftSlope * nextEnd, frustum.BottomSlope * nextEnd, nextEnd }, viewInverse) + adjustTranslate;
	}
	std::vector<ShadowInfo> shadowinfo;
	shadowinfo.resize(cascadeCount);
	for (size_t i = 0; i < cascadeCount; ++i)
	{
		const std::array<Mathf::Vector3, 8>& sliceFrustum = sliceFrustums[i];

		DirectX::SimpleMath::Vector3 centerPos = { 0.f, 0.f, 0.f };
		for (const DirectX::SimpleMath::Vector3& pos : sliceFrustum)
		{
			centerPos += pos;
		}
		centerPos /= 8.f;

		float radius = 0.f;
		for (const DirectX::SimpleMath::Vector3& pos : sliceFrustum)
		{
			float distance = DirectX::SimpleMath::Vector3::Distance(centerPos, pos);
			radius = std::max<float>(radius, distance);
		}

		radius = std::ceil(radius * 16.f) / 16.f;

		Mathf::Vector3 maxExtents = { radius, radius, radius };
		Mathf::Vector3 minExtents = -maxExtents;
		DirectX::SimpleMath::Vector3 shadowPos = centerPos + LightDir * minExtents.z;
		Mathf::Vector3 cascadeExtents = maxExtents - minExtents;
		Mathf::xMatrix lightView = DirectX::XMMatrixLookAtLH(shadowPos, centerPos, { 0, 1, 0 });
		Mathf::xMatrix lightProj = DirectX::XMMatrixOrthographicOffCenterLH(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.f, cascadeExtents.z);


		shadowinfo[i].m_eyePosition = shadowPos;
		shadowinfo[i].m_lookAt = centerPos;
		shadowinfo[i].m_nearPlane = 0;
		shadowinfo[i].m_farPlane = cascadeExtents.z;
		shadowinfo[i].m_viewWidth = maxExtents.x;
		shadowinfo[i].m_viewHeight = maxExtents.y;
		shadowinfo[i].m_lightViewProjection = (lightView * lightProj);
	}

	return shadowinfo;
}
