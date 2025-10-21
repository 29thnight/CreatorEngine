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
    float velocityRandomRange;

    float3 acceleration;
    float initialRotation;       // �ʱ� Z�� ȸ����

    float initialRotationRange;  // �ʱ� ȸ�� ���� ����
    float pad1;
    float pad2;
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
    UINT m_particleCapacity;
    bool m_allowNewSpawn = true;

    // ���� ������ (�ʱ� �õ��)
    std::random_device m_randomDevice;
    std::mt19937 m_randomGenerator;
    std::uniform_real_distribution<float> m_uniform;

    Mathf::Vector3 m_previousEmitterPosition;
    bool m_forcePositionUpdate;

    std::mutex m_resetMutex;    

    XMFLOAT3 m_originalEmitterSize;
    XMFLOAT2 m_originalParticleScale;

public:
    SpawnModuleCS();
    virtual ~SpawnModuleCS();

    // ParticleModule �������̽� ����
    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;
    virtual void OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition) override;

    virtual void ResetForReuse();
    virtual bool IsReadyForReuse() const;

    // JSON ����ȭ�� �޼ҵ�� �߰� 
    const SpawnParams& GetSpawnParams() const { return m_spawnParams; }
    const ParticleTemplateParams& GetParticleTemplate() const { return m_particleTemplate; }
    bool IsInitialized() const { return m_isInitialized; }
    UINT GetParticleCapacity() const { return m_particleCapacity; }

    // ���� ���� �޼����
    void SetEmitterPosition(const Mathf::Vector3& position, const Mathf::Vector3& worldPosition);
    void SetEmitterRotation(const Mathf::Vector3& rotation);
    void SetEmitterScale(const Mathf::Vector3& scale);

    void SetSpawnRate(float rate);
    void SetEmitterType(EmitterType type);
    void SetEmitterSize(const XMFLOAT3& size);
    void SetEmitterRadius(float radius);

    // ��ƼŬ ���ø� ����
    void SetParticleLifeTime(float lifeTime);
    void SetParticleSize(const XMFLOAT2& size);
    void SetParticleColor(const XMFLOAT4& color);
    void SetParticleVelocity(const XMFLOAT3& velocity, float randomRange = 0.0f);
    void SetParticleAcceleration(const XMFLOAT3& acceleration);
    void SetRotateSpeed(float speed);
    void SetRotation(float rotation, float randomrange);

    // ���� ��ȸ
    float GetSpawnRate() const { return m_spawnParams.spawnRate; }
    EmitterType GetEmitterType() const { return static_cast<EmitterType>(m_spawnParams.emitterType); }
    ParticleTemplateParams GetTemplate() const { return m_particleTemplate; }

    void SetAllowNewSpawn(bool allow) {
        m_allowNewSpawn = allow;
        m_spawnParamsDirty = true;
    }
    bool IsAllowNewSpawn() const { return m_allowNewSpawn; }

public:
    // ����ȭ
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
