#pragma once
#include "ParticleModule.h"

class MovementModuleCS : public ParticleModule
{
public:
    MovementModuleCS(bool useGravity = false, float gravityStrength = 1.0f)
        : m_gravity(useGravity), m_gravityStrength(gravityStrength),
        m_computeShader(nullptr), m_movementParamsBuffer(nullptr),
        m_isInitialized(false), m_paramsDirty(true), m_easingEnabled(false),
        m_easingType(0)
    {
    }

    ~MovementModuleCS()
    {
        Release();
    }

    // ParticleModule methods
    void Initialize() override;
    void Update(float delta, std::vector<ParticleData>& particles) override;
    void OnSystemResized(UINT max) override;

    // Movement settings
    void SetUseGravity(bool use) { m_gravity = use; m_paramsDirty = true;  std::cout << "asd"; }
    void SetGravityStrength(float strength) { m_gravityStrength = strength; m_paramsDirty = true; }
    void SetEasingEnabled(bool enabled) { m_easingEnabled = enabled; m_paramsDirty = true; }
    void SetEasingType(int type) { m_easingType = type; m_paramsDirty = true; }

    // Compute shader methods
    bool InitializeCompute();

private:
    void UpdateConstantBuffers(float delta);
    void Release();

    // Parameters structure (constant buffer)
    struct alignas(16) MovementParams
    {
        float deltaTime;
        float gravityStrength;
        int useGravity;
        int easingEnabled;
        int easingType;
        float3 pad;
    };

private:
    // Basic movement properties
    bool m_gravity;
    float m_gravityStrength;
    bool m_easingEnabled;
    int m_easingType;

    // Compute shader related variables
    ID3D11ComputeShader* m_computeShader;
    ID3D11Buffer* m_movementParamsBuffer;

    // State tracking
    bool m_isInitialized;
    bool m_paramsDirty;
    UINT m_particlesCapacity = 0;
};