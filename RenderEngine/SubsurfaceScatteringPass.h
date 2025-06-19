#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
class SubsurfaceScatteringPass final : public IRenderPass
{
public:
	SubsurfaceScatteringPass();
	~SubsurfaceScatteringPass();

	void Initialize(Texture* diffuse, Texture* metalRough);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;

private:
	Texture* m_DiffuseTexture{};
	Texture* m_MetalRoughTexture{};
	Texture* m_CopiedTexture{};

	ComPtr<ID3D11Buffer> m_Buffer{};
	float2 direction{ 1.f, 0.f };
	float strength{ 1.35f };
	float width{ 0.013f };

	bool isOn{ true };
};

