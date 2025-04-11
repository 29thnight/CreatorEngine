#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct alignas(16) FXAAParametersBuffer
{
	int2 TextureSize;
	float Bias{ 0.688f };
	float BiasMin{ 0.021f };
	float SpanMax{ 8.0f };
	float _spacer1{};
	float _spacer2{};
	float _spacer3{};
};

class Camera;
class AAPass final : public IRenderPass
{
public:
	AAPass();
	~AAPass();
	void SetAntiAliasingTexture(Texture* texture) { m_AntiAliasingTexture = texture; }
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	virtual void Resize() override;

private:
	Texture* m_CopiedTexture;
	Texture* m_AntiAliasingTexture;
	FXAAParametersBuffer m_FXAAParameters;
	ComPtr<ID3D11Buffer> m_FXAAParametersBuffer;
	bool m_isApply{ true };
};