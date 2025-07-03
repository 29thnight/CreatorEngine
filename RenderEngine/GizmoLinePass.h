#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct LineVertex;
class GizmoLinePass : public IRenderPass
{
public:
	GizmoLinePass();
	~GizmoLinePass() override = default;

	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

private:
	void DrawWireCircleAndLines(const Mathf::Vector3& center, float radius, const Mathf::Vector3& up, const Mathf::Vector3& direction, const Mathf::Color4& color);
	void DrawWireCircle(const Mathf::Vector3& center, float radius, const Mathf::Vector3& up, const Mathf::Color4& color);
	void DrawLines(LineVertex* vertices, uint32_t vertexCount);
	void DrawWireSphere(const Mathf::Vector3& center, float radius, const Mathf::Color4& color);
	void DrawWireCone(const Mathf::Vector3& apex, const Mathf::Vector3& direction, float height, float outerConeAngle, const Mathf::Color4& color);
	void DrawBoundingFrustum(const DirectX::BoundingFrustum& frustum, const Mathf::Color4& color);

private:
	ComPtr<ID3D11Buffer> m_gizmoCameraBuffer{};
	ComPtr<ID3D11Buffer> m_lineVertexBuffer{};
	uint32_t m_lineVertexCount{};
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};

	bool m_isShowPhysicsDebugInfo{ false };
};
