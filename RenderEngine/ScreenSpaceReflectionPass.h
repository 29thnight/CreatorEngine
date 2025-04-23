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
	virtual void Resize() override;
private:
	ID3D11RenderTargetView* m_renderTargetViews[RTV_TypeMax]{}; //0: diffuse, 1: metalRough, 2: normal, 3: emissive
	ID3D11RenderTargetView* m_editorRTV[RTV_TypeMax]{}; //0: diffuse, 1: metalRough, 2: normal, 3: emissive
	Texture* m_DiffuseTexture{};
	Texture* m_MetalRoughTexture{};
	Texture* m_NormalTexture{};
	Texture* m_EmissiveTexture{};

	Texture* m_CopiedTexture{};

	ComPtr<ID3D11Buffer> m_Buffer{};
	float stepSize = 1.0f;//0.047f;
	float MaxThickness = 0.002f;//0.022f;
	//float time = 0.016f;
	int maxRayCount = 100;

	bool isOn{ true };
};

