#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
class DeferredPass final : public IRenderPass
{
public:
    DeferredPass();
    ~DeferredPass();
    void Initialize(Texture* renderTarget, Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive);
	void EditorInitialize(Texture* renderTarget, Texture* diffuse, Texture* metalRough, Texture* normals, Texture* emissive);
    void UseAmbientOcclusion(Texture* aoMap);
    void UseEnvironmentMap(Texture* envMap, Texture* preFilter, Texture* brdfLut);
    void DisableAmbientOcclusion();
    void Execute(Scene& scene) override;
	void ExecuteEditor(Scene& scene, Camera& camera);
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

	Texture* m_EditorRenderTarget{};
	Texture* m_EditorDiffuseTexture{};
	Texture* m_EditorMetalRoughTexture{};
	Texture* m_EditorNormalTexture{};
	Texture* m_EditorEmissiveTexture{};

    bool m_UseAmbientOcclusion{ true };
    bool m_UseEnvironmentMap{ true };

    ComPtr<ID3D11Buffer> m_Buffer{};
};
