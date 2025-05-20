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
	void Resize(uint32_t width, uint32_t height) override;

private:
    Texture* GetLightIcon(int lightType, bool isMainLight) const;

private:
    Texture* MainLightIcon{};
    Texture* PointLightIcon{};
    Texture* SpotLightIcon{};
    Texture* DirectionalLightIcon{};
    Texture* CameraIcon{};

	ComPtr<ID3D11Buffer> m_gizmoCameraBuffer{};
	ComPtr<ID3D11Buffer> m_positionBuffer{};
	ComPtr<ID3D11Buffer> m_sizeBuffer{};
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
};
