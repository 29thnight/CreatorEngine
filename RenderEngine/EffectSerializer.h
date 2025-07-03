#pragma once
#include <nlohmann/json.hpp>
#include "Core.Minimal.h"

class EffectBase;
class ParticleSystem;
class RenderModules;
class ParticleModule;

// JSON 직렬화를 담당하는 클래스
class EffectSerializer
{
public:
    // EffectBase를 JSON으로 저장
    static nlohmann::json SerializeEffect(const EffectBase& effect);

    // JSON에서 EffectBase로 복원
    static std::unique_ptr<EffectBase> DeserializeEffect(const nlohmann::json& json);

    // ParticleSystem을 JSON으로 저장
    static nlohmann::json SerializeParticleSystem(ParticleSystem& system);

    // JSON에서 ParticleSystem으로 복원
    static std::shared_ptr<ParticleSystem> DeserializeParticleSystem(const nlohmann::json& json);

    // 전체 EffectManager의 effects를 JSON 파일로 저장
    static bool SaveEffectsToFile(const std::string& filePath,
        const std::unordered_map<std::string, std::unique_ptr<EffectBase>>& effects);

    // JSON 파일에서 effects를 로드
    static bool LoadEffectsFromFile(const std::string& filePath,
        std::unordered_map<std::string, std::unique_ptr<EffectBase>>& effects);

    // 수학 타입 직렬화 헬퍼
    static nlohmann::json SerializeVector3(const Mathf::Vector3& vec);
    static Mathf::Vector3 DeserializeVector3(const nlohmann::json& json);

    static nlohmann::json SerializeVector4(const Mathf::Vector4& vec);
    static Mathf::Vector4 DeserializeVector4(const nlohmann::json& json);

    static nlohmann::json SerializeXMFLOAT3(const XMFLOAT3& vec);
    static XMFLOAT3 DeserializeXMFLOAT3(const nlohmann::json& json);

    static nlohmann::json SerializeXMFLOAT4(const XMFLOAT4& vec);
    static XMFLOAT4 DeserializeXMFLOAT4(const nlohmann::json& json);

private:
    // ParticleModule 직렬화
    static nlohmann::json SerializeModule(const ParticleModule& module);
    static std::unique_ptr<ParticleModule> DeserializeModule(const nlohmann::json& json);

    // RenderModules 직렬화 (별도 함수)
    static nlohmann::json SerializeRenderModule(const RenderModules& renderModule);
    static std::unique_ptr<RenderModules> DeserializeRenderModule(const nlohmann::json& json);
};

