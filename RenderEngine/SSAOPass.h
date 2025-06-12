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

class SSAOPass final : public IRenderPass
{
public:
	SSAOPass();
	~SSAOPass();

	void Initialize(Texture* renderTarget, ID3D11ShaderResourceView* depth, Texture* normal, Texture* diffuse);
	void ReloadDSV(ID3D11ShaderResourceView* depth);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

private:
    UniqueTexturePtr m_NoiseTexture{ TEXTURE_NULL_INITIALIZER };
	SSAOBuffer m_SSAOBuffer;
	ComPtr<ID3D11Buffer> m_Buffer;
	ID3D11ShaderResourceView* m_DepthSRV;

	Texture* m_NormalTexture;
	Texture* m_RenderTarget;
	Texture* m_DiffuseTexture;

	float radius = 0.1f;
	float thickness = 0.1f;
};
