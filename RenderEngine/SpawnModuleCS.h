#pragma once
#include "ParticleModule.h"
#include "EffectSerializer.h"
#include "ISerializable.h"

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
    virtual nlohmann::json SerializeData() const override
    {
        nlohmann::json json;

        // SpawnParams ����ȭ
        json["spawnParams"] = {
            {"spawnRate", m_spawnParams.spawnRate},
            {"emitterType", m_spawnParams.emitterType},
            {"emitterSize", EffectSerializer::SerializeXMFLOAT3(m_spawnParams.emitterSize)},
            {"emitterRadius", m_spawnParams.emitterRadius},
            {"emitterPosition", EffectSerializer::SerializeXMFLOAT3(m_spawnParams.emitterPosition)}
        };

        // ParticleTemplateParams ����ȭ
        json["particleTemplate"] = {
            {"lifeTime", m_particleTemplate.lifeTime},
            {"rotateSpeed", m_particleTemplate.rotateSpeed},
            {"size", {
                {"x", m_particleTemplate.size.x},
                {"y", m_particleTemplate.size.y}
            }},
            {"color", EffectSerializer::SerializeXMFLOAT4(m_particleTemplate.color)},
            {"velocity", EffectSerializer::SerializeXMFLOAT3(m_particleTemplate.velocity)},
            {"acceleration", EffectSerializer::SerializeXMFLOAT3(m_particleTemplate.acceleration)},
            {"minVerticalVelocity", m_particleTemplate.minVerticalVelocity},
            {"maxVerticalVelocity", m_particleTemplate.maxVerticalVelocity},
            {"horizontalVelocityRange", m_particleTemplate.horizontalVelocityRange}
        };

        // ���� ����
        json["state"] = {
            {"isInitialized", m_isInitialized},
            {"particleCapacity", m_particleCapacity}
        };

        return json;
    }

    virtual void DeserializeData(const nlohmann::json& json) override
    {
        // SpawnParams ����
        if (json.contains("spawnParams"))
        {
            const auto& spawnJson = json["spawnParams"];

            if (spawnJson.contains("spawnRate"))
                m_spawnParams.spawnRate = spawnJson["spawnRate"];

            if (spawnJson.contains("emitterType"))
                m_spawnParams.emitterType = spawnJson["emitterType"];

            if (spawnJson.contains("emitterSize"))
                m_spawnParams.emitterSize = EffectSerializer::DeserializeXMFLOAT3(spawnJson["emitterSize"]);

            if (spawnJson.contains("emitterRadius"))
                m_spawnParams.emitterRadius = spawnJson["emitterRadius"];

            if (spawnJson.contains("emitterPosition"))
                m_spawnParams.emitterPosition = EffectSerializer::DeserializeXMFLOAT3(spawnJson["emitterPosition"]);
        }

        // ParticleTemplateParams ����
        if (json.contains("particleTemplate"))
        {
            const auto& templateJson = json["particleTemplate"];

            if (templateJson.contains("lifeTime"))
                m_particleTemplate.lifeTime = templateJson["lifeTime"];

            if (templateJson.contains("rotateSpeed"))
                m_particleTemplate.rotateSpeed = templateJson["rotateSpeed"];

            if (templateJson.contains("size"))
            {
                const auto& sizeJson = templateJson["size"];
                m_particleTemplate.size.x = sizeJson.value("x", 1.0f);
                m_particleTemplate.size.y = sizeJson.value("y", 1.0f);
            }

            if (templateJson.contains("color"))
                m_particleTemplate.color = EffectSerializer::DeserializeXMFLOAT4(templateJson["color"]);

            if (templateJson.contains("velocity"))
                m_particleTemplate.velocity = EffectSerializer::DeserializeXMFLOAT3(templateJson["velocity"]);

            if (templateJson.contains("acceleration"))
                m_particleTemplate.acceleration = EffectSerializer::DeserializeXMFLOAT3(templateJson["acceleration"]);

            if (templateJson.contains("minVerticalVelocity"))
                m_particleTemplate.minVerticalVelocity = templateJson["minVerticalVelocity"];

            if (templateJson.contains("maxVerticalVelocity"))
                m_particleTemplate.maxVerticalVelocity = templateJson["maxVerticalVelocity"];

            if (templateJson.contains("horizontalVelocityRange"))
                m_particleTemplate.horizontalVelocityRange = templateJson["horizontalVelocityRange"];
        }

        // ���� ���� ����
        if (json.contains("state"))
        {
            const auto& stateJson = json["state"];

            if (stateJson.contains("particleCapacity"))
                m_particleCapacity = stateJson["particleCapacity"];
        }

        // ��������� �����ϱ� ���� ��Ƽ �÷��� ����
        m_spawnParamsDirty = true;
        m_templateDirty = true;
    }

    virtual std::string GetModuleType() const override
    {
        return "SpawnModuleCS";
    }

private:
    bool InitializeComputeShader();
    bool CreateConstantBuffers();
    bool CreateUtilityBuffers();
    void UpdateConstantBuffers(float deltaTime);
    void ReleaseResources();
};
