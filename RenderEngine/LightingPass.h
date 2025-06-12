#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class LightingPass : public IRenderPass
{
public:
	LightingPass();
	~LightingPass();

	void Initialize(Texture* dest);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;

private:
	Texture* m_pLightingTexture{};
};

