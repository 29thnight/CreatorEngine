#include "VolumetricFogPass.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "RenderScene.h"
#include "LightController.h"
#include "ShadowMapPass.h"
#include "TimeSystem.h"

#include "LightProperty.h"
#include "VolumetricFogPassSetting.h"

#define MAX_LIGHT 20
#define VOXEL_VOLUME_SIZE_Z 128
struct alignas(16) MainCB
{
	XMMATRIX InvViewProj;
	XMMATRIX PrevViewProj;

	XMMATRIX ShadowMatrix;
	XMFLOAT4 SunDirection;
	XMFLOAT4 SunColor;
	XMFLOAT4 CameraPosition;
	XMFLOAT4 CameraNearFar_FrameIndex_PreviousFrameBlend;
	XMFLOAT4 VolumeSize;
	float Anisotropy;
	float Density;
	float Strength;
	float ThicknessFactor;
};

struct alignas(16) CompositeCB
{
	XMMATRIX ViewProj;
	XMMATRIX InvView;
	XMMATRIX InvProj;
	XMFLOAT4 CameraNearFar;
	XMFLOAT4 VolumeSize;
	float BlendingWithSceneColorFactor;
};

VolumetricFogPass::VolumetricFogPass()
{
	m_pMainShader = &ShaderSystem->ComputeShaders["VolumetricFog"];
	m_pAccumulationShader = &ShaderSystem->ComputeShaders["VolumetricFogAccumulation"];

	CD3D11_SAMPLER_DESC samplerDesc{ CD3D11_DEFAULT() };
	samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	samplerDesc.BorderColor[0] = 1.0f;
	samplerDesc.BorderColor[1] = 1.0f;
	samplerDesc.BorderColor[2] = 1.0f;
	samplerDesc.BorderColor[3] = 1.0f;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = 0;
	samplerDesc.MaxAnisotropy = 0;
	samplerDesc.MipLODBias = 0;

	CD3D11_SAMPLER_DESC clampSamplerDesc{ CD3D11_DEFAULT() };
	clampSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	clampSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	clampSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	clampSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	clampSamplerDesc.MinLOD = 0;
	clampSamplerDesc.MaxLOD = 0;

	CD3D11_SAMPLER_DESC wrapSamplerDesc{ CD3D11_DEFAULT() };
	wrapSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	wrapSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	wrapSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	wrapSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	wrapSamplerDesc.MinLOD = -1000;
	wrapSamplerDesc.MaxLOD = 1000;

	DirectX11::CreateSamplerState(&samplerDesc, &m_pShadowSamper);
	DirectX11::CreateSamplerState(&clampSamplerDesc, &m_pClampSampler);
	DirectX11::CreateSamplerState(&wrapSamplerDesc, &m_pWrapSampler);
	DirectX::SetName(m_pShadowSamper, std::to_string(reinterpret_cast<uintptr_t>(this)) + " Shadow : " + std::to_string(samplerDesc.Filter));
	DirectX::SetName(m_pClampSampler, std::to_string(reinterpret_cast<uintptr_t>(this)) + " Clamp : " + std::to_string(clampSamplerDesc.Filter));
	DirectX::SetName(m_pWrapSampler, std::to_string(reinterpret_cast<uintptr_t>(this)) + " Wrap : " + std::to_string(wrapSamplerDesc.Filter));

	//m_pso->m_samplers.push_back(linearSampler);
	m_Buffer = DirectX11::CreateBuffer(sizeof(MainCB), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_lightBuffer = DirectX11::CreateBuffer(sizeof(LightProperties), D3D11_BIND_CONSTANT_BUFFER, nullptr);

	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Fullscreen"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["VolumetricFogComposite"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

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

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);
	m_CompositeBuffer = DirectX11::CreateBuffer(sizeof(CompositeCB), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

VolumetricFogPass::~VolumetricFogPass()
{
	Memory::SafeDelete(m_pShadowSamper);
	Memory::SafeDelete(m_pClampSampler);
	Memory::SafeDelete(m_pWrapSampler);

	for (int i = 0; i < 2; ++i)
	{
		Memory::SafeDelete(mTempVoxelInjectionTexture3D[i]);
		Memory::SafeDelete(mTempVoxelInjectionTexture3DSRV[i]);
		Memory::SafeDelete(mTempVoxelInjectionTexture3DUAV[i]);
	}

	Memory::SafeDelete(mFinalVoxelInjectionTexture3D);
	Memory::SafeDelete(mFinalVoxelInjectionTexture3DSRV);
	Memory::SafeDelete(mFinalVoxelInjectionTexture3DUAV);
}
//
void VolumetricFogPass::Initialize(std::string_view fileName)
{
	mCurrentVoxelVolumeSizeX = 160;
	mCurrentVoxelVolumeSizeY = 90;

	D3D11_TEXTURE3D_DESC texDesc = {};
	texDesc.Width = mCurrentVoxelVolumeSizeX;							// �ؽ�ó ����
	texDesc.Height = mCurrentVoxelVolumeSizeY;							// �ؽ�ó ����
	texDesc.Depth = VOXEL_VOLUME_SIZE_Z;            // ���� (Z)
	texDesc.MipLevels = 1;							// Mip Level �� (���� 1)
	texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // ���� (����: HDR ����)
	texDesc.Usage = D3D11_USAGE_DEFAULT;             // GPU���� ���
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS; // ���̴����� �б�/�����
	texDesc.CPUAccessFlags = 0;     // CPU ���� �� ��
	texDesc.MiscFlags = 0;

	HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateTexture3D(&texDesc, nullptr, &mTempVoxelInjectionTexture3D[0]);
	if (FAILED(hr)) {
		Debug->LogError("Failed to create 3D texture for volumetric fog read.");
	}
	hr = DirectX11::DeviceStates->g_pDevice->CreateTexture3D(&texDesc, nullptr, &mTempVoxelInjectionTexture3D[1]);
	if (FAILED(hr)) {
		Debug->LogError("Failed to create 3D texture for volumetric fog write.");
	}
	hr = DirectX11::DeviceStates->g_pDevice->CreateTexture3D(&texDesc, nullptr, &mFinalVoxelInjectionTexture3D);
	if (FAILED(hr)) {
		Debug->LogError("Failed to create 3D texture for volumetric fog final.");
	}

	// Shader Resource View
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
	srvDesc.Texture3D.MipLevels = texDesc.MipLevels;
	srvDesc.Texture3D.MostDetailedMip = 0;

	DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(mTempVoxelInjectionTexture3D[0], &srvDesc, &mTempVoxelInjectionTexture3DSRV[0]);
	DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(mTempVoxelInjectionTexture3D[1], &srvDesc, &mTempVoxelInjectionTexture3DSRV[1]);
	DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(mFinalVoxelInjectionTexture3D, &srvDesc, &mFinalVoxelInjectionTexture3DSRV);

	// Unordered Access View (for Compute Shader ��)
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
	uavDesc.Texture3D.MipSlice = 0;
	uavDesc.Texture3D.FirstWSlice = 0;
	uavDesc.Texture3D.WSize = texDesc.Depth;

	DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(mTempVoxelInjectionTexture3D[0], &uavDesc, &mTempVoxelInjectionTexture3DUAV[0]);
	DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(mTempVoxelInjectionTexture3D[1], &uavDesc, &mTempVoxelInjectionTexture3DUAV[1]);
	DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(mFinalVoxelInjectionTexture3D, &uavDesc, &mFinalVoxelInjectionTexture3DUAV);

	file::path path = file::path(fileName);
	if (file::exists(path))
	{
		m_pBlueNoiseTexture = Texture::LoadManagedFromPath(fileName);
	}

	m_CopiedTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"CopiedTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
	);
	m_CopiedTexture->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_CopiedTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void VolumetricFogPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void VolumetricFogPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	if (!isOn) return;
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);
	auto& lightManager = scene.m_LightController;
	ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)
	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	ID3D11ShaderResourceView* nullSRVall[3] = { nullptr, nullptr, nullptr };
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };

	context.SetComputeShader(m_pMainShader->GetShader());
	deferredPtr->CSSetSamplers(0, 1, &m_pClampSampler);
	deferredPtr->CSSetSamplers(1, 1, &m_pWrapSampler);
	deferredPtr->CSSetSamplers(2, 1, &m_pShadowSamper);

	int readIndex = mCurrentTexture3DRead;
	int writeIndex = !mCurrentTexture3DRead;

	Mathf::Vector4 lightdir = scene.m_LightController->GetLight(0).m_direction;
	Mathf::Color4  lightColor = scene.m_LightController->GetLight(0).m_color;
	lightColor.w = scene.m_LightController->GetLight(0).m_intencity;
	Mathf::Matrix viewProjMat = camera.CalculateView() * camera.CalculateProjection();

	//GIT_COMBINE_WARN_BEGIN : shadowMapPass �ڵ� ������ ���� ���� ����Ǿ����� ���� �� Ȯ�� �ٶ�. by Hero.P
	auto shadowMapPass = scene.m_LightController->GetShadowMapPass();
	auto& cascadeInfo = camera.m_cascadeinfo[2];
	auto& useCascade = shadowMapPass->m_useCascade;

	MainCB data{};
	data.InvViewProj = XMMatrixTranspose(XMMatrixInverse(nullptr, viewProjMat));
	data.PrevViewProj = mPrevViewProj;
	data.ShadowMatrix = cascadeInfo.m_lightViewProjection;
	data.SunDirection = -lightdir;
	data.SunColor = lightColor;
	data.CameraPosition = XMFLOAT4{ camera.m_eyePosition.m128_f32[0], camera.m_eyePosition.m128_f32[1], camera.m_eyePosition.m128_f32[2], 1.0f };
	data.CameraNearFar_FrameIndex_PreviousFrameBlend = XMFLOAT4{ mCustomNearPlane, mCustomFarPlane, static_cast<float>(Time->GetFrameCount()), mPreviousFrameBlendFactor };
	data.VolumeSize = XMFLOAT4{ static_cast<float>(mCurrentVoxelVolumeSizeX), static_cast<float>(mCurrentVoxelVolumeSizeY), VOXEL_VOLUME_SIZE_Z, 0.0f };
	data.Anisotropy = mAnisotropy;
	data.Density = mDensity;
	data.Strength = mStrength;
	data.ThicknessFactor = mThicknessFactor;
	//GIT_COMBINE_WARN_END : shadowMapPass �ڵ� ������ ���� ���� ����Ǿ����� ���� �� Ȯ�� �ٶ�. by Hero.P
	if (lightManager->hasLightWithShadows) {
		lightManager->CSBindCloudShadowMap(deferredPtr);
	}
	context.UpdateBuffer(m_Buffer.Get(), &data);
	context.UpdateBuffer(m_lightBuffer.Get(), &lightManager->GetProperties());
	context.SetComputeConstantBuffer(0, m_Buffer.Get());
	context.SetComputeConstantBuffer(2, m_lightBuffer.Get());
	context.SetComputeShaderResource(0, renderData->m_shadowMapTexture->m_pSRV);
	context.SetComputeShaderResource(1, m_pBlueNoiseTexture->m_pSRV);
	context.SetComputeShaderResource(2, mTempVoxelInjectionTexture3DSRV[readIndex]);
	context.SetComputeUnorderedAccessView(0, mTempVoxelInjectionTexture3DUAV[writeIndex]);
	context.Dispatch(
		(UINT)ceil(mCurrentVoxelVolumeSizeX / 8.0f),
		(UINT)ceil(mCurrentVoxelVolumeSizeY / 8.0f),
		VOXEL_VOLUME_SIZE_Z
	);

	context.SetComputeShaderResources(2, 1, reinterpret_cast<RHINativeShaderResource const*>(nullSRV));
	context.SetComputeUnorderedAccessViews(0, 1, reinterpret_cast<RHINativeUnorderedAccess const*>(nullUAV), nullptr);
	mCurrentTexture3DRead = !mCurrentTexture3DRead;

	// accumulate
	readIndex = mCurrentTexture3DRead;
	context.SetComputeShader(m_pAccumulationShader->GetShader());
	deferredPtr->CSSetSamplers(0, 1, &m_pWrapSampler);
	context.SetComputeShaderResource(2, mTempVoxelInjectionTexture3DSRV[readIndex]);
	context.SetComputeUnorderedAccessView(0, mFinalVoxelInjectionTexture3DUAV);
	context.Dispatch(
		(UINT)ceil(mCurrentVoxelVolumeSizeX / 8.0f),
		(UINT)ceil(mCurrentVoxelVolumeSizeY / 8.0f),
		1
	);

	context.SetComputeShaderResources(0, 3, reinterpret_cast<RHINativeShaderResource const*>(nullSRVall));
	context.SetComputeUnorderedAccessViews(0, 1, reinterpret_cast<RHINativeUnorderedAccess const*>(nullUAV), nullptr);

	mPrevViewProj = XMMatrixTranspose(camera.CalculateView() * camera.CalculateProjection());

	// composite
	m_pso->Apply(deferredPtr);
	ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
	context.SetRenderTarget(view, nullptr);
	{
		const D3D11_VIEWPORT& vpRef = DirectX11::DeviceStates->g_Viewport;
		const RHIViewport rhiVp{ vpRef.TopLeftX, vpRef.TopLeftY, vpRef.Width, vpRef.Height, vpRef.MinDepth, vpRef.MaxDepth };
		context.SetViewports(1, &rhiVp);
	}
	CompositeCB compositeData{};
	compositeData.ViewProj = XMMatrixTranspose(camera.CalculateView() * camera.CalculateProjection());
	compositeData.InvView = camera.CalculateInverseView();
	compositeData.InvProj = camera.CalculateInverseProjection();
	compositeData.CameraNearFar = XMFLOAT4{ mCustomNearPlane, mCustomFarPlane, 0.0f, 0.0f };
	compositeData.VolumeSize = data.VolumeSize;
	compositeData.BlendingWithSceneColorFactor = mBlendingWithSceneColorFactor;
	context.UpdateBuffer(m_CompositeBuffer.Get(), &compositeData);
	context.SetPixelShaderConstantBuffer(0, m_CompositeBuffer.Get());

	context.CopyResource(m_CopiedTexture->m_pTexture, renderData->m_renderTarget->m_pTexture);
	ID3D11ShaderResourceView* pTextures[3] = {
		m_CopiedTexture->m_pSRV,
		renderData->m_depthStencil->m_pSRV,
		mFinalVoxelInjectionTexture3DSRV
	};
	context.SetPixelShaderResources(0, 3, reinterpret_cast<RHINativeShaderResource const*>(pTextures));

	context.Draw(4, 0);
	context.SetPixelShaderResources(0, 3, reinterpret_cast<RHINativeShaderResource const*>(nullSRVall));

	ID3D11CommandList* commandList{};
	deferredPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void VolumetricFogPass::ControlPanel()
{
	ImGui::PushID(this);
	auto& setting = EngineSettingInstance->GetRenderPassSettingsRW().volumetricFog;

	ImGui::Text("VolumetricFogMain");
	if (ImGui::Checkbox("VolumetricFog", &isOn)) {
		setting.isOn = isOn;
	}
	if(ImGui::SliderFloat("Anisotropy", &mAnisotropy, 0.0f, 1.0f)){
		setting.mAnisotropy = mAnisotropy;
	}
	if(ImGui::SliderFloat("Density", &mDensity, 0.1f, 10.0f)){
		setting.mDensity = mDensity;
	}
	if (ImGui::SliderFloat("Strength", &mStrength, 0.0f, 50.0f)) {
		setting.mStrength = mStrength;
	}
	if (ImGui::SliderFloat("Thickness", &mThicknessFactor, 0.0f, 0.1f)) {
		setting.mThicknessFactor = mThicknessFactor;
	}
	if (ImGui::SliderFloat("Blending with scene", &mBlendingWithSceneColorFactor, 0.0f, 1.0f)) {
		setting.mBlendingWithSceneColorFactor = mBlendingWithSceneColorFactor;
	}
	if(ImGui::SliderFloat("Blending with previous frame", &mPreviousFrameBlendFactor, 0.0f, 1.0f)){
		setting.mPreviousFrameBlendFactor = mPreviousFrameBlendFactor;
	}
	if(ImGui::SliderFloat("Custom near plane", &mCustomNearPlane, 0.01f, 10.0f)){
		setting.mCustomNearPlane = mCustomNearPlane;
	}
	if(ImGui::SliderFloat("Custom far plane", &mCustomFarPlane, 10.0f, 10000.0f)){
		setting.mCustomFarPlane = mCustomFarPlane;
	}
	ImGui::PopID();
}

void VolumetricFogPass::ApplySettings(const VolumetricFogPassSetting& settings)
{
	mAnisotropy = settings.mAnisotropy;
	mDensity = settings.mDensity;
	mStrength = settings.mStrength;
	mThicknessFactor = settings.mThicknessFactor;
	mBlendingWithSceneColorFactor = settings.mBlendingWithSceneColorFactor;
	mPreviousFrameBlendFactor = settings.mPreviousFrameBlendFactor;
	mCustomNearPlane = settings.mCustomNearPlane;
	mCustomFarPlane = settings.mCustomFarPlane;
	isOn = settings.isOn;
}

void VolumetricFogPass::Resize(uint32_t width, uint32_t height)
{
}