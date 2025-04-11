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
	// IRenderPass을(를) 통해 상속됨
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize() override;

private:
	CameraBuffer m_CameraBuffer;
	ComPtr<ID3D11Buffer> m_Buffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;

	Texture* m_RenderTarget{};
};

