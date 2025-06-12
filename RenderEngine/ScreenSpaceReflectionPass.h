#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
class ScreenSpaceReflectionPass final : public IRenderPass
{
public:
	ScreenSpaceReflectionPass();
	~ScreenSpaceReflectionPass();

	void Initialize(Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
private:
	Texture* m_DiffuseTexture{};
	Texture* m_MetalRoughTexture{};
	Texture* m_NormalTexture{};
	Texture* m_EmissiveTexture{};

	Texture* m_CopiedTexture{};
	Texture* m_prevSSRTexture{};
	Texture* m_prevCopiedSSRTexture{};

	ComPtr<ID3D11Buffer> m_Buffer{};
	float stepSize = 0.114f;//0.196f;//1.0f;//0.047f;
	float MaxThickness = 0.00416f;//0.00045f;//0.022f;
	//float time = 0.016f;
	int maxRayCount = 20;

	bool isOn{ true };
};

