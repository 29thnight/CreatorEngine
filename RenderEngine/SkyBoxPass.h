#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class SkyBoxPass final : public IRenderPass
{
public:
    SkyBoxPass();
    ~SkyBoxPass();

    void Initialize(Texture* renderTarget, const std::string_view& fileName, float size = 50.f);
    void GenerateCubeMap(Scene& scene);
    Texture* GenerateEnvironmentMap(Scene& scene);
    Texture* GeneratePrefilteredMap(Scene& scene);
    Texture* GenerateBRDFLUT(Scene& scene);

    std::unique_ptr<Texture> m_EnvironmentMap{};
    std::unique_ptr<Texture> m_SpecularMap{};
    std::unique_ptr<Texture> m_BRDFLUT{};

    void Execute(Scene& scene) override;
};
