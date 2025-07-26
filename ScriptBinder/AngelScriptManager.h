#pragma once
#include "Core.Minimal.h" // 필요한 경우 포함
#include <angelscript.h>
#include "AScriptBehaviourWrapper.h" // AScriptBehaviour 정의를 위해 포함

// AngelScript 메시지 콜백 함수
void MessageCallback(const asSMessageInfo* msg, void* param);

class AngelScriptManager : public Singleton<AngelScriptManager>
{
private:
    friend Singleton;
    AngelScriptManager() = default;
    ~AngelScriptManager();

public:
    void Initialize();
    asIScriptEngine* GetEngine() const { return m_asEngine; }

private:
    asIScriptEngine* m_asEngine = nullptr;
};

static auto& AngelScriptManagers = AngelScriptManager::GetInstance();
