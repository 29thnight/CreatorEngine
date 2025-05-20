#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "Mesh.h"
#include "Camera.h"

class SpritePass final : public IRenderPass
{
public:
	SpritePass();
	~SpritePass();

	void SetGizmoRendering(bool isGizmoRendering) { m_isGizmoRendering = isGizmoRendering; }
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

private:
	bool m_isGizmoRendering{ false };
	std::unique_ptr<Mesh> m_QuadMesh{};
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
};