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
	// dNormal이다. 여기 있던 Normal은 IRenderPass.h의 RTV_Type::Normal이었고,
	// 그 값이 우연히 2(= dNormal)라 결과가 맞아떨어졌을 뿐이다. RTV_Type에
	// 멤버가 하나 끼어들면 All이 7보다 작아지고 MAX가 줄어, 세 텍스처를 다
	// 쓰는 데칼이 m_pBlendStates[7]로 배열 밖을 짚는다.
	Normal_ORM = dNormal | dORM,
	All = dDiffuse | dNormal | dORM,
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
	void CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera) override;
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

