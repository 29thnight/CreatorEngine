#include "EffectSerializer.h"
#include "EffectBase.h"

nlohmann::json EffectSerializer::SerializeEffect(const EffectBase& effect)
{
    nlohmann::json json;

    json["name"] = effect.GetName();
    json["position"] = SerializeVector3(effect.GetPosition());
    json["isPlaying"] = effect.IsPlaying();
    json["isPaused"] = effect.IsPaused();
    json["timeScale"] = effect.GetTimeScale();
    json["duration"] = effect.GetDuration();
    json["loop"] = effect.IsLooping();
    json["currentTime"] = effect.GetCurrentTime();

    json["particleSystems"] = nlohmann::json::array();
    for (const auto& ps : effect.GetAllParticleSystems())
    {
        if (ps)
        {
            json["particleSystems"].push_back(SerializeParticleSystem(*ps));
        }
    }

    return json;
}

std::unique_ptr<EffectBase> EffectSerializer::DeserializeEffect(const nlohmann::json& json)
{
    auto effect = std::make_unique<EffectBase>();

    if (json.contains("name"))
        effect->SetName(json["name"]);

    if (json.contains("position"))
        effect->SetPosition(DeserializeVector3(json["position"]));

    if (json.contains("timeScale"))
        effect->SetTimeScale(json["timeScale"]);

    if (json.contains("duration"))
        effect->SetDuration(json["duration"]);

    if (json.contains("loop"))
        effect->SetLoop(json["loop"]);

    if (json.contains("particleSystems"))
    {
        for (const auto& psJson : json["particleSystems"])
        {
            auto particleSystem = DeserializeParticleSystem(psJson);
            if (particleSystem)
            {
                effect->AddParticleSystem(particleSystem);
            }
        }
    }

    return effect;
}

nlohmann::json EffectSerializer::SerializeParticleSystem(ParticleSystem& system)
{
    nlohmann::json json;

    json["maxParticles"] = system.GetMaxParticles();
    json["particleDataType"] = static_cast<int>(system.GetParticleDataType());
    json["position"] = SerializeVector3(system.GetPosition());
    json["isRunning"] = system.IsRunning();

    json["modules"] = nlohmann::json::array();

    for (auto it = system.GetModuleList().begin(); it != system.GetModuleList().end(); ++it)
    {
        json["modules"].push_back(SerializeModule(*it));
    }

    json["renderModules"] = nlohmann::json::array();
    for (const auto& renderModule : system.GetRenderModules())
    {
        if (renderModule)
        {
            json["renderModules"].push_back(SerializeRenderModule(*renderModule));
        }
    }

    return json;
}

std::shared_ptr<ParticleSystem> EffectSerializer::DeserializeParticleSystem(const nlohmann::json& json)
{
    // JSON에서 실제 저장된 값을 읽어오기
    int maxParticles = json["maxParticles"];
    ParticleDataType dataType = static_cast<ParticleDataType>(json["particleDataType"]);

    auto system = std::make_shared<ParticleSystem>(maxParticles, dataType);

    if (json.contains("position"))
    {
        system->SetPosition(DeserializeVector3(json["position"]));
    }

    if (json.contains("modules"))
    {
        for (const auto& moduleJson : json["modules"])
        {
            auto module = DeserializeModule(moduleJson);
            if (module)
            {
                system->AddExistingModule(std::move(module));
            }
        }
    }

    if (json.contains("renderModules"))
    {
        for (const auto& renderModuleJson : json["renderModules"])
        {
            auto renderModule = DeserializeRenderModule(renderModuleJson);
            if (renderModule)
            {
                system->AddExistingRenderModule(std::move(renderModule));
            }
        }
    }

    return system;
}

bool EffectSerializer::SaveEffectsToFile(const std::string& filePath,
    const std::unordered_map<std::string, std::unique_ptr<EffectBase>>& effects)
{
    try
    {
        nlohmann::json json;
        json["effects"] = nlohmann::json::object();

        for (const auto& [name, effect] : effects)
        {
            if (effect)
            {
                json["effects"][name] = SerializeEffect(*effect);
            }
        }

        std::ofstream file(filePath);
        if (!file.is_open())
        {
            std::cerr << "Failed to open file for writing: " << filePath << std::endl;
            return false;
        }

        file << json.dump(4);
        file.close();

        std::cout << "Effects saved to: " << filePath << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error saving effects: " << e.what() << std::endl;
        return false;
    }
}

bool EffectSerializer::LoadEffectsFromFile(const std::string& filePath,
    std::unordered_map<std::string, std::unique_ptr<EffectBase>>& effects)
{
    try
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            std::cerr << "Failed to open file for reading: " << filePath << std::endl;
            return false;
        }

        nlohmann::json json;
        file >> json;
        file.close();

        effects.clear();

        if (json.contains("effects"))
        {
            for (const auto& [name, effectJson] : json["effects"].items())
            {
                auto effect = DeserializeEffect(effectJson);
                if (effect)
                {
                    effects[name] = std::move(effect);
                }
            }
        }

        std::cout << "Effects loaded from: " << filePath << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error loading effects: " << e.what() << std::endl;
        return false;
    }
}

nlohmann::json EffectSerializer::SerializeModule(const ParticleModule& module)
{
    nlohmann::json json;

    const ISerializable* serializable = dynamic_cast<const ISerializable*>(&module);
    if (serializable)
    {
        json["type"] = serializable->GetModuleType();
        json["data"] = serializable->SerializeData();

        json["stage"] = static_cast<int>(module.GetStage());
        json["useEasing"] = module.IsEasingEnabled();
    }
    else
    {
        json["type"] = "unknown";
        json["data"] = nlohmann::json::object();
    }

    return json;
}

std::unique_ptr<ParticleModule> EffectSerializer::DeserializeModule(const nlohmann::json& json)
{
    if (!json.contains("type"))
    {
        std::cerr << "Module JSON missing 'type' field" << std::endl;
        return nullptr;
    }

    std::string moduleType = json["type"];
    std::unique_ptr<ParticleModule> module;

    if (moduleType == "SpawnModuleCS")
    {
        module = std::make_unique<SpawnModuleCS>();
    }
    else if (moduleType == "MeshSpawnModuleCS")
    {
        module = std::make_unique<MeshSpawnModuleCS>();
    }
    else
    {
        std::cerr << "Unknown module type: " << moduleType << std::endl;
        return nullptr;
    }

    if (module)
    {
        if (json.contains("stage"))
        {
            module->SetStage(static_cast<ModuleStage>(json["stage"]));
        }

        if (json.contains("useEasing"))
        {
            module->EnableEasing(json["useEasing"]);
        }

        ISerializable* serializable = dynamic_cast<ISerializable*>(module.get());
        if (serializable && json.contains("data"))
        {
            serializable->DeserializeData(json["data"]);
        }
    }

    return module;
}

nlohmann::json EffectSerializer::SerializeRenderModule(const RenderModules& renderModule)
{
    nlohmann::json json;

    const ISerializable* serializable = dynamic_cast<const ISerializable*>(&renderModule);
    if (serializable)
    {
        json["type"] = serializable->GetModuleType();
        json["data"] = serializable->SerializeData();
    }
    else
    {
        json["type"] = "unknown";
        json["data"] = nlohmann::json::object();
    }

    return json;
}

std::unique_ptr<RenderModules> EffectSerializer::DeserializeRenderModule(const nlohmann::json& json)
{
    if (!json.contains("type"))
    {
        std::cerr << "RenderModule JSON missing 'type' field" << std::endl;
        return nullptr;
    }

    std::string moduleType = json["type"];
    std::unique_ptr<RenderModules> renderModule;

    if (moduleType == "BillboardModuleGPU")
    {
        renderModule = std::make_unique<BillboardModuleGPU>();
    }
    else if (moduleType == "MeshModuleGPU")
    {
        renderModule = std::make_unique<MeshModuleGPU>();
    }
    else
    {
        std::cerr << "Unknown render module type: " << moduleType << std::endl;
        return nullptr;
    }

    if (renderModule)
    {
        ISerializable* serializable = dynamic_cast<ISerializable*>(renderModule.get());
        if (serializable && json.contains("data"))
        {
            serializable->DeserializeData(json["data"]);
        }
    }

    return renderModule;
}

nlohmann::json EffectSerializer::SerializeVector3(const Mathf::Vector3& vec)
{
    return nlohmann::json{
        {"x", vec.x},
        {"y", vec.y},
        {"z", vec.z}
    };
}

Mathf::Vector3 EffectSerializer::DeserializeVector3(const nlohmann::json& json)
{
    return Mathf::Vector3(
        json.value("x", 0.0f),
        json.value("y", 0.0f),
        json.value("z", 0.0f)
    );
}

nlohmann::json EffectSerializer::SerializeVector4(const Mathf::Vector4& vec)
{
    return nlohmann::json{
        {"x", vec.x},
        {"y", vec.y},
        {"z", vec.z},
        {"w", vec.w}
    };
}

Mathf::Vector4 EffectSerializer::DeserializeVector4(const nlohmann::json& json)
{
    return Mathf::Vector4(
        json.value("x", 0.0f),
        json.value("y", 0.0f),
        json.value("z", 0.0f),
        json.value("w", 1.0f)
    );
}

nlohmann::json EffectSerializer::SerializeXMFLOAT3(const XMFLOAT3& vec)
{
    return nlohmann::json{
        {"x", vec.x},
        {"y", vec.y},
        {"z", vec.z}
    };
}

XMFLOAT3 EffectSerializer::DeserializeXMFLOAT3(const nlohmann::json& json)
{
    return XMFLOAT3(
        json.value("x", 0.0f),
        json.value("y", 0.0f),
        json.value("z", 0.0f)
    );
}

nlohmann::json EffectSerializer::SerializeXMFLOAT4(const XMFLOAT4& vec)
{
    return nlohmann::json{
        {"x", vec.x},
        {"y", vec.y},
        {"z", vec.z},
        {"w", vec.w}
    };
}

XMFLOAT4 EffectSerializer::DeserializeXMFLOAT4(const nlohmann::json& json)
{
    return XMFLOAT4(
        json.value("x", 0.0f),
        json.value("y", 0.0f),
        json.value("z", 0.0f),
        json.value("w", 1.0f)
    );
}