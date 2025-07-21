#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
struct VignettePassSetting;
class VignettePass final : public IRenderPass
{
public:
	VignettePass();
	~VignettePass();

	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
        void ApplySettings(const VignettePassSetting& setting);
private:
	Texture* m_CopiedTexture{};
	ComPtr<ID3D11Buffer> m_Buffer{};
	float radius = 0.75f;
	float softness = 0.5f;

	bool isOn = true;
};
