#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
class ShadowMapPass;
struct VolumetricFogPassSetting;
//class VolumetricFogMain final : public IRenderPass
//{
//	
//public:
//	VolumetricFogMain();
//	~VolumetricFogMain();
//	void Initialize(std::string_view fileName, UINT volumeSizeX, UINT volumeSizeY);
//	void Prepare();
//	void Execute(RenderScene& scene, Camera& camera) override;
//	void ControlPanel() override;
//	virtual void Resize() override;
//
//public:
//};
//
//class VolumetricFogComposite final : public IRenderPass {
//public:
//	VolumetricFogComposite();
//	~VolumetricFogComposite();
//	void Initialize();
//	void Prepare();
//	void Execute(RenderScene& scene, Camera& camera) override;
//	void ControlPanel() override;
//	virtual void Resize() override;
//private:
//	ComPtr<ID3D11Buffer> m_Buffer{};
//};

class VolumetricFogPass final : public IRenderPass
{
public:
	VolumetricFogPass();
	~VolumetricFogPass();
	void Initialize(std::string_view fileName);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	virtual void ApplySettings(const VolumetricFogPassSetting& settings);
	virtual void Resize(uint32_t width, uint32_t height) override;
private:
	bool isOn{ true };

	float mAnisotropy = 0.109f;
	float mDensity = 0.101f;
	float mStrength = 2.0f;
	float mThicknessFactor = 0.01f;
	float mBlendingWithSceneColorFactor = 0.851f;
	float mPreviousFrameBlendFactor = 0.95f;

	float mCustomNearPlane = 0.5f;
	float mCustomFarPlane = 1000.0f;

	UINT mCurrentVoxelVolumeSizeX = 0;
	UINT mCurrentVoxelVolumeSizeY = 0;

public:
	ComputeShader* m_pMainShader{};
	ComputeShader* m_pAccumulationShader{};

	ID3D11SamplerState* m_pClampSampler{};
	ID3D11SamplerState* m_pWrapSampler{};

	ID3D11SamplerState* m_pShadowSamper{};

	ID3D11Texture3D* mTempVoxelInjectionTexture3D[2];
	ID3D11Texture3D* mFinalVoxelInjectionTexture3D{};

	Texture* m_pShadowMap{};
	Managed::UniquePtr<Texture> m_pBlueNoiseTexture{};

	ID3D11ShaderResourceView* mTempVoxelInjectionTexture3DSRV[2];
	ID3D11UnorderedAccessView* mTempVoxelInjectionTexture3DUAV[2];
	ID3D11ShaderResourceView* mFinalVoxelInjectionTexture3DSRV{};
	ID3D11UnorderedAccessView* mFinalVoxelInjectionTexture3DUAV{};

	//std::weak_ptr<ShadowMapPass> m_pShadowMapPass{ nullptr };

	ComPtr<ID3D11Buffer> m_Buffer{};
	ComPtr<ID3D11Buffer> m_CompositeBuffer{};

	ComPtr<ID3D11Buffer> m_lightBuffer{};

	XMMATRIX mPrevViewProj;

	bool mCurrentTexture3DRead = false;

	Texture* m_CopiedTexture{};
};

