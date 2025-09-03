#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct alignas(16) SSAOBuffer
{
	Mathf::xMatrix m_ViewProjection;
	Mathf::xMatrix m_InverseViewProjection;
	Mathf::xMatrix m_InverseProjection;
	Mathf::Vector4 m_SampleKernel[64];
	Mathf::Vector4 m_CameraPosition;
	float m_Radius;
	float m_Thickness;
	Mathf::Vector2 m_windowSize;
	UINT m_frameIndex;
};

struct SSAOPassSetting;
class SSAOPass final : public IRenderPass
{
public:
	SSAOPass();
	~SSAOPass();

	void Initialize(Managed::SharedPtr<Texture> renderTarget, ID3D11ShaderResourceView* depth, Managed::SharedPtr<Texture> normal, Managed::SharedPtr<Texture> diffuse);
	void ReloadDSV(ID3D11ShaderResourceView* depth);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
        void ApplySettings(const SSAOPassSetting& setting);
	void Resize(uint32_t width, uint32_t height) override;

private:
    Managed::UniquePtr<Texture> m_NoiseTexture;
	SSAOBuffer					m_SSAOBuffer;
	ComPtr<ID3D11Buffer>		m_Buffer;
	ID3D11ShaderResourceView*	m_DepthSRV;

	std::weak_ptr<Texture>		m_NormalTexture;
	std::weak_ptr<Texture>		m_RenderTarget;
	std::weak_ptr<Texture>		m_DiffuseTexture;

	float						radius = 0.5f;
	float						thickness = 0.5f;
};
