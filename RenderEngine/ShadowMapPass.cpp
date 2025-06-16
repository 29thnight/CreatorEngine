#include "ShadowMapPass.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"
#include "RenderableComponents.h"
#include "LightController.h"
#include "directxtk\SimpleMath.h"
#include "MeshRendererProxy.h"
#include "Skeleton.h"

bool g_useCascade = true;
constexpr size_t CASCADE_BEGIN_END_COUNT = 2;

ShadowMapPass::ShadowMapPass()
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

	m_boneBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * Skeleton::MAX_BONES, D3D11_BIND_CONSTANT_BUFFER, nullptr);

	//DeviceState::g_pDevice->CreateDeferredContext(0, &defferdContext1);
}

ShadowMapPass::~ShadowMapPass()
{
	for (auto& [frame, cmdArr] : m_commandQueueMap)
	{
		for (auto& queue : cmdArr)
		{
			while (!queue.empty())
			{
				ID3D11CommandList* CommandJob;
				if (queue.try_pop(CommandJob))
				{
					Memory::SafeDelete(CommandJob);
				}
			}
		}
	}
}

void ShadowMapPass::Initialize(uint32 width, uint32 height)
{

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
				Memory::SafeDelete(CommandJob);
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

	static auto& cameras = CameraManagement->m_cameras;
	static std::vector<RenderPassData*> dataPtrs{};
	static RenderPassData* selectedData{};
	static int selectedIndex = 0;
	static size_t lastCameraCount = 0;

	size_t currentCount = CameraManagement->GetCameraCount();
	if (currentCount != lastCameraCount)
	{
		lastCameraCount = currentCount;

		dataPtrs.clear();
		for (auto cam : cameras)
		{
			if (nullptr != cam)
			{
				auto data = RenderPassData::GetData(cam);
				dataPtrs.push_back(data);
			}
		}
	}

	ImGui::SliderInt("CameraIndex", &selectedIndex, 0, dataPtrs.size() - 1);

	if (dataPtrs[selectedIndex] != selectedData)
	{
		selectedData = dataPtrs[selectedIndex];
	}

	for (int i = 0; i < cascadeCount; ++i)
	{
		ImGui::Image((ImTextureID)selectedData->sliceSRV[i], ImVec2(256, 256));
	}

	ImGui::SliderFloat("epsilon", &m_settingConstant._epsilon, 0.0001f, 0.03f);
}

void ShadowMapPass::Resize(uint32_t width, uint32_t height)
{

}

void ShadowMapPass::CreateCommandListCascadeShadow(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
	auto* defferdContextPtr1 = defferdContext;

	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	if (!m_abled)
	{
		for (int i = 0; i < cascadeCount; i++)
		{
			DirectX11::ClearDepthStencilView(defferdContextPtr1, renderData->m_shadowMapDSVarr[i], D3D11_CLEAR_DEPTH, 1.0f, 0);
		}
		return;
	}

	auto  lightdir	= scene.m_LightController->GetLight(0).m_direction; //type = Mathf::Vector4
	auto  desc		= scene.m_LightController->m_shadowMapRenderDesc;	//type = ShadowMapRenderDesc
	auto& constant	= camera.m_shadowMapConstant;						//type = ShadowMapConstant
	auto  projMat	= camera.CalculateProjection();						//type = Mathf::xMatrix

	m_pso->Apply(defferdContextPtr1);

	DevideCascadeEnd(camera);
	DevideShadowInfo(camera, lightdir);

	constant.devideShadow		= m_settingConstant.devideShadow;
	constant._epsilon			= m_settingConstant._epsilon;
	constant.m_casCadeEnd1		= Mathf::Vector4::Transform({ 0, 0, camera.m_cascadeEnd[1], 1.f }, projMat).z;
	constant.m_casCadeEnd2		= Mathf::Vector4::Transform({ 0, 0, camera.m_cascadeEnd[2], 1.f }, projMat).z;
	constant.m_casCadeEnd3		= Mathf::Vector4::Transform({ 0, 0, camera.m_cascadeEnd[3], 1.f }, projMat).z;
	constant.m_shadowMapWidth	= desc.m_textureWidth;
	constant.m_shadowMapHeight  = desc.m_textureHeight;

	scene.UseModel(defferdContextPtr1);
	DirectX11::RSSetViewports(defferdContextPtr1, 1, &shadowViewport);
	DirectX11::VSSetConstantBuffer(defferdContextPtr1, 3, 1, m_boneBuffer.GetAddressOf());

	HashedGuid currentAnimatorGuid{};
	for (int i = 0; i < cascadeCount; i++)
	{
		auto& cascadeInfo = camera.m_cascadeinfo[i]; //type = ShadowInfo&

		renderData->m_shadowCamera.m_eyePosition = cascadeInfo.m_eyePosition;
		renderData->m_shadowCamera.m_lookAt		 = cascadeInfo.m_lookAt;
		renderData->m_shadowCamera.m_viewHeight	 = cascadeInfo.m_viewHeight;
		renderData->m_shadowCamera.m_viewWidth	 = cascadeInfo.m_viewWidth;
		renderData->m_shadowCamera.m_nearPlane	 = cascadeInfo.m_nearPlane;
		renderData->m_shadowCamera.m_farPlane	 = cascadeInfo.m_farPlane;
		constant.m_lightViewProjection[i]		 = cascadeInfo.m_lightViewProjection;

		DirectX11::ClearDepthStencilView(defferdContextPtr1, renderData->m_shadowMapDSVarr[i], D3D11_CLEAR_DEPTH, 1.0f, 0);
		DirectX11::OMSetRenderTargets(defferdContextPtr1, 0, nullptr, renderData->m_shadowMapDSVarr[i]);
		renderData->m_shadowCamera.UpdateBuffer(defferdContextPtr1, true);

		CreateCommandListProxyToShadow(defferdContext, scene, camera);
	}

	DirectX11::RSSetViewports(defferdContextPtr1, 1, &DeviceState::g_Viewport);
	DirectX11::UnbindRenderTargets(defferdContextPtr1);

	ID3D11CommandList* pCommandList;
	DirectX11::FinishCommandList(defferdContextPtr1, false, &pCommandList);
	PushQueue(camera.m_cameraIndex, pCommandList);
}

void ShadowMapPass::CreateCommandListNormalShadow(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
	auto defferdContextPtr1 = defferdContext;
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	if (!m_abled)
	{
		DirectX11::ClearDepthStencilView(defferdContextPtr1, renderData->m_shadowMapDSVarr[0], D3D11_CLEAR_DEPTH, 1.0f, 0);
		return;
	}

	auto  lightdir			= scene.m_LightController->GetLight(0).m_direction; //type = Mathf::Vector4
	auto  desc				= scene.m_LightController->m_shadowMapRenderDesc;	//type = ShadowMapRenderDesc
	auto& constantCopy		= camera.m_shadowMapConstant;						//type = ShadowMapConstant

	m_pso->Apply(defferdContextPtr1);

	DevideCascadeEnd(camera);
	DevideShadowInfo(camera, lightdir);

	constantCopy.devideShadow							= m_settingConstant.devideShadow;
	constantCopy._epsilon								= m_settingConstant._epsilon;
	renderData->m_shadowCamera.m_eyePosition			= camera.m_cascadeinfo[2].m_eyePosition;
	renderData->m_shadowCamera.m_lookAt					= camera.m_cascadeinfo[2].m_lookAt;
	renderData->m_shadowCamera.m_viewHeight				= camera.m_cascadeinfo[2].m_viewHeight;
	renderData->m_shadowCamera.m_viewWidth				= camera.m_cascadeinfo[2].m_viewWidth;
	renderData->m_shadowCamera.m_nearPlane				= camera.m_cascadeinfo[2].m_nearPlane;
	renderData->m_shadowCamera.m_farPlane				= camera.m_cascadeinfo[2].m_farPlane;
	constantCopy.m_shadowMapWidth						= desc.m_textureWidth;
	constantCopy.m_shadowMapHeight						= desc.m_textureHeight;
	constantCopy.m_lightViewProjection[0]				= camera.m_cascadeinfo[2].m_lightViewProjection;

	DirectX11::ClearDepthStencilView(defferdContextPtr1, renderData->m_shadowMapDSVarr[0], D3D11_CLEAR_DEPTH, 1.0f, 0);
	DirectX11::OMSetRenderTargets(defferdContextPtr1, 0, nullptr, renderData->m_shadowMapDSVarr[0]);
	DirectX11::VSSetConstantBuffer(defferdContextPtr1, 3, 1, m_boneBuffer.GetAddressOf());
	DirectX11::RSSetViewports(defferdContextPtr1, 1, &shadowViewport);
	renderData->m_shadowCamera.UpdateBuffer(defferdContextPtr1, true);
	scene.UseModel(defferdContextPtr1);

	CreateCommandListProxyToShadow(defferdContext, scene, camera);

	DirectX11::RSSetViewports(defferdContextPtr1, 1, &DeviceState::g_Viewport);
	DirectX11::UnbindRenderTargets(defferdContextPtr1);

	ID3D11CommandList* pd3dCommandList1;
	defferdContextPtr1->FinishCommandList(false, &pd3dCommandList1);
	PushQueue(camera.m_cameraIndex, pd3dCommandList1);
}

void ShadowMapPass::CreateCommandListProxyToShadow(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
	auto defferdContextPtr1 = defferdContext;

	HashedGuid currentAnimatorGuid{};

	for (auto& MeshRendererProxy : scene.GetShadowRenderQueue())
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
}

void ShadowMapPass::DevideCascadeEnd(Camera& camera)
{
	camera.m_cascadeEnd.clear();

	camera.m_cascadeEnd.push_back(camera.m_nearPlane);

	float distanceZ = camera.m_farPlane - camera.m_nearPlane;

	for (float ratio : camera.m_cascadeDevideRatios)
	{
		camera.m_cascadeEnd.push_back(ratio * distanceZ);
	}

	camera.m_cascadeEnd.push_back(camera.m_farPlane);
}

void ShadowMapPass::DevideShadowInfo(Camera& camera, Mathf::Vector4 LightDir)
{
	auto Fullfrustum = camera.GetFrustum();

	Mathf::Vector3 FullfrustumCorners[8];
	Fullfrustum.GetCorners(FullfrustumCorners);
	float nearZ = camera.m_nearPlane;
	float farZ  = camera.m_farPlane;

	DirectX::BoundingFrustum frustum(camera.CalculateProjection());

	Mathf::Matrix cameraview  = camera.CalculateView();
	Mathf::Matrix viewInverse = camera.CalculateInverseView();
	Mathf::Vector3 forwardVec = cameraview.Forward();

	for (size_t i = 0; i < sliceFrustums.size(); ++i)
	{
		std::array<Mathf::Vector3, 8>& sliceFrustum = sliceFrustums[i];
		float curEnd	= camera.m_cascadeEnd[i];
		float nextEnd	= camera.m_cascadeEnd[i + 1];

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

		Mathf::Vector3 shadowPos						= centerPos + LightDir * -radius;
		Mathf::Vector3 cascadeExtents					= maxExtents - minExtents;
		Mathf::xMatrix lightView						= DirectX::XMMatrixLookAtLH(shadowPos, centerPos, { 0, 1, 0 });
		Mathf::xMatrix lightProj						= DirectX::XMMatrixOrthographicOffCenterLH(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.1f, cascadeExtents.z);
		camera.m_cascadeinfo[i].m_eyePosition			= shadowPos;
		camera.m_cascadeinfo[i].m_lookAt				= centerPos;
		camera.m_cascadeinfo[i].m_nearPlane				= 0.1f; //*****
		camera.m_cascadeinfo[i].m_farPlane				= cascadeExtents.z;
		camera.m_cascadeinfo[i].m_viewWidth				= maxExtents.x;
		camera.m_cascadeinfo[i].m_viewHeight			= maxExtents.y;
		camera.m_cascadeinfo[i].m_lightViewProjection	= lightView * lightProj;
	}
}

void ShadowMapPass::CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera)
{
	scene.m_LightController->m_shadowMapConstant.useCasCade = g_useCascade;
	if (g_useCascade)
	{
		CreateCommandListCascadeShadow(defferdContext, scene, camera);
	}
	else
	{
		CreateCommandListNormalShadow(defferdContext, scene, camera);
	}
}
