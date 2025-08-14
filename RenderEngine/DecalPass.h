#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct DecalVertex {
	Mathf::Vector3 position;
};

class Camera;
class DecalPass final : public IRenderPass
{
public:
	DecalPass();
	~DecalPass();

	void Initialize(Texture* diffuseTexture, Texture* normalTexture);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;

private:
	Texture* m_DiffuseTexture{};
	Texture* m_NormalTexture{};

	Texture* m_CopiedDepthTexture{};

	ComPtr<ID3D11Buffer> m_Buffer{};
	ComPtr<ID3D11Buffer> m_decalBuffer{};

	Managed::UniquePtr<Texture> TestTexture{};

	ComPtr<ID3D11Buffer> m_vertexBuffer{};
	ComPtr<ID3D11Buffer> m_indexBuffer{};

	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
	static constexpr const uint32 m_decalstride = sizeof(DecalVertex);
};

