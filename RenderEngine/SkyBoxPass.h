#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "Mesh.h"

class SkyBoxPass final : public IRenderPass
{
public:
    SkyBoxPass();
    ~SkyBoxPass();

    void Initialize(const std::string_view& fileName, float size = 25.f);
	void SetRenderTarget(Texture* renderTarget);
	void SetBackBuffer(ID3D11RenderTargetView* backBuffer);
    void GenerateCubeMap(RenderScene& scene);
    Texture* GenerateEnvironmentMap(RenderScene& scene);
    Texture* GeneratePrefilteredMap(RenderScene& scene);
    Texture* GenerateBRDFLUT(RenderScene& scene);

    std::unique_ptr<Texture> m_EnvironmentMap{};
    std::unique_ptr<Texture> m_SpecularMap{};
    std::unique_ptr<Texture> m_BRDFLUT{};

    void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize() override;

private:
    //skybox ���̴��� �ش� pass�� �⺻ pso�� ������Ű��
	VertexShader* m_fullscreenVS{};
	PixelShader* m_irradiancePS{};
	PixelShader* m_prefilterPS{};
	PixelShader* m_brdfPS{};
	ID3D11RasterizerState* m_skyBoxRasterizerState{};
	ID3D11RenderTargetView* m_backBuffer{};

	PixelShader* m_rectToCubeMapPS{};

	std::unique_ptr<Mesh> m_skyBoxMesh{};

	std::unique_ptr<Texture> m_skyBoxTexture{};
	std::unique_ptr<Texture> m_skyBoxCubeMap{};

	Mathf::xMatrix m_scaleMatrix{};
	Texture* m_RenderTarget{};
	bool m_cubeMapGenerationRequired{ true };
	float m_size{ 25.f };
	float m_scale{ 40.f };
	int m_cubeMapSize{ 512 };
};
