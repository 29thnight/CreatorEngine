#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "Mesh.h"

class SkyBoxPass final : public IRenderPass
{
public:
    SkyBoxPass();
    ~SkyBoxPass();

    void Initialize(std::string_view fileName, float size = 25.f);
	void SetRenderTarget(Texture* renderTarget);
	void SetBackBuffer(ID3D11RenderTargetView* backBuffer);
    void GenerateCubeMap(RenderScene& scene);
	void GenerateCubeMap(std::string_view fileName, RenderScene& scene);
	Managed::SharedPtr<Texture> GenerateEnvironmentMap(RenderScene& scene);
    Managed::SharedPtr<Texture> GeneratePrefilteredMap(RenderScene& scene);
    Managed::SharedPtr<Texture> GenerateBRDFLUT(RenderScene& scene);

    void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

	file::path CurrentSkyBoxTextureName() const { return m_fileName; }

public:
	Managed::SharedPtr<Texture> m_EnvironmentMap;
	Managed::SharedPtr<Texture> m_SpecularMap;
	Managed::SharedPtr<Texture> m_BRDFLUT;

private:
    //skybox 쉐이더는 해당 pass의 기본 pso에 고정
	VertexShader*				m_fullscreenVS{};
	PixelShader*				m_irradiancePS{};
	PixelShader*				m_prefilterPS{};
	PixelShader*				m_brdfPS{};
	ID3D11RenderTargetView*		m_backBuffer{};
	PixelShader*				m_rectToCubeMapPS{};
	std::unique_ptr<Mesh>		m_skyBoxMesh{};

	Managed::UniquePtr<Texture>	m_skyBoxTexture{};
	Managed::UniquePtr<Texture>	m_skyBoxCubeMap{};

	Camera						ortho{ false };
	Mathf::xMatrix				m_scaleMatrix{};
	Texture*					m_RenderTarget{};
	file::path					m_fileName{};
	bool						m_cubeMapGenerationRequired{ true };
	float						m_size{ 25.f };
	float						m_scale{ 40.f };
	int							m_cubeMapSize{ 512 };
};
