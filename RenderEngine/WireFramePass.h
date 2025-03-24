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
	// IRenderPass��(��) ���� ��ӵ�
	void Execute(Scene& scene, Camera& camera) override;

private:
	CameraBuffer m_CameraBuffer;
	ComPtr<ID3D11Buffer> m_Buffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;

	Texture* m_RenderTarget{};
};

