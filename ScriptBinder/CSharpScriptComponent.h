#pragma once
#include "Component.h"
#include <mono/metadata/object.h>
#include "MonoManager.h"
#include "IOnDisable.h"

class CSharpScriptComponent : public Component
{
public:
    //TODO : register refl
    CSharpScriptComponent()
    {
        m_name = "CSharpScriptComponent";
        m_typeID = TypeTrait::GUIDCreator::GetTypeID<CSharpScriptComponent>();
    }
    virtual ~CSharpScriptComponent()
    {
        MonoManagers->UnregisterBehavior(m_handle);
    }

    MonoObject* GetMonoInstance() const { return m_monoInstance; }

    void ManagedDataInitialize(MonoObject* instance, const std::string& scriptName, FileGuid guid)
    {
        m_handle = MonoManagers->RegisterBehaviorInstance(instance);
        m_scriptName = scriptName;
        m_scriptFileGuid = guid;
    }

private:
    std::string m_scriptName{};
    FileGuid m_scriptFileGuid{};
    MonoBehaviorHandle m_handle{};
    MonoObject* m_monoInstance{};
};
