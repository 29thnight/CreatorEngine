#pragma once
#include "Core.Minimal.h"
#include "DeviceResources.h"
#include "ShadowMapPass.h"
#include "GBufferPass.h"

#include "Light.h"

class Scene;
class SceneRenderer
{
public:
	SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);

	void Initialize(Scene* _pScene = nullptr);
	void Render();

private:
	void Clear(const float color[4], float depth, uint8_t stencil);
	void SetRenderTargets(Texture& texture, bool enableDepthTest = true);
	void UnbindRenderTargets();

	Scene* m_currentScene{};
	std::shared_ptr<DirectX11::DeviceResources> m_deviceResources{};

	//pass
	std::unique_ptr<ShadowMapPass> m_pShadowMapPass{};
	std::unique_ptr<GBufferPass> m_pGBufferPass{};

	//buffers
	ComPtr<ID3D11Buffer> m_ModelBuffer;
	ComPtr<ID3D11Buffer> m_ViewBuffer;
	ComPtr<ID3D11Buffer> m_ProjBuffer;

	//textures
	std::unique_ptr<Texture> m_colorTexture;
	std::unique_ptr<Texture> m_diffuseTexture;
	std::unique_ptr<Texture> m_metalRoughTexture;
	std::unique_ptr<Texture> m_normalTexture;
	std::unique_ptr<Texture> m_emissiveTexture;
	std::unique_ptr<Texture> m_ambientOcclusionTexture;
	std::unique_ptr<Texture> m_toneMappedColourTexture;
};