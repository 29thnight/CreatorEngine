#pragma once
#include "ParticleModule.h"

class LifeModuleCS : public ParticleModule
{
public:
    LifeModuleCS()
        : m_computeShader(nullptr), m_lifeParamsBuffer(nullptr),
        m_isInitialized(false), m_paramsDirty(true),
        m_inactiveIndicesUAV(nullptr), m_inactiveCountUAV(nullptr)
    {
        SetStage(ModuleStage::LIFE); 
    }

    ~LifeModuleCS()
    {
        Release();
    }

    void Initialize() override;
    void Update(float delta, std::vector<ParticleData>& particles) override;
    void OnSystemResized(UINT max) override;

    bool InitializeCompute();

    void SetSharedBuffers(
        ID3D11UnorderedAccessView* inactiveIndicesUAV,
        ID3D11UnorderedAccessView* inactiveCountUAV,
        ID3D11UnorderedAccessView* activeCountUAV)
    {
        // 공유 버퍼 설정 - ParticleSystem에서 호출하여 공유 버퍼 전달
        m_inactiveIndicesUAV = inactiveIndicesUAV;
        m_inactiveCountUAV = inactiveCountUAV;
        m_activeCountUAV = activeCountUAV;
    }

    UINT GetActiveParticleCount();


private:
    void UpdateConstantBuffers(float delta);
    void Release();

    struct alignas(16) LifeParams
    {
        float deltaTime;
        uint32_t maxParticles;
        uint32_t resetInactiveCounter;
        float pad;
    };

private:
    ID3D11ComputeShader* m_computeShader;
    ID3D11Buffer* m_lifeParamsBuffer;

    ID3D11UnorderedAccessView* m_inactiveIndicesUAV;
    ID3D11UnorderedAccessView* m_inactiveCountUAV;
    ID3D11UnorderedAccessView* m_activeCountUAV;

    bool m_isInitialized;
    bool m_paramsDirty;
    UINT m_particlesCapacity = 0;

    // 스테이징 버퍼
    ID3D11Buffer* m_activeCountStagingBuffer = nullptr;
};

