#include "ShadowMapPass.h"
#include "EditorImGuiTexture.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "../EngineEntry/RenderPassSettings.h"
#include "Scene.h"
#include "Mesh.h"
#include "Sampler.h"
#include "RenderableComponents.h"
#include "LightController.h"
#include "directxtk\SimpleMath.h"
#include "MeshRendererProxy.h"
#include "Skeleton.h"
#include "Terrain.h"

bool g_useCascade = true;
constexpr size_t CASCADE_BEGIN_END_COUNT = 2;

cbuffer CloudShadowMapBuffer
{
	Mathf::Matrix shadowViewProjection;
	Mathf::Vector2 cloudMapSize;
	Mathf::Vector2 size;
	Mathf::Vector2 direction;
	UINT frameIndex;
	float moveSpeed;
	float alpha;
	bool32 isOn;
};

ShadowMapPass::ShadowMapPass()
{
	m_pso = std::make_unique<PipelineStateObject>();
	m_instancePSO = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["ShadowVertexShader"];

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
		DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
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

	m_cloudShadowMapBuffer = DirectX11::CreateBuffer(sizeof(CloudShadowMapBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	
	m_cascadeIndexBuffer = DirectX11::CreateBuffer(
		sizeof(CascadeIndexBuffer),
		D3D11_BIND_CONSTANT_BUFFER,
		nullptr
	);
}

ShadowMapPass::~ShadowMapPass()
{
	Memory::SafeDelete(m_cloudShadowMapBuffer);
}

void ShadowMapPass::Initialize(uint32 width, uint32 height)
{

}

void ShadowMapPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void ShadowMapPass::ControlPanel()
{
    ImGui::Text("ShadowPass");
    auto& setting = EngineSettingInstance->GetRenderPassSettingsRW().shadow;

    ImGui::Checkbox("Enable2", &m_abled);
    if (ImGui::Checkbox("UseCasCade", &g_useCascade))
    {
        setting.useCascade = g_useCascade;
    }

    if (ImGui::Checkbox("Is Cloud On", &isCloudOn))
    {
        setting.isCloudOn = isCloudOn;
    }
    if (ImGui::DragFloat2("CloudSize", &cloudSize.x, 0.075f, 0.f, 10.f))
    {
        setting.cloudSize = cloudSize;
    }
    if (ImGui::DragFloat2("CloudDirection Based Direction Light", &cloudDirection.x, 0.075f, -1.f, 1.f))
    {
        setting.cloudDirection = cloudDirection;
    }
    if (ImGui::DragFloat("Cloud MoveSpeed", &cloudMoveSpeed, 0.0001f, 0.f, 1.f, "%.5f"))
    {
        setting.cloudMoveSpeed = cloudMoveSpeed;
    }
	if (ImGui::DragFloat("CloudAlpha", &cloudAlpha, 0.01f, 0.f, 10.f, "%.2f"))
	{
		setting.cloudAlpha = cloudAlpha;
	}

	static auto& cameras = CameraManagement->GetCameras();
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
				auto data = RenderPassData::GetData(cam.get());
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
		ImGui::Image((ImTextureID)EditorImGuiTexture::FromRawDx11Srv(selectedData->sliceSRV[i]), ImVec2(256, 256));
	}

    if (ImGui::SliderFloat("epsilon", &m_settingConstant._epsilon, 0.0001f, 0.03f))
    {
        setting.epsilon = m_settingConstant._epsilon;
    }
}

void ShadowMapPass::Resize(uint32_t width, uint32_t height)
{

}

void ShadowMapPass::CreateCommandListCascadeShadow(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	auto* deferredContextPtr1 = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	// RHI 이식(PHASE 3-1, 5차 슬라이스). 프록시 드로우 루프(ProxyToShadow)와
	// 섀도 카메라 버퍼 갱신은 DX11 고유 경로로 남는다.

	for (auto i : std::views::iota(0, cascadeCount))
	{
		context.ClearDepthStencil(renderData->m_shadowMapDSVarr[i], true, false, 1.0f, 0);
	}

	if (!m_abled)
	{
		return;
	}

	auto  lightdir	= scene.m_LightController->GetLight(0).m_direction; //type = Mathf::Vector4
	auto  desc		= scene.m_LightController->m_shadowMapRenderDesc;	//type = ShadowMapRenderDesc
	auto& constant	= renderData->m_shadowCamera.m_shadowMapConstant;	//type = ShadowMapConstant
	auto  projMat	= renderData->GetFrameSnapshot().projection;			//type = Mathf::xMatrix

	DevideCascadeEnd(camera, *renderData);
	DevideShadowInfo(camera, *renderData, lightdir);

	constant.devideShadow		= m_settingConstant.devideShadow;
	constant._epsilon			= m_settingConstant._epsilon;
	constant.m_casCadeEnd1		= Mathf::Vector4::Transform({ 0, 0, renderData->m_cascadeEnd[1], 1.f }, projMat).z;
	constant.m_casCadeEnd2		= Mathf::Vector4::Transform({ 0, 0, renderData->m_cascadeEnd[2], 1.f }, projMat).z;
	constant.m_casCadeEnd3		= Mathf::Vector4::Transform({ 0, 0, renderData->m_cascadeEnd[3], 1.f }, projMat).z;
	constant.m_shadowMapWidth	= desc.m_textureWidth;
	constant.m_shadowMapHeight  = desc.m_textureHeight;

	m_pso->Apply(deferredContextPtr1);
	scene.UseModel(deferredContextPtr1);

	const RHIViewport shadowVp{ shadowViewport.TopLeftX, shadowViewport.TopLeftY,
		shadowViewport.Width, shadowViewport.Height, shadowViewport.MinDepth, shadowViewport.MaxDepth };
	context.SetViewports(1, &shadowVp);

	RHINativeBuffer boneBuffer = m_boneBuffer.Get();
	RHINativeBuffer cascadeIndexBuffer = m_cascadeIndexBuffer.Get();
	context.SetVertexShaderConstantBuffers(3, 1, &boneBuffer);
	context.SetVertexShaderConstantBuffers(4, 1, &cascadeIndexBuffer);

	renderData->m_shadowCamera.SetShadowInfo(renderData->m_cascadeInfo);
	renderData->m_shadowCamera.UpdateBufferCascade(deferredContextPtr1, true);

	HashedGuid currentAnimatorGuid{};
	for (auto i : std::views::iota(0, cascadeCount))
	{
		CascadeIndexBuffer m_currentCascadeIndex{};
		m_currentCascadeIndex.cascadeIndex = i;

		context.SetRenderTargets(0, nullptr, renderData->m_shadowMapDSVarr[i]);
		context.UpdateBuffer(m_cascadeIndexBuffer.Get(), &m_currentCascadeIndex);

		CreateCommandListProxyToShadow(deferredContextPtr1, scene, camera);
		CreateTerrainRenderCommandList(deferredContextPtr1, scene, camera);
	}

	const RHIViewport restoreVp = RHI::Device().GetFullViewport();
	context.SetViewports(1, &restoreVp);
	context.UnbindRenderTargets();

	RHINativeShaderResource nullSRV[] = { nullptr };
	context.SetVertexShaderResources(0, 1, nullSRV);

	ID3D11CommandList* pCommandList;
	DirectX11::FinishCommandList(deferredContextPtr1, false, &pCommandList);
	PushQueue(camera.m_cameraIndex, pCommandList);
}

void ShadowMapPass::CreateCommandListNormalShadow(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	auto* deferredContextPtr1 = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	// RHI 이식(PHASE 3-1, 5차 슬라이스) — 캐스케이드 변형과 같은 규약.

	context.ClearDepthStencil(renderData->m_shadowMapDSVarr[0], true, false, 1.0f, 0);

	if (!m_abled)
	{
		return;
	}

	auto  lightdir			= scene.m_LightController->GetLight(0).m_direction; //type = Mathf::Vector4
	auto  desc				= scene.m_LightController->m_shadowMapRenderDesc;	//type = ShadowMapRenderDesc
	// 예전에는 이 상수를 camera.m_shadowMapConstant에 썼다. 그런데 셰이더에
	// 올라가는 것은 renderData->m_shadowCamera.m_shadowMapConstant다
	// (DeferredPass가 그것을 바인딩한다). 즉 이 경로가 계산한 값은 아무 데도
	// 가지 않는 죽은 쓰기였고, 동시에 게임 소유 카메라를 렌더 스레드가 쓰는
	// 경로이기도 했다. 캐스케이드 경로와 같은 곳에 쓰도록 맞춘다.
	auto& constantCopy		= renderData->m_shadowCamera.m_shadowMapConstant;	//type = ShadowMapConstant

	m_pso->Apply(deferredContextPtr1);

	DevideCascadeEnd(camera, *renderData);
	DevideShadowInfo(camera, *renderData, lightdir);

	const ShadowInfo& lastCascade = renderData->m_cascadeInfo[cascadeCount - 1];

	constantCopy.devideShadow							= m_settingConstant.devideShadow;
	constantCopy._epsilon								= m_settingConstant._epsilon;
	renderData->m_shadowCamera.m_eyePosition			= lastCascade.m_eyePosition;
	renderData->m_shadowCamera.m_lookAt					= lastCascade.m_lookAt;
	renderData->m_shadowCamera.m_viewHeight				= lastCascade.m_viewHeight;
	renderData->m_shadowCamera.m_viewWidth				= lastCascade.m_viewWidth;
	renderData->m_shadowCamera.m_nearPlane				= lastCascade.m_nearPlane;
	renderData->m_shadowCamera.m_farPlane				= lastCascade.m_farPlane;
	constantCopy.m_shadowMapWidth						= desc.m_textureWidth;
	constantCopy.m_shadowMapHeight						= desc.m_textureHeight;
	constantCopy.m_lightViewProjection[0]				= lastCascade.m_lightViewProjection;

	context.ClearDepthStencil(renderData->m_shadowMapDSVarr[0], true, false, 1.0f, 0);
	context.SetRenderTargets(0, nullptr, renderData->m_shadowMapDSVarr[0]);

	RHINativeBuffer boneBuffer = m_boneBuffer.Get();
	context.SetVertexShaderConstantBuffers(3, 1, &boneBuffer);

	const RHIViewport shadowVp{ shadowViewport.TopLeftX, shadowViewport.TopLeftY,
		shadowViewport.Width, shadowViewport.Height, shadowViewport.MinDepth, shadowViewport.MaxDepth };
	context.SetViewports(1, &shadowVp);

	renderData->m_shadowCamera.UpdateBuffer(deferredContextPtr1, true);
	scene.UseModel(deferredContextPtr1);

	CreateCommandListProxyToShadow(deferredContextPtr1, scene, camera);
	CreateTerrainRenderCommandList(deferredContextPtr1, scene, camera);

	const RHIViewport restoreVp = RHI::Device().GetFullViewport();
	context.SetViewports(1, &restoreVp);
	context.UnbindRenderTargets();

	ID3D11CommandList* pd3dCommandList1;
	deferredContextPtr1->FinishCommandList(false, &pd3dCommandList1);
	PushQueue(camera.m_cameraIndex, pd3dCommandList1);
}

void ShadowMapPass::CreateCommandListProxyToShadow(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	auto deferredContextPtr1 = deferredContext;

	auto renderData = RenderPassData::GetData(&camera);

	HashedGuid currentAnimatorGuid{};

	for (auto& PrimitiveRenderProxy : renderData->m_shadowRenderQueue)
	{
		if (!PrimitiveRenderProxy || (int)PrimitiveRenderProxy->m_proxyType == (int)PrimitiveProxyType::Expired) continue;

		scene.UpdateModel(PrimitiveRenderProxy->m_worldMatrix, deferredContextPtr1);

		HashedGuid animatorGuid = PrimitiveRenderProxy->m_animatorGuid;
		if (PrimitiveRenderProxy->m_isAnimationEnabled && HashedGuid::INVAILD_ID != animatorGuid)
		{
			if (animatorGuid != currentAnimatorGuid && PrimitiveRenderProxy->m_finalTransforms)
			{
				DirectX11::UpdateBuffer(deferredContextPtr1, m_boneBuffer.Get(), PrimitiveRenderProxy->m_finalTransforms.get());
				currentAnimatorGuid = PrimitiveRenderProxy->m_animatorGuid;
			}
		}

		PrimitiveRenderProxy->DrawShadow(deferredContextPtr1);
	}
}

void ShadowMapPass::CreateTerrainRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	auto deferredContextPtr1 = deferredContext;

	auto renderData = RenderPassData::GetData(&camera);

	for (auto& terrainProxy : renderData->m_terrainQueue) {
		//Unsafe�� ���
		if (!terrainProxy || (int)terrainProxy->m_proxyType != (int)PrimitiveProxyType::TerrainComponent) continue;

		auto terrainMesh = terrainProxy->m_terrainMesh;
		auto terrainMaterial = terrainProxy->m_terrainMaterial;

		if (terrainMesh && terrainMaterial)
		{
			scene.UpdateModel(terrainProxy->m_worldMatrix, deferredContextPtr1);
			terrainMesh->Draw(deferredContextPtr1);
		}
	}
}

void ShadowMapPass::DevideCascadeEnd(Camera& camera, RenderPassData& renderData)
{
	// 카메라에서는 읽기만 한다. 쓰는 곳은 렌더 측 스크래치다.
	// Initalize에서 reserve해 두었으므로 clear/push_back이 재할당을 일으키지 않는다.
	auto& cascadeEnd = renderData.m_cascadeEnd;
	cascadeEnd.clear();

	cascadeEnd.push_back(renderData.GetFrameSnapshot().nearPlane);

	const float distanceZ = renderData.GetFrameSnapshot().farPlane - renderData.GetFrameSnapshot().nearPlane;

	// 분할 비율만 카메라에서 읽는다 — 저작 설정이라 프레임 중 바뀌지 않는다.
	for (float ratio : camera.m_cascadeDevideRatios)
	{
		cascadeEnd.push_back(ratio * distanceZ);
	}

	cascadeEnd.push_back(renderData.GetFrameSnapshot().farPlane);
}

void ShadowMapPass::DevideShadowInfo(Camera& camera, RenderPassData& renderData, Mathf::Vector4 LightDir)
{
	// Normalize light direction once before the loop.
	if (Mathf::Vector3(LightDir) == Mathf::Vector3{ 0.f, 0.f, 0.f })
	{
		LightDir = { 0.f, 0.f, -1.f, 0.f };
	}
	else
	{
		LightDir.Normalize();
	}

	// 살아 있는 카메라에서 다시 계산하지 않고 프레임 밀봉된 행렬을 쓴다.
	// CalculateInverseView()는 정의상 CalculateView()의 역행렬이라 값은 같다.
	const auto frustum = DirectX::BoundingFrustum(renderData.GetFrameSnapshot().projection);
	const auto viewInverse = XMMatrixInverse(nullptr, renderData.GetFrameSnapshot().view);

	for (const int i : std::views::iota(0, cascadeCount))
	{
		// 1. Calculate the 8 corners of the cascade slice's frustum in world space.
		std::array<Mathf::Vector3, 8> sliceFrustumCorners;
		const float curEnd = renderData.m_cascadeEnd[i];
		const float nextEnd = renderData.m_cascadeEnd[i + 1];

		sliceFrustumCorners[0] = Mathf::Vector3::Transform({ frustum.RightSlope * curEnd, frustum.TopSlope * curEnd, curEnd }, viewInverse);
		sliceFrustumCorners[1] = Mathf::Vector3::Transform({ frustum.RightSlope * curEnd, frustum.BottomSlope * curEnd, curEnd }, viewInverse);
		sliceFrustumCorners[2] = Mathf::Vector3::Transform({ frustum.LeftSlope * curEnd, frustum.TopSlope * curEnd, curEnd }, viewInverse);
		sliceFrustumCorners[3] = Mathf::Vector3::Transform({ frustum.LeftSlope * curEnd, frustum.BottomSlope * curEnd, curEnd }, viewInverse);
		sliceFrustumCorners[4] = Mathf::Vector3::Transform({ frustum.RightSlope * nextEnd, frustum.TopSlope * nextEnd, nextEnd }, viewInverse);
		sliceFrustumCorners[5] = Mathf::Vector3::Transform({ frustum.RightSlope * nextEnd, frustum.BottomSlope * nextEnd, nextEnd }, viewInverse);
		sliceFrustumCorners[6] = Mathf::Vector3::Transform({ frustum.LeftSlope * nextEnd, frustum.TopSlope * nextEnd, nextEnd }, viewInverse);
		sliceFrustumCorners[7] = Mathf::Vector3::Transform({ frustum.LeftSlope * nextEnd, frustum.BottomSlope * nextEnd, nextEnd }, viewInverse);

		// 2. Calculate the center and radius of the bounding sphere for the slice.
		const Mathf::Vector3 centerPos = std::accumulate(sliceFrustumCorners.begin(), sliceFrustumCorners.end(), Mathf::Vector3::Zero) / 8.f;

		const auto distance_from_center = [&](const Mathf::Vector3& pos) -> float 
		{
			return Mathf::Vector3::Distance(centerPos, pos);
		};
		const float radius = std::ranges::max(sliceFrustumCorners | std::views::transform(distance_from_center));

		// 3. Quantize radius and center position to prevent shadow shimmering.
		const float quantizedRadius = std::ceil(radius * 32.f) / 32.f;
		const Mathf::Vector3 quantizedCenterPos = Mathf::Vector3(std::floor(centerPos.x), std::floor(centerPos.y), std::floor(centerPos.z));

		// 4. Calculate the light's view and projection matrices.
		const Mathf::Vector3 maxExtents = Mathf::Vector3(quantizedRadius, quantizedRadius, quantizedRadius);
		const Mathf::Vector3 minExtents = -maxExtents;

		const Mathf::xVector shadowPos = quantizedCenterPos + LightDir * -250.f;

		const Mathf::xMatrix lightView = DirectX::XMMatrixLookAtLH(shadowPos, quantizedCenterPos, { 0.f, 1.f, 0.f });
		const Mathf::xMatrix lightProj = DirectX::XMMatrixOrthographicOffCenterLH(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.1f, 500.f);

		// 5. Store the calculated information for the current cascade.
		ShadowInfo& cascadeInfo = renderData.m_cascadeInfo[i];
		cascadeInfo.m_eyePosition = shadowPos;
		cascadeInfo.m_lookAt = quantizedCenterPos;
		cascadeInfo.m_nearPlane = 0.1f;
		cascadeInfo.m_farPlane = 500.f;
		cascadeInfo.m_viewWidth = maxExtents.x;
		cascadeInfo.m_viewHeight = maxExtents.y;
		cascadeInfo.m_lightViewProjection = lightView * lightProj;
	}
}

void ShadowMapPass::UseCloudShadowMap(std::string_view filename)
{
	file::path path = file::path(filename);
	if (file::exists(path) || nullptr == m_cloudShadowMapTexture)
	{
		m_cloudShadowMapTexture = Texture::LoadManagedFromPath(filename);
		m_cloudShadowMapTexture->m_textureType = TextureType::ImageTexture;
	}
}

void ShadowMapPass::UpdateCloudBuffer(ID3D11DeviceContext* defferdContext, LightController* lightcontroller)
{
	if (lightcontroller->m_lightCount <= 0 || nullptr == m_cloudShadowMapTexture)
		return;
	ID3D11DeviceContext* defferdPtr = defferdContext;

	auto LightDir = lightcontroller->GetLight(0).m_direction;
	Mathf::Vector3 shadowPos = Mathf::Vector3(LightDir.x, LightDir.y, LightDir.z) * -250;
	Mathf::xMatrix lightView = DirectX::XMMatrixLookAtLH(shadowPos, Mathf::Vector3::Zero, { 0, 1, 0 });
	Mathf::xMatrix lightProj = DirectX::XMMatrixOrthographicOffCenterLH(-512, 512, -512, 512, 0.1f, 500);

	CloudShadowMapBuffer buffer{};
	buffer.shadowViewProjection = lightView * lightProj;
	buffer.cloudMapSize = m_cloudShadowMapTexture->GetImageSize();
	buffer.size = cloudSize;
	buffer.direction = cloudDirection;
	buffer.frameIndex = Time->GetFrameCount();
	buffer.moveSpeed = cloudMoveSpeed;
	buffer.alpha = cloudAlpha;
	buffer.isOn = isCloudOn;
	DirectX11::UpdateBuffer(defferdPtr, m_cloudShadowMapBuffer, &buffer);
}

void ShadowMapPass::PSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, LightController* lightcontroller, bool isOn)
{
	if (isOn && nullptr != m_cloudShadowMapTexture)
	{
		UpdateCloudBuffer(defferdContext, lightcontroller);
		DirectX11::PSSetConstantBuffer(defferdContext, 4, 1, &m_cloudShadowMapBuffer);
		DirectX11::PSSetShaderResources(defferdContext, 10, 1, &m_cloudShadowMapTexture->m_pSRV);
	}
	else 
	{
		UpdateCloudBuffer(defferdContext, lightcontroller);
		ID3D11Buffer* nullbuf = nullptr;
		ID3D11ShaderResourceView* nullsrv = nullptr;
		DirectX11::PSSetConstantBuffer(defferdContext, 4, 1, &nullbuf);
		DirectX11::PSSetShaderResources(defferdContext, 10, 1, &nullsrv);
	}
}

void ShadowMapPass::CSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, LightController* lightcontroller, bool isOn)
{
	if (isOn && nullptr != m_cloudShadowMapTexture)
	{
		UpdateCloudBuffer(defferdContext, lightcontroller);
		DirectX11::CSSetConstantBuffer(defferdContext, 1, 1, &m_cloudShadowMapBuffer);
		DirectX11::CSSetShaderResources(defferdContext, 3, 1, &m_cloudShadowMapTexture->m_pSRV);
	}
	else 
	{
		UpdateCloudBuffer(defferdContext, lightcontroller);
		ID3D11Buffer* nullbuf = nullptr;
		ID3D11ShaderResourceView* nullsrv = nullptr;
		DirectX11::CSSetConstantBuffer(defferdContext, 1, 1, &nullbuf);
		DirectX11::CSSetShaderResources(defferdContext, 3, 1, &nullsrv);
	}
}

void ShadowMapPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	scene.m_LightController->m_shadowMapConstant.useCasCade = g_useCascade;

	// camera.m_shadowMapConstant에도 같은 값을 쓰고 있었는데, 그 상수는 아무도
	// 읽지 않는다(셰이더에 올라가는 것은 m_shadowCamera 쪽이다). 게임 소유
	// 카메라를 렌더 스레드가 쓰는 경로라 지운다 — 그림자 경로에서 이제
	// 게임 카메라 쓰기는 0건이다(PHASE 3-2).
	if (RenderPassData::VaildCheck(&camera))
	{
		RenderPassData::GetData(&camera)->m_shadowCamera.m_shadowMapConstant.useCasCade = g_useCascade;
	}

	if (g_useCascade)
	{
		CreateCommandListCascadeShadow(context, scene, camera);
	}
	else
	{
		CreateCommandListNormalShadow(context, scene, camera);
	}
}

void ShadowMapPass::ApplySettings(const ShadowMapPassSetting& setting)
{
    g_useCascade = setting.useCascade;
    isCloudOn = setting.isCloudOn;
    cloudSize = setting.cloudSize;
    cloudDirection = setting.cloudDirection;
    cloudMoveSpeed = setting.cloudMoveSpeed;
	cloudAlpha = setting.cloudAlpha;
    m_settingConstant._epsilon = setting.epsilon;
}
