#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
struct DeferredPassSetting;
class DeferredPass final : public IRenderPass
{
public:
    DeferredPass();
    ~DeferredPass();
    void Initialize(Managed::SharedPtr<Texture> diffuse, Managed::SharedPtr<Texture> metalRough, 
        Managed::SharedPtr<Texture> normals, Managed::SharedPtr<Texture> emissive, Managed::SharedPtr<Texture> bitmask);
    void UseAmbientOcclusion(Managed::SharedPtr<Texture> aoMap);
    void UseEnvironmentMap(Managed::SharedPtr<Texture> envMap, Managed::SharedPtr<Texture> preFilter, Managed::SharedPtr<Texture> brdfLut);
    void DisableAmbientOcclusion();
    void Execute(RenderScene& scene, Camera& camera) override;
    void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
        void ApplySettings(const DeferredPassSetting& setting);

    void UseLightAndEmissiveRTV(Managed::SharedPtr<Texture> lightEmissive);
private:
    Managed::WeakPtr<Texture> m_DiffuseTexture{};
    Managed::WeakPtr<Texture> m_MetalRoughTexture{};
    Managed::WeakPtr<Texture> m_NormalTexture{};
    Managed::WeakPtr<Texture> m_EmissiveTexture{};
    Managed::WeakPtr<Texture> m_BitmaskTexture{};
    Managed::WeakPtr<Texture> m_AmbientOcclusionTexture{};
    Managed::WeakPtr<Texture> m_EnvironmentMap{};
    Managed::WeakPtr<Texture> m_PreFilter{};
    Managed::WeakPtr<Texture> m_BrdfLut{};

    Managed::WeakPtr<Texture> m_LightEmissiveTexture{};

    bool m_UseAmbientOcclusion{ true };
    bool m_UseEnvironmentMap{ true };
	bool m_UseLightWithShadows{ true };
	float m_envMapIntensity{ 1.f };

    ComPtr<ID3D11Buffer> m_Buffer{};
    ComPtr<ID3D11Buffer> m_shadowcamBuffer{};

    bool showTextures = false;
};
