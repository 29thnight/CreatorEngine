#pragma once
#include "ParticleSystem.h"
#include "../ShaderSystem.h"
#include "Effects.h"
#include "BillboardModuleGPU.h"

class SparkleEffect : public ParticleSystem
{
public:
    SparkleEffect(const Mathf::Vector3& position, int maxParticles = 100);

    ~SparkleEffect();

    void InitializeModules();

    void Update(float delta) override;

    void Render(RenderScene& scene, Camera& camera) override;

    void SpawnSparklesBurst(int count);

    void UpdateInstanceData();
private:
    BillboardModuleGPU* m_billboardModule;

    float m_delta;
    Texture* m_sparkleTexture;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    float m_rate = 0;
    UINT m_max = 0;
};