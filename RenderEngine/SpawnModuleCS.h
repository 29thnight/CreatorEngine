#pragma once
#include "ParticleModule.h"
#include "ISerializable.h"

class EffectSerializer;
// ��ƼŬ ���ø� ����ü (��� ����)
struct alignas(16) ParticleTemplateParams
{
    float lifeTime;
    float rotateSpeed;
    float2 size;

    float4 color;

    float3 velocity;
    float pad1;

    float3 acceleration;
    float pad2;

    float minVerticalVelocity;
    float maxVerticalVelocity;
    float horizontalVelocityRange;
    float pad3;
};

enum class EmitterType;
class SpawnModuleCS : public ParticleModule, public ISerializable
{
private:
    // ��ǻƮ ���̴� ����
    ID3D11ComputeShader* m_computeShader;

    // ��� ���۵�
    ID3D11Buffer* m_spawnParamsBuffer;
    ID3D11Buffer* m_templateBuffer;

    // ���� �� �ð� ���� ���۵�
    ID3D11Buffer* m_randomStateBuffer;
    ID3D11UnorderedAccessView* m_randomStateUAV;
    ID3D11Buffer* m_spawnTimerBuffer;
    ID3D11UnorderedAccessView* m_spawnTimerUAV;

    // ���� �Ķ���͵�
    SpawnParams m_spawnParams;
    ParticleTemplateParams m_particleTemplate;

    // ���� ����
    bool m_spawnParamsDirty;
    bool m_templateDirty;
    bool m_isInitialized;
    UINT m_particleCapacity;

    // ���� ������ (�ʱ� �õ��)
    std::random_device m_randomDevice;
    std::mt19937 m_randomGenerator;
    std::uniform_real_distribution<float> m_uniform;

public:
    SpawnModuleCS();
    virtual ~SpawnModuleCS();

    // ParticleModule �������̽� ����
    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;

    // JSON ����ȭ�� �޼ҵ�� �߰� 
    const SpawnParams& GetSpawnParams() const { return m_spawnParams; }
    const ParticleTemplateParams& GetParticleTemplate() const { return m_particleTemplate; }
    bool IsInitialized() const { return m_isInitialized; }
    UINT GetParticleCapacity() const { return m_particleCapacity; }

    // ���� ���� �޼����
    void SetEmitterPosition(const Mathf::Vector3& position);
    void SetSpawnRate(float rate);
    void SetEmitterType(EmitterType type);
    void SetEmitterSize(const XMFLOAT3& size);
    void SetEmitterRadius(float radius);

    // ��ƼŬ ���ø� ����
    void SetParticleLifeTime(float lifeTime);
    void SetParticleSize(const XMFLOAT2& size);
    void SetParticleColor(const XMFLOAT4& color);
    void SetParticleVelocity(const XMFLOAT3& velocity);
    void SetParticleAcceleration(const XMFLOAT3& acceleration);
    void SetVelocityRange(float minVertical, float maxVertical, float horizontalRange);
    void SetRotateSpeed(float speed);

    // ���� ��ȸ
    float GetSpawnRate() const { return m_spawnParams.spawnRate; }
    EmitterType GetEmitterType() const { return static_cast<EmitterType>(m_spawnParams.emitterType); }
    ParticleTemplateParams GetTemplate() const { return m_particleTemplate; }

    // ISerializable �������̽� ����
    virtual nlohmann::json SerializeData() const override;
    

    virtual void DeserializeData(const nlohmann::json& json) override;
    

    virtual std::string GetModuleType() const override;
    
private:
    bool InitializeComputeShader();
    bool CreateConstantBuffers();
    bool CreateUtilityBuffers();
    void UpdateConstantBuffers(float deltaTime);
    void ReleaseResources();
};
