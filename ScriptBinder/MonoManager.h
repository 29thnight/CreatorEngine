#pragma once
#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/metadata.h>
#include "MonoBehaviorRecord.h"
#include "Core.Minimal.h"

class MonoManager : public Singleton<MonoManager>
{
private:
    friend class Singleton;
    MonoManager() = default;
    ~MonoManager() = default;

public:
    bool Initialize(const std::string& monoLibDir,
                    const std::string& monoEtcDir);
    bool LoadAssembly(const std::string& assemblyPath);
    void RegisterInternalCalls();

    void ScanMonoBehaviors();

    MonoBehaviorHandle RegisterBehaviorInstance(MonoObject* behaviorObj);
    void UnregisterBehavior(MonoBehaviorHandle h);

    MonoDomain* GetDomain() const { return m_domain; }
    MonoImage* GetImage()  const { return m_image; }

private:
    MonoDomain*     m_domain{ nullptr };
    MonoAssembly*   m_assembly{ nullptr };
    MonoImage*      m_image{ nullptr };

    MonoBehaviorHandle m_nextBehaviorHandle = 1;

    std::unordered_map<MonoBehaviorHandle, MonoBehaviorRecord>  m_behaviors;
    std::unordered_map<MonoClass*, MonoBehaviorInfo>            m_behaviorTypeMap;
};

inline static auto& MonoManagers = MonoManager::GetInstance();
