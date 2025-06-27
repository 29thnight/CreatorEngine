// MeshSpawnModuleCS.h - 변경 필요한 부분들

#pragma once
#include "ParticleModule.h"

struct alignas(16) MeshParticleTemplateParams
{
    float lifeTime;
    float3 pad1;                      

    float3 minScale;                  
    float pad2;
    float3 maxScale;                  
    float pad3;

    float3 minRotationSpeed;          
    float pad4;
    float3 maxRotationSpeed;          
    float pad5;

    float3 minInitialRotation;        
    float pad6;
    float3 maxInitialRotation;        
    float pad7;

    float4 color;                     

    float3 velocity;                  
    float pad8;
    float3 acceleration;              
    float pad9;

    float minVerticalVelocity;        
    float maxVerticalVelocity;        
    float horizontalVelocityRange;    
    float pad10;
};

class MeshSpawnModuleCS : public ParticleModule
{
private:
    ID3D11ComputeShader* m_computeShader;  // "MeshSpawnModule" 로드

    // 상수 버퍼들 (동일)
    ID3D11Buffer* m_spawnParamsBuffer;
    ID3D11Buffer* m_templateBuffer;

    // 난수 및 시간 관리 버퍼들 (동일)
    ID3D11Buffer* m_randomStateBuffer;
    ID3D11UnorderedAccessView* m_randomStateUAV;

    SpawnParams m_spawnParams;                           // 동일
    MeshParticleTemplateParams m_meshParticleTemplate;   // 변경!

    // 상태 관리 (동일)
    bool m_spawnParamsDirty;
    bool m_templateDirty;
    bool m_isInitialized;
    UINT m_particleCapacity;

    // 난수 생성기 (동일)
    std::random_device m_randomDevice;
    std::mt19937 m_randomGenerator;
    std::uniform_real_distribution<float> m_uniform;

public:
    MeshSpawnModuleCS();
    virtual ~MeshSpawnModuleCS();

    // ParticleModule 인터페이스 구현
    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;

    // 스폰 설정 메서드들
    void SetSpawnRate(float rate);
    void SetEmitterType(EmitterType type);
    void SetEmitterSize(const XMFLOAT3& size);
    void SetEmitterRadius(float radius);

    // 3D 메시 관련 설정 메서드들
    void SetParticleLifeTime(float lifeTime);
    void SetParticleScaleRange(const XMFLOAT3& minScale, const XMFLOAT3& maxScale);          
    void SetParticleRotationSpeedRange(const XMFLOAT3& minSpeed, const XMFLOAT3& maxSpeed);  
    void SetParticleInitialRotationRange(const XMFLOAT3& minRot, const XMFLOAT3& maxRot);    
    void SetParticleColor(const XMFLOAT4& color);
    void SetParticleVelocity(const XMFLOAT3& velocity);
    void SetParticleAcceleration(const XMFLOAT3& acceleration);
    void SetVelocityRange(float minVertical, float maxVertical, float horizontalRange);

    // 상태 조회
    float GetSpawnRate() const { return m_spawnParams.spawnRate; }
    EmitterType GetEmitterType() const { return static_cast<EmitterType>(m_spawnParams.emitterType); }
    MeshParticleTemplateParams GetTemplate() const { return m_meshParticleTemplate; }

private:
    bool InitializeComputeShader();   // "MeshSpawnModule" 셰이더 로드
    bool CreateConstantBuffers();     // MeshParticleTemplateParams 크기로 변경
    bool CreateUtilityBuffers();      // 동일
    void UpdateConstantBuffers(float deltaTime);  // 동일
    void ReleaseResources();          // 동일
};
