#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
class DeferredPass final : public IRenderPass
{
public:
    DeferredPass();
    ~DeferredPass();
    void Initialize(Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive);
    void UseAmbientOcclusion(Texture* aoMap);
    void UseEnvironmentMap(Texture* envMap, Texture* preFilter, Texture* brdfLut);
    void DisableAmbientOcclusion();
    void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
    virtual void ReloadShaders() override;
    virtual void Resize() override;

private:
    Texture* m_DiffuseTexture{};
    Texture* m_MetalRoughTexture{};
    Texture* m_NormalTexture{};
    Texture* m_EmissiveTexture{};
    Texture* m_AmbientOcclusionTexture{};
    Texture* m_EnvironmentMap{};
    Texture* m_PreFilter{};
    Texture* m_BrdfLut{};

    bool m_UseAmbientOcclusion{ true };
    bool m_UseEnvironmentMap{ true };
	bool m_UseLightWithShadows{ true };
	float m_envMapIntensity{ 1.f };

    ComPtr<ID3D11Buffer> m_Buffer{};
    ComPtr<ID3D11Buffer> m_shadowcamBuffer{};
};
