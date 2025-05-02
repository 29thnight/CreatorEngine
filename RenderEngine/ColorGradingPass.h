#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
class ColorGradingPass final : public IRenderPass
{
public:
	ColorGradingPass();
	~ColorGradingPass();

	void Initialize(const std::string_view& fileName);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	virtual void Resize() override;

	UniqueTexturePtr m_pColorGradingTexture{};
private:
	Texture* m_pCopiedTexture{};
	ComPtr<ID3D11Buffer> m_Buffer{};
	float lerp = 0.f;
	bool isOn{ true };
};

