#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class GizmoPass : public IRenderPass
{
public:
	GizmoPass();
	~GizmoPass() override = default;

	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize() override;

private:
	ComPtr<ID3D11Buffer> m_gizmoCameraBuffer{};
	ComPtr<ID3D11Buffer> m_positionBuffer{};
	ComPtr<ID3D11Buffer> m_sizeBuffer{};
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
};