#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
class VignettePass final : public IRenderPass
{
public:
	VignettePass();
	~VignettePass();

	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	virtual void Resize() override;
private:
	Texture* m_CopiedTexture{};
	ComPtr<ID3D11Buffer> m_Buffer{};
	float radius = 0.75f;
	float softness = 0.5f;

	bool isOn = true;
};
