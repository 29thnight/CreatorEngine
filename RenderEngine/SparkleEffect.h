#pragma once
#include "EffectModules.h"
#include "ShaderSystem.h"
#include "Effects.h"

struct SparkleParameters : public EffectParameters
{
    Mathf::Vector2 size;
    Mathf::Vector2 range;
};

class SparkleEffect : public EffectModules
{
public:
    SparkleEffect(const Mathf::Vector3& position, int maxParticles = 100);

    ~SparkleEffect();

    void InitializeModules();

    void Update(float delta) override;

    void Render(RenderScene& scene, Camera& camera) override;

    void SpawnSparklesBurst(int count);

    void UpdateConstantBuffer();

    void UpdateInstanceData();

    void SetParameters(SparkleParameters* params);

private:
    BillboardModule* m_billboardModule;

    float m_delta;
    SparkleParameters* m_sparkleParams;
    std::shared_ptr<Texture> m_sparkleTexture;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    SpawnModule* m_spawnModule;
};