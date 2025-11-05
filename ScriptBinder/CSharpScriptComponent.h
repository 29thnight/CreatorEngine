#pragma once
#include "Component.h"

#include <string>
#include <utility>
#define UNUSE_MONO_LIB
#ifndef UNUSE_MONO_LIB

#include <cstdint>
#include "MonoBehaviorRecord.h"
#include "MonoManager.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

class CSharpScriptComponent : public Component
{
public:
    ReflectionFieldInheritance(CSharpScriptComponent, Component) \
    { \
        PropertyField \
        ({ \
            meta_property(m_scriptName) \
			meta_property(m_scriptFileGuid) \
        }); \
        FieldEnd(CSharpScriptComponent, PropertyOnlyInheritance) \
    };

    CSharpScriptComponent()
    {
        m_name = "CSharpScriptComponent";
        m_typeID = type_guid(CSharpScriptComponent);
    }

    ~CSharpScriptComponent() override
    {
        ReleaseManagedInstance();
    }

    void ManagedDataInitialize(MonoObject* instance, std::string scriptName, FileGuid guid)
    {
        ReleaseManagedInstance();

        m_scriptName = std::move(scriptName);
        m_scriptFileGuid = guid;
        m_monoInstance = instance;

        if (m_monoInstance != nullptr)
        {
            m_handle = RegisterManagedInstance(m_monoInstance);
            if (auto* monoManager = MonoManager::GetInstance(); monoManager != nullptr)
            {
                monoManager->BindScriptEvents(this);
            }
        }
    }

    void ResetManagedInstance()
    {
        ReleaseManagedInstance();
        m_scriptName.clear();
        m_scriptFileGuid = {};
    }

    [[nodiscard]] MonoObject* GetMonoInstance() const noexcept { return m_monoInstance; }
    [[nodiscard]] const std::string& GetScriptName() const noexcept { return m_scriptName; }
    [[nodiscard]] FileGuid GetScriptGuid() const noexcept { return m_scriptFileGuid; }
    [[nodiscard]] MonoBehaviorHandle GetBehaviorHandle() const noexcept { return m_handle; }
    [[nodiscard]] bool HasManagedInstance() const noexcept { return m_monoInstance != nullptr; }

    static MonoObject* ResolveMonoInstance(MonoBehaviorHandle handle)
    {
        if (handle == 0)
        {
            return nullptr;
        }

        auto& registry = GetMonoBehaviorRegistry();
        std::scoped_lock lock{ registry.mutex };
        if (auto it = registry.instances.find(handle); it != registry.instances.end())
        {
            return it->second;
        }
        return nullptr;
    }

private:
    struct MonoBehaviorRegistry
    {
        std::mutex mutex{};
        std::atomic<MonoBehaviorHandle> nextHandle{ 1 };
        std::unordered_map<MonoBehaviorHandle, MonoObject*> instances{};
    };

    static MonoBehaviorRegistry& GetMonoBehaviorRegistry()
    {
        static MonoBehaviorRegistry registry{};
        return registry;
    }

    void ReleaseManagedInstance() noexcept
    {
        if (m_handle != 0)
        {
            if (auto* monoManager = MonoManager::GetInstance(); monoManager != nullptr)
            {
                monoManager->UnbindScriptEvents(this);
            }

            UnregisterManagedInstance(m_handle);
            m_handle = 0;
        }
        m_monoInstance = nullptr;
    }

    static MonoBehaviorHandle RegisterManagedInstance(MonoObject* instance)
    {
        auto& registry = GetMonoBehaviorRegistry();
        std::scoped_lock lock{ registry.mutex };

        const MonoBehaviorHandle handle = registry.nextHandle.fetch_add(1, std::memory_order_relaxed);
        registry.instances.insert_or_assign(handle, instance);
        return handle;
    }

    static void UnregisterManagedInstance(MonoBehaviorHandle handle)
    {
        if (handle == 0)
        {
            return;
        }

        auto& registry = GetMonoBehaviorRegistry();
        std::scoped_lock lock{ registry.mutex };
        registry.instances.erase(handle);
    }

private:
    [[Property]]
    std::string m_scriptName{};
    [[Property]]
    FileGuid m_scriptFileGuid{};
    MonoBehaviorHandle m_handle{};
    MonoObject* m_monoInstance{ nullptr };
};

#endif // UNUSE_MONO_LIB