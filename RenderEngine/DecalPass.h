#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct DecalVertex {
	Mathf::Vector3 position;
};

enum DecalChannel : uint32
{
	None = 0,
	dDiffuse = 1 << 0,
	dNormal = 1 << 1,
	dORM = 1 << 2,
	Diffuse_Normal = dDiffuse | dNormal,
	Diffuse_ORM = dDiffuse | dORM,
	Normal_ORM = Normal | dORM,
	All = dDiffuse | Normal | dORM,
	MAX
};

class Camera;
class DecalPass final : public IRenderPass
{
public:
	DecalPass();
	~DecalPass();

	void Initialize(Texture* diffuseTexture, Texture* normalTexture, Texture* ormTexture);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;

private:
	Texture* m_DiffuseTexture{};
	Texture* m_NormalTexture{};
	Texture* m_OccluRoughMetalTexture{};

	Texture* m_CopiedDepthTexture{};
	Texture* m_CopiedDiffuseTexture{};
	Texture* m_CopiedNormalTexture{};
	Texture* m_CopiedORMTexture{};

	ComPtr<ID3D11Buffer> m_Buffer{};
	ComPtr<ID3D11Buffer> m_decalBuffer{};

	Managed::UniquePtr<Texture> TestTexture{};

	ComPtr<ID3D11Buffer> m_vertexBuffer{};
	ComPtr<ID3D11Buffer> m_indexBuffer{};

	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
	ComPtr<ID3D11BlendState1> m_pBlendStates[DecalChannel::MAX]; // diffuse, normal, orm, diffuse+normal, diffuse+orm, normal+orm, all

	static constexpr const uint32 m_decalstride = sizeof(DecalVertex);
};

