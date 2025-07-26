#include "AngelScriptManager.h"
#include "LogSystem.h" // Debug->LogError를 사용하기 위해 포함
#include "StringFactory.h"

// AngelScript 메시지 콜백 함수 구현
void MessageCallback(const asSMessageInfo *msg, void *param)
{
    const char *type = "ERR ";
    if (msg->type == asMSGTYPE_WARNING)
    {
        type = "WARN";
    }
    else if (msg->type == asMSGTYPE_INFORMATION)
    {
        type = "INFO";
    }

    Debug->LogDebug(std::string(msg->section) + " (" + std::to_string(msg->row) + ", " + std::to_string(msg->col) + ") : " + type + " : " + msg->message);
}

// AngelScript에서 호출될 Print 함수
void Print(const std::string& msg)
{
    Debug->LogDebug("AngelScript: " + msg);
}

AngelScriptManager::~AngelScriptManager()
{
    if (m_asEngine)
    {
        m_asEngine->Release();
        m_asEngine = nullptr;
    }
}

void AngelScriptManager::Initialize()
{
    m_asEngine = asCreateScriptEngine();
    if (m_asEngine == nullptr)
    {
        Debug->LogError("Failed to create AngelScript engine.");
        return;
    }

    m_asEngine->SetMessageCallback(asFUNCTION(MessageCallback), 0, asCALL_CDECL);

    // std::string 타입 등록
    static StringFactory* stringFactory = new StringFactory();
    StringFactory::RegisterStdString(m_asEngine, stringFactory);

    int r;
    // GC 지원
    r = m_asEngine->RegisterObjectType("AScriptBehaviourWrapper", 0, asOBJ_REF | asOBJ_GC); assert(r >= 0);

    // Factory
    r = m_asEngine->RegisterObjectBehaviour("AScriptBehaviourWrapper", asBEHAVE_FACTORY,
        "AScriptBehaviourWrapper@ f()", asFUNCTION(AScriptBehaviourWrapper::Factory), asCALL_CDECL); assert(r >= 0);

    // 레퍼런스 카운트
    r = m_asEngine->RegisterObjectBehaviour("AScriptBehaviourWrapper", asBEHAVE_ADDREF,
        "void f()", asMETHOD(AScriptBehaviourWrapper, AddRef), asCALL_THISCALL); assert(r >= 0);

    r = m_asEngine->RegisterObjectBehaviour("AScriptBehaviourWrapper", asBEHAVE_RELEASE,
        "void f()", asMETHOD(AScriptBehaviourWrapper, Release), asCALL_THISCALL); assert(r >= 0);

    // GC behaviours
    r = m_asEngine->RegisterObjectBehaviour("AScriptBehaviourWrapper", asBEHAVE_SETGCFLAG,
        "void f()", asMETHOD(AScriptBehaviourWrapper, SetGCFlag), asCALL_THISCALL); assert(r >= 0);

    r = m_asEngine->RegisterObjectBehaviour("AScriptBehaviourWrapper", asBEHAVE_GETGCFLAG,
        "bool f()", asMETHOD(AScriptBehaviourWrapper, GetGCFlag), asCALL_THISCALL); assert(r >= 0);

    r = m_asEngine->RegisterObjectBehaviour("AScriptBehaviourWrapper", asBEHAVE_GETREFCOUNT,
        "int f()", asMETHOD(AScriptBehaviourWrapper, GetRefCount), asCALL_THISCALL); assert(r >= 0);

    r = m_asEngine->RegisterObjectBehaviour("AScriptBehaviourWrapper", asBEHAVE_ENUMREFS,
        "void f(int&in)", asMETHOD(AScriptBehaviourWrapper, EnumReferences), asCALL_THISCALL); assert(r >= 0);

    r = m_asEngine->RegisterObjectBehaviour("AScriptBehaviourWrapper", asBEHAVE_RELEASEREFS,
        "void f(int&in)", asMETHOD(AScriptBehaviourWrapper, ReleaseAllReferences), asCALL_THISCALL); assert(r >= 0);

    // 이벤트 메서드
    r = m_asEngine->RegisterObjectMethod("AScriptBehaviourWrapper", "void OnStart()",
        asMETHOD(AScriptBehaviourWrapper, OnStart), asCALL_THISCALL); assert(r >= 0);

    r = m_asEngine->RegisterObjectMethod("AScriptBehaviourWrapper", "void OnUpdate(float)",
        asMETHOD(AScriptBehaviourWrapper, OnUpdate), asCALL_THISCALL); assert(r >= 0);

	using namespace Mathf;

    // Vector2는 8바이트(float2)로 POD 타입
    r = m_asEngine->RegisterObjectType("Vector2", sizeof(Mathf::Vector2),
        asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_CDAK);
    assert(r >= 0);

    // 기본 생성자
    r = m_asEngine->RegisterObjectBehaviour("Vector2", asBEHAVE_CONSTRUCT,
        "void f()",
        asFUNCTIONPR(([](void* mem) { new(mem) Mathf::Vector2(); }), (void*), void),
        asCALL_CDECL_OBJLAST);
    assert(r >= 0);

    // x,y를 받는 생성자
    r = m_asEngine->RegisterObjectBehaviour("Vector2", asBEHAVE_CONSTRUCT,
        "void f(float x, float y)",
        asFUNCTIONPR(([](float x, float y, void* mem) { new(mem) Mathf::Vector2(x, y); }), (float, float, void*), void),
        asCALL_CDECL_OBJLAST);
    assert(r >= 0);

    r = m_asEngine->RegisterObjectProperty("Vector2", "float x", asOFFSET(Mathf::Vector2, x)); assert(r >= 0);
    r = m_asEngine->RegisterObjectProperty("Vector2", "float y", asOFFSET(Mathf::Vector2, y)); assert(r >= 0);

    // 덧셈
    r = m_asEngine->RegisterObjectMethod("Vector2",
        "Vector2 opAdd(const Vector2 &in) const",
        asFUNCTIONPR(([](const Vector2* self, const Vector2& v) { return *self + v; }),
            (const Vector2*, const Vector2&), Vector2),
        asCALL_CDECL_OBJFIRST);
    assert(r >= 0);

    // 뺄셈
    r = m_asEngine->RegisterObjectMethod("Vector2",
        "Vector2 opSub(const Vector2 &in) const",
        asFUNCTIONPR(([](const Vector2* self, const Vector2& v) { return *self - v; }),
            (const Vector2*, const Vector2&), Vector2),
        asCALL_CDECL_OBJFIRST);
    assert(r >= 0);

    // 스칼라 곱
    r = m_asEngine->RegisterObjectMethod("Vector2",
        "Vector2 opMul(float s) const",
        asFUNCTIONPR(([](const Vector2* self, float s) { return *self * s; }),
            (const Vector2*, float), Vector2),
        asCALL_CDECL_OBJFIRST);
    assert(r >= 0);

    // 스칼라 나눗셈
    r = m_asEngine->RegisterObjectMethod("Vector2",
        "Vector2 opDiv(float s) const",
        asFUNCTIONPR(([](const Vector2* self, float s) { return *self / s; }),
            (const Vector2*, float), Vector2),
        asCALL_CDECL_OBJFIRST);
    assert(r >= 0);

    r = m_asEngine->RegisterObjectMethod("Vector2",
        "float Length() const",
        asMETHOD(Vector2, Length),
        asCALL_THISCALL);
    assert(r >= 0);

     r = m_asEngine->RegisterObjectMethod("Vector2",
        "float LengthSquared() const",
        asMETHOD(Vector2, LengthSquared),
		   asCALL_THISCALL);
	 assert(r >= 0);

     r = m_asEngine->RegisterObjectMethod("Vector2",
         "void Normalize()",
         asMETHOD(Vector2, Normalize),
         asCALL_THISCALL);
     assert(r >= 0);




    // 전역 함수 Print 등록
    r = m_asEngine->RegisterGlobalFunction("void Print(const string &in)", asFUNCTION(Print), asCALL_CDECL);
    if (r < 0)
    {
        Debug->LogError("Failed to register global function Print.");
        return;
    }
}
