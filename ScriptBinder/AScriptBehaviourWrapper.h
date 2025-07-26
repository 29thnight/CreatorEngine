#pragma once
#include <string>
#include <angelscript.h> // asIScriptObject를 사용하기 위함

class AScriptBehaviourWrapper
{
public:
    // 이벤트로 스크립트에서 오버라이드 할 수 있는 부분
    void OnStart()
    {
        if (!m_isDead || m_isDead->Get()) return;

        asIScriptEngine* engine = m_obj->GetEngine();
        asIScriptContext* ctx = engine->RequestContext();
        asIScriptFunction* func = m_obj->GetObjectType()->GetMethodByDecl("void OnStart()");
        if (func)
        {
            ctx->Prepare(func);
            ctx->SetObject(m_obj);
            ctx->Execute();
        }
        engine->ReturnContext(ctx);
    }

    void OnUpdate(float deltaTime)
    {
        if (!m_isDead || m_isDead->Get()) return;

        asIScriptEngine* engine = m_obj->GetEngine();
        asIScriptContext* ctx = engine->RequestContext();
        asIScriptFunction* func = m_obj->GetObjectType()->GetMethodByDecl("void OnUpdate(float)");
        if (func)
        {
            ctx->Prepare(func);
            ctx->SetObject(m_obj);
            ctx->SetArgFloat(0, deltaTime);
            ctx->Execute();
        }
        engine->ReturnContext(ctx);
    }

    // ====== 레퍼런스 카운트 ======
    void AddRef()
    {
        m_refCount++;
        if (!m_isDead->Get())
            m_obj->AddRef();
    }

    void Release()
    {
        if (!m_isDead->Get())
            m_obj->Release();

        if (--m_refCount == 0)
            delete this;
    }

    // ====== GC behaviours ======
    void SetGCFlag() { m_gcFlag = true; }
    bool GetGCFlag() { return m_gcFlag; }
    int  GetRefCount() { return m_refCount; }

    void EnumReferences(asIScriptEngine* engine)
    {
        if (m_obj) engine->GCEnumCallback(m_obj);
        if (m_isDead) engine->GCEnumCallback(m_isDead);
    }

    void ReleaseAllReferences(asIScriptEngine* /*engine*/)
    {
        if (m_obj)
        {
            m_obj->Release();
            m_obj = nullptr;
        }
        if (m_isDead)
        {
            m_isDead->Release();
            m_isDead = nullptr;
        }
    }

    // ====== Factory ======
    static AScriptBehaviourWrapper* Factory()
    {
        asIScriptContext* ctx = asGetActiveContext();
        asIScriptFunction* func = ctx->GetFunction(0);

        // 오직 스크립트 쪽 AScriptBehaviour 파생 클래스에서만 호출되도록 제한
        if (func->GetObjectType() == nullptr ||
            std::string(func->GetObjectType()->GetName()) != "AScriptBehaviour")
        {
            ctx->SetException("Invalid attempt to instantiate");
            return nullptr;
        }

        asIScriptObject* obj = reinterpret_cast<asIScriptObject*>(ctx->GetThisPointer(0));
        return new AScriptBehaviourWrapper(obj);
    }

protected:
    AScriptBehaviourWrapper(asIScriptObject* obj)
        : m_refCount(1), m_gcFlag(false), m_obj(obj)
    {
        // weak ref flag
        m_isDead = obj->GetWeakRefFlag();
        m_isDead->AddRef();
    }

    ~AScriptBehaviourWrapper()
    {
        if (m_isDead)
        {
            m_isDead->Release();
            m_isDead = nullptr;
        }
    }

private:
    int m_refCount;
    bool m_gcFlag;
    asIScriptObject* m_obj = nullptr;
    asILockableSharedBool* m_isDead = nullptr;
};

