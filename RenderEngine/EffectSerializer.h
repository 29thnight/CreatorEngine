#pragma once
#include <nlohmann/json.hpp>
#include "EffectBase.h"

// JSON ����ȭ�� ����ϴ� Ŭ����
class EffectSerializer
{
public:
    // EffectBase�� JSON���� ����
    static nlohmann::json SerializeEffect(const EffectBase& effect);

    // JSON���� EffectBase�� ����
    static std::unique_ptr<EffectBase> DeserializeEffect(const nlohmann::json& json);

    // ParticleSystem�� JSON���� ����
    static nlohmann::json SerializeParticleSystem(ParticleSystem& system);

    // JSON���� ParticleSystem���� ����
    static std::shared_ptr<ParticleSystem> DeserializeParticleSystem(const nlohmann::json& json);

    // ��ü EffectManager�� effects�� JSON ���Ϸ� ����
    static bool SaveEffectsToFile(const std::string& filePath,
        const std::unordered_map<std::string, std::unique_ptr<EffectBase>>& effects);

    // JSON ���Ͽ��� effects�� �ε�
    static bool LoadEffectsFromFile(const std::string& filePath,
        std::unordered_map<std::string, std::unique_ptr<EffectBase>>& effects);

    // ���� Ÿ�� ����ȭ ����
    static nlohmann::json SerializeVector3(const Mathf::Vector3& vec);
    static Mathf::Vector3 DeserializeVector3(const nlohmann::json& json);

    static nlohmann::json SerializeVector4(const Mathf::Vector4& vec);
    static Mathf::Vector4 DeserializeVector4(const nlohmann::json& json);

    static nlohmann::json SerializeXMFLOAT3(const XMFLOAT3& vec);
    static XMFLOAT3 DeserializeXMFLOAT3(const nlohmann::json& json);

    static nlohmann::json SerializeXMFLOAT4(const XMFLOAT4& vec);
    static XMFLOAT4 DeserializeXMFLOAT4(const nlohmann::json& json);

private:
    // ParticleModule ����ȭ
    static nlohmann::json SerializeModule(const ParticleModule& module);
    static std::unique_ptr<ParticleModule> DeserializeModule(const nlohmann::json& json);

    // RenderModules ����ȭ (���� �Լ�)
    static nlohmann::json SerializeRenderModule(const RenderModules& renderModule);
    static std::unique_ptr<RenderModules> DeserializeRenderModule(const nlohmann::json& json);
};

// �� ��� Ŭ�������� �����ؾ� �� �������̽�
class ISerializable
{
public:
    virtual ~ISerializable() = default;

    // �� ����� �ڽŸ��� �����͸� JSON���� ����
    virtual nlohmann::json SerializeData() const = 0;

    // JSON���� �ڽ��� �����͸� ����
    virtual void DeserializeData(const nlohmann::json& json) = 0;

    // ��� Ÿ���� ���ڿ��� ��ȯ (Ŭ������� �����ϰ�)
    virtual std::string GetModuleType() const = 0;
};