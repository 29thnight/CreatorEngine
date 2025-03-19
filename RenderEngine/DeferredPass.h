#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class DeferredPass final : public IRenderPass
{
public:
    DeferredPass();
    ~DeferredPass();
    void Initialize(Texture* renderTarget, Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive);
    void UseAmbientOcclusion(Texture* aoMap);
    void UseEnvironmentMap(Texture* envMap, Texture* preFilter, Texture* brdfLut);
    void DisableAmbientOcclusion();
    void Execute(Scene& scene) override;
	void ControlPanel() override;

private:
    Texture* m_RenderTarget{};
    Texture* m_DiffuseTexture{};
    Texture* m_MetalRoughTexture{};
    Texture* m_NormalTexture{};
    Texture* m_EmissiveTexture{};
    Texture* m_AmbientOcclusionTexture{};
    Texture* m_EnvironmentMap{};
    Texture* m_PreFilter{};
    Texture* m_BrdfLut{};

    bool m_UseAmbientOcclusion{};
    bool m_UseEnvironmentMap{};

    ComPtr<ID3D11Buffer> m_Buffer{};
};
