#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct alignas(16) CameraBuffer
{
	Mathf::Vector4 m_CameraPosition;
};

class WireFramePass : public IRenderPass
{
public:
	WireFramePass();
	~WireFramePass();

	void SetRenderTarget(Texture* renderTarget);

	// IRenderPass을(를) 통해 상속됨
	void Execute(Scene& scene) override;

private:
	CameraBuffer m_CameraBuffer;
	ComPtr<ID3D11Buffer> m_Buffer;

	Texture* m_RenderTarget{};
};

