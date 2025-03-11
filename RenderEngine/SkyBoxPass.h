#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "Mesh.h"

class SkyBoxPass final : public IRenderPass
{
public:
    SkyBoxPass();
    ~SkyBoxPass();

    void Initialize(const std::string_view& fileName, float size = 50.f);
	void SetRenderTarget(Texture* renderTarget);
    void GenerateCubeMap(Scene& scene);
    Texture* GenerateEnvironmentMap(Scene& scene);
    Texture* GeneratePrefilteredMap(Scene& scene);
    Texture* GenerateBRDFLUT(Scene& scene);

    std::unique_ptr<Texture> m_EnvironmentMap{};
    std::unique_ptr<Texture> m_SpecularMap{};
    std::unique_ptr<Texture> m_BRDFLUT{};

    void Execute(Scene& scene) override;

private:
    //skybox ���̴��� �ش� pass�� �⺻ pso�� ������Ű��
	VertexShader* m_fullscreenVS{};
	PixelShader* m_irradiancePS{};
	PixelShader* m_prefilterPS{};
	PixelShader* m_brdfPS{};
	ID3D11RasterizerState* m_skyBoxRasterizerState{};

	PixelShader* m_rectToCubeMapPS{};

	std::unique_ptr<Mesh> m_skyBoxMesh{};

	std::unique_ptr<Texture> m_skyBoxTexture{};
	std::unique_ptr<Texture> m_skyBoxCubeMap{};

	Mathf::xMatrix m_scaleMatrix{};
	Texture* m_RenderTarget{};
	bool m_cubeMapGenerationRequired{ true };
	int m_cubeMapSize{ 512 };
};
