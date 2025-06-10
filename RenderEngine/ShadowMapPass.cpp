#include "ShadowMapPass.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"
#include "RenderableComponents.h"
#include "LightController.h"
#include "directxtk\SimpleMath.h"
#include "RenderCommand.h"
#include "Skeleton.h"

bool g_useCascade = true;
constexpr size_t CASCADE_BEGIN_END_COUNT = 2;

ShadowMapPass::ShadowMapPass() : m_shadowCamera(true)
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];

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

	auto linearSampler	= std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
	auto pointSampler	= std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	shadowViewport.TopLeftX = 0;
	shadowViewport.TopLeftY = 0;
	shadowViewport.Width	= 2048;
	shadowViewport.Height	= 2048;
	shadowViewport.MinDepth = 0.0f;
	shadowViewport.MaxDepth = 1.0f;

	//m_threadPool = new ThreadPool; //일단 잠시 대기

	m_boneBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * Skeleton::MAX_BONES, D3D11_BIND_CONSTANT_BUFFER, nullptr);

	DeviceState::g_pDevice->CreateDeferredContext(0, &defferdContext1);

	m_cascadeinfo.resize(cascadeCount);
}

void ShadowMapPass::Initialize(uint32 width, uint32 height)
{
	Texture* shadowMapTexture = Texture::CreateArray(
		width, 
		height, 
		"Shadow Map",
		DXGI_FORMAT_R32_TYPELESS, 
		D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 
		cascadeCount
	);

	for(int i = 0; i < cascadeCount; ++i)
	{
		sliceSRV[i] = DirectX11::CreateSRVForArraySlice(DeviceState::g_pDevice, shadowMapTexture->m_pTexture, DXGI_FORMAT_R32_FLOAT, i);
	}

	for (int i = 0; i < cascadeCount; i++)
	{
		CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
		depthStencilViewDesc.Format							= DXGI_FORMAT_D32_FLOAT;
		depthStencilViewDesc.ViewDimension					= D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		depthStencilViewDesc.Texture2DArray.ArraySize		= 1;
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
	shadowMapTexture->m_textureType = TextureType::ImageTexture;
	m_shadowMapTexture = MakeUniqueTexturePtr(shadowMapTexture);
	m_shadowCamera.m_isOrthographic = true;
}

void ShadowMapPass::Execute(RenderScene& scene, Camera& camera)
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
			}
		}

		ShadowMapConstant shadowMapConstant = scene.m_LightController->m_shadowMapConstant;
		DirectX11::UpdateBuffer(scene.m_LightController->m_shadowMapBuffer, &shadowMapConstant);
	}
}

void ShadowMapPass::ControlPanel()
{
	ImGui::Text("ShadowPass");
	ImGui::Checkbox("Enable2", &m_abled);
	ImGui::Checkbox("UseCasCade", &g_useCascade);
	for (int i = 0; i < cascadeCount; ++i)
	{
		ImGui::Image((ImTextureID)sliceSRV[i], ImVec2(256, 256));
	}
	ImGui::SliderFloat("epsilon", &m_settingConstant._epsilon, 0.0001f, 0.03f);
}

void ShadowMapPass::Resize(uint32_t width, uint32_t height)
{

}

void ShadowMapPass::CreateCommandListCascadeShadow(RenderScene& scene, Camera& camera)
{
	auto* defferdContextPtr1 = defferdContext1.Get();

	if (!m_abled)
	{
		for (int i = 0; i < cascadeCount; i++)
		{
			DirectX11::ClearDepthStencilView(defferdContextPtr1, m_shadowMapDSVarr[i], D3D11_CLEAR_DEPTH, 1.0f, 0);
		}
		return;
	}

	auto  lightdir		= scene.m_LightController->GetLight(0).m_direction; //type = Mathf::Vector4
	auto  desc			= scene.m_LightController->m_shadowMapRenderDesc;	//type = ShadowMapRenderDesc
	auto& constantRef	= scene.m_LightController->m_shadowMapConstant;		//type = ShadowMapConstant&
	auto  constantCopy	= scene.m_LightController->m_shadowMapConstant;		//type = ShadowMapConstant
	auto  projMat		= camera.CalculateProjection();						//type = Mathf::xMatrix

	m_pso->Apply(defferdContextPtr1);

	DevideCascadeEnd(camera);
	DevideShadowInfo(camera, lightdir);

	constantRef.devideShadow	= m_settingConstant.devideShadow;
	constantRef._epsilon		= m_settingConstant._epsilon;
	constantCopy.m_casCadeEnd1	= Mathf::Vector4::Transform({ 0, 0, m_cascadeEnd[1], 1.f }, projMat).z;
	constantCopy.m_casCadeEnd2	= Mathf::Vector4::Transform({ 0, 0, m_cascadeEnd[2], 1.f }, projMat).z;
	constantCopy.m_casCadeEnd3	= Mathf::Vector4::Transform({ 0, 0, m_cascadeEnd[3], 1.f }, projMat).z;

	scene.UseModel(defferdContextPtr1);
	DirectX11::RSSetViewports(defferdContextPtr1, 1, &shadowViewport);
	DirectX11::VSSetConstantBuffer(defferdContextPtr1, 3, 1, m_boneBuffer.GetAddressOf());

	HashedGuid currentAnimatorGuid{};
	for (int i = 0; i < cascadeCount; i++)
	{
		auto& cascadeInfo = m_cascadeinfo[i]; //type = ShadowInfo&

		m_shadowCamera.m_eyePosition			= cascadeInfo.m_eyePosition;
		m_shadowCamera.m_lookAt					= cascadeInfo.m_lookAt;
		m_shadowCamera.m_viewHeight				= cascadeInfo.m_viewHeight;
		m_shadowCamera.m_viewWidth				= cascadeInfo.m_viewWidth;
		m_shadowCamera.m_nearPlane				= cascadeInfo.m_nearPlane;
		m_shadowCamera.m_farPlane				= cascadeInfo.m_farPlane;
		constantCopy.m_shadowMapWidth			= desc.m_textureWidth;
		constantCopy.m_shadowMapHeight			= desc.m_textureHeight;
		constantCopy.m_lightViewProjection[i]	= cascadeInfo.m_lightViewProjection;

		DirectX11::ClearDepthStencilView(defferdContextPtr1, m_shadowMapDSVarr[i], D3D11_CLEAR_DEPTH, 1.0f, 0);
		DirectX11::OMSetRenderTargets(defferdContextPtr1, 0, nullptr, m_shadowMapDSVarr[i]);
		m_shadowCamera.UpdateBuffer(defferdContextPtr1, true);

		CreateCommandListProxyToShadow(scene, camera);

		ID3D11CommandList* pCommandList;
		DirectX11::FinishCommandList(defferdContextPtr1, true, &pCommandList);
		PushQueue(camera.m_cameraIndex, pCommandList);
	}

	DirectX11::UpdateBuffer(defferdContextPtr1, scene.m_LightController->m_shadowMapBuffer, &constantCopy);
	DirectX11::RSSetViewports(defferdContextPtr1, 1, &DeviceState::g_Viewport);
	DirectX11::UnbindRenderTargets(defferdContextPtr1);

	ID3D11CommandList* pCommandList;
	DirectX11::FinishCommandList(defferdContextPtr1, false, &pCommandList);
	PushQueue(camera.m_cameraIndex, pCommandList);

	constantRef = constantCopy;
}

void ShadowMapPass::CreateCommandListNormalShadow(RenderScene& scene, Camera& camera)
{
	auto* defferdContextPtr1 = defferdContext1.Get();

	if (!m_abled)
	{
		DirectX11::ClearDepthStencilView(defferdContextPtr1, m_shadowMapDSVarr[0], D3D11_CLEAR_DEPTH, 1.0f, 0);
		return;
	}

	auto  lightdir			= scene.m_LightController->GetLight(0).m_direction; //type = Mathf::Vector4
	auto  desc				= scene.m_LightController->m_shadowMapRenderDesc;	//type = ShadowMapRenderDesc
	auto& constantRef		= scene.m_LightController->m_shadowMapConstant;		//type = ShadowMapConstant&
	auto  constantCopy		= scene.m_LightController->m_shadowMapConstant;		//type = ShadowMapConstant

	m_pso->Apply(defferdContextPtr1);

	DevideCascadeEnd(camera);
	DevideShadowInfo(camera, lightdir);

	constantRef.devideShadow				= m_settingConstant.devideShadow;
	constantRef._epsilon					= m_settingConstant._epsilon;
	m_shadowCamera.m_eyePosition			= m_cascadeinfo[2].m_eyePosition;
	m_shadowCamera.m_lookAt					= m_cascadeinfo[2].m_lookAt;
	m_shadowCamera.m_viewHeight				= m_cascadeinfo[2].m_viewHeight;
	m_shadowCamera.m_viewWidth				= m_cascadeinfo[2].m_viewWidth;
	m_shadowCamera.m_nearPlane				= m_cascadeinfo[2].m_nearPlane;
	m_shadowCamera.m_farPlane				= m_cascadeinfo[2].m_farPlane;
	constantCopy.m_shadowMapWidth			= desc.m_textureWidth;
	constantCopy.m_shadowMapHeight			= desc.m_textureHeight;
	constantCopy.m_lightViewProjection[0]	= m_cascadeinfo[2].m_lightViewProjection;

	DirectX11::ClearDepthStencilView(defferdContextPtr1, m_shadowMapDSVarr[0], D3D11_CLEAR_DEPTH, 1.0f, 0);
	DirectX11::OMSetRenderTargets(defferdContextPtr1, 0, nullptr, m_shadowMapDSVarr[0]);
	DirectX11::VSSetConstantBuffer(defferdContextPtr1, 3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::RSSetViewports(defferdContextPtr1, 1, &shadowViewport);
	m_shadowCamera.UpdateBuffer(defferdContextPtr1, true);
	scene.UseModel(defferdContextPtr1);

	CreateCommandListProxyToShadow(scene, camera);

	DirectX11::UpdateBuffer(defferdContextPtr1, scene.m_LightController->m_shadowMapBuffer, &constantCopy);
	DirectX11::RSSetViewports(defferdContextPtr1, 1, &DeviceState::g_Viewport);
	DirectX11::UnbindRenderTargets(defferdContextPtr1);

	constantRef = constantCopy;

	ID3D11CommandList* pd3dCommandList1;
	defferdContext1->FinishCommandList(true, &pd3dCommandList1);
	PushQueue(camera.m_cameraIndex, pd3dCommandList1);
}

void ShadowMapPass::CreateCommandListProxyToShadow(RenderScene& scene, Camera& camera)
{
	auto* defferdContextPtr1 = defferdContext1.Get();

	HashedGuid currentAnimatorGuid{};

	for (auto& MeshRendererProxy : camera.m_defferdQueue)
	{
		scene.UpdateModel(MeshRendererProxy->m_worldMatrix, defferdContextPtr1);

		HashedGuid animatorGuid = MeshRendererProxy->m_animatorGuid;
		if (MeshRendererProxy->m_isAnimationEnabled && HashedGuid::INVAILD_ID != animatorGuid)
		{
			if (animatorGuid != currentAnimatorGuid)
			{
				DirectX11::UpdateBuffer(defferdContextPtr1, m_boneBuffer.Get(), MeshRendererProxy->m_finalTransforms);
				currentAnimatorGuid = MeshRendererProxy->m_animatorGuid;
			}
		}

		MeshRendererProxy->Draw(defferdContextPtr1);
	}

	for (auto& MeshRendererProxy : camera.m_forwardQueue)
	{
		scene.UpdateModel(MeshRendererProxy->m_worldMatrix, defferdContextPtr1);

		HashedGuid animatorGuid = MeshRendererProxy->m_animatorGuid;
		if (MeshRendererProxy->m_isAnimationEnabled && HashedGuid::INVAILD_ID != animatorGuid)
		{
			if (animatorGuid != currentAnimatorGuid)
			{
				DirectX11::UpdateBuffer(m_boneBuffer.Get(), MeshRendererProxy->m_finalTransforms);
				currentAnimatorGuid = MeshRendererProxy->m_animatorGuid;
			}
		}

		MeshRendererProxy->Draw(defferdContextPtr1);
	}
}

void ShadowMapPass::DevideCascadeEnd(Camera& camera)
{
	if(m_cascadeEnd.size() != m_cascadeDevideRatios.size() || m_cascadeDevideRatios.size() != m_cascadeRatioSize)
	{
		m_cascadeEnd.reserve(m_cascadeDevideRatios.size() + CASCADE_BEGIN_END_COUNT);

		m_cascadeEnd.push_back(camera.m_nearPlane);

		float distanceZ = camera.m_farPlane - camera.m_nearPlane;

		for (float ratio : m_cascadeDevideRatios)
		{
			m_cascadeEnd.push_back(ratio * distanceZ);
		}

		m_cascadeEnd.push_back(camera.m_farPlane);
	}
}

void ShadowMapPass::DevideShadowInfo(Camera& camera, Mathf::Vector4 LightDir)
{
	auto Fullfrustum = camera.GetFrustum();

	Mathf::Vector3 FullfrustumCorners[8];
	Fullfrustum.GetCorners(FullfrustumCorners);
	float nearZ = camera.m_nearPlane;
	float farZ  = camera.m_farPlane;

	DirectX::BoundingFrustum frustum(camera.CalculateProjection());
	//float frustumDistnace = (frustum.Far - frustum.Near);

	Mathf::Matrix cameraview = camera.CalculateView();
	Mathf::Matrix viewInverse = camera.CalculateInverseView();
	Mathf::Vector3 forwardVec = cameraview.Forward();

	for (size_t i = 0; i < sliceFrustums.size(); ++i)
	{
		std::array<Mathf::Vector3, 8>& sliceFrustum = sliceFrustums[i];
		float curEnd	= m_cascadeEnd[i];
		float nextEnd	= m_cascadeEnd[i + 1];

		sliceFrustum[0] = Mathf::Vector3::Transform({ frustum.RightSlope * curEnd, frustum.TopSlope * curEnd, curEnd }, viewInverse);
		sliceFrustum[1] = Mathf::Vector3::Transform({ frustum.RightSlope * curEnd, frustum.BottomSlope * curEnd, curEnd }, viewInverse);
		sliceFrustum[2] = Mathf::Vector3::Transform({ frustum.LeftSlope * curEnd, frustum.TopSlope * curEnd, curEnd }, viewInverse);
		sliceFrustum[3] = Mathf::Vector3::Transform({ frustum.LeftSlope * curEnd, frustum.BottomSlope * curEnd, curEnd }, viewInverse);

		sliceFrustum[4] = Mathf::Vector3::Transform({ frustum.RightSlope * nextEnd, frustum.TopSlope * nextEnd, nextEnd }, viewInverse);
		sliceFrustum[5] = Mathf::Vector3::Transform({ frustum.RightSlope * nextEnd, frustum.BottomSlope * nextEnd, nextEnd }, viewInverse);
		sliceFrustum[6] = Mathf::Vector3::Transform({ frustum.LeftSlope * nextEnd, frustum.TopSlope * nextEnd, nextEnd }, viewInverse);
		sliceFrustum[7] = Mathf::Vector3::Transform({ frustum.LeftSlope * nextEnd, frustum.BottomSlope * nextEnd, nextEnd }, viewInverse);
	}

	for (size_t i = 0; i < cascadeCount; ++i)
	{
		const auto& sliceFrustum = sliceFrustums[i];

		Mathf::Vector3 centerPos = { 0.f, 0.f, 0.f };
		for (const auto& pos : sliceFrustum)
		{
			centerPos += pos;
		}
		centerPos /= 8.f;

		float radius = 0.f;
		for (const auto& pos : sliceFrustum)
		{
			float distance = Mathf::Vector3::Distance(centerPos, pos);
			radius = std::max<float>(radius, distance);
		}

		radius = std::ceil(radius * 32.f) / 32.f;

		Mathf::Vector3 maxExtents = { radius, radius, radius };
		Mathf::Vector3 minExtents = -maxExtents;

		if (Mathf::Vector3(LightDir) == Mathf::Vector3{ 0, 0, 0 })
		{
			LightDir = { 0.f, 0.f, -1.f, 0.f };
		}
		else
		{
			LightDir.Normalize();
		}

		Mathf::Vector3 shadowPos				= centerPos + LightDir * -radius;
		Mathf::Vector3 cascadeExtents			= maxExtents - minExtents;
		Mathf::xMatrix lightView				= DirectX::XMMatrixLookAtLH(shadowPos, centerPos, { 0, 1, 0 });
		Mathf::xMatrix lightProj				= DirectX::XMMatrixOrthographicOffCenterLH(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.1f, cascadeExtents.z);
		m_cascadeinfo[i].m_eyePosition			= shadowPos;
		m_cascadeinfo[i].m_lookAt				= centerPos;
		m_cascadeinfo[i].m_nearPlane			= 0.1f; //*****
		m_cascadeinfo[i].m_farPlane				= cascadeExtents.z;
		m_cascadeinfo[i].m_viewWidth			= maxExtents.x;
		m_cascadeinfo[i].m_viewHeight			= maxExtents.y;
		m_cascadeinfo[i].m_lightViewProjection	= lightView * lightProj;
	}
}

void ShadowMapPass::CreateRenderCommandList(RenderScene& scene, Camera& camera)
{
	scene.m_LightController->m_shadowMapConstant.useCasCade = g_useCascade;
	if (g_useCascade)
	{
		CreateCommandListCascadeShadow(scene, camera);
	}
	else
	{
		CreateCommandListNormalShadow(scene, camera);
	}
}
