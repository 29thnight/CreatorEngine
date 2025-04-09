#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct alignas(16) SSAOBuffer
{
	Mathf::xMatrix m_ViewProjection;
	Mathf::xMatrix m_InverseViewProjection;
	Mathf::Vector4 m_SampleKernel[64];
	Mathf::Vector4 m_CameraPosition;
	float m_Radius;
	Mathf::Vector2 m_windowSize;
};

class SSAOPass final : public IRenderPass
{
public:
	SSAOPass();
	~SSAOPass();

	void Initialize(Texture* renderTarget, ID3D11ShaderResourceView* depth, Texture* normal);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void ReloadShaders() override;
	void Resize() override;

private:
	std::unique_ptr<Texture> m_NoiseTexture;
	SSAOBuffer m_SSAOBuffer;
	ComPtr<ID3D11Buffer> m_Buffer;
	ID3D11ShaderResourceView* m_DepthSRV;

	Texture* m_NormalTexture;
	Texture* m_RenderTarget;
};