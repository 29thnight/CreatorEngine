#include "MonoManager.h"
#ifndef UNUSE_MONO_LIB
#include <cassert>
#include <sstream>
#include <iostream>
#include <mono/metadata/mono-gc.h>
#include <mono/metadata/threads.h>

// 네가 기존에 작성한 Object 바인딩 등록 함수
void Register_Object_ICalls();

bool MonoManager::Initialize(const char* domainName,
    const char* monoLibDir,
    const char* monoEtcDir,
    bool enableDebug)
{
    // Mono 엔진 경로 설정
    mono_set_dirs(monoLibDir, monoEtcDir);
    mono_config_parse(nullptr);

    if (enableDebug)
    {
        // mdb/pdb 심볼 로딩 활성화
        mono_debug_init(MONO_DEBUG_FORMAT_MONO);
        mono_jit_set_aot_mode(MONO_AOT_MODE_INTERP); // 필요시 인터프리터 병행
        mono_debug_domain_create(mono_get_root_domain());
    }

    // 루트 도메인 생성
    m_rootDomain = mono_jit_init_version(domainName, "v4.0.30319");
    if (!m_rootDomain)
    {
        std::cerr << "[Mono] Failed to create root domain\n";
        return false;
    }

    // 앱 도메인 생성 (스크립트용 도메인 분리)
    if (!CreateAppDomain(domainName))
        return false;

    // 내부 호출 등록 (필요한 바인딩 모두 여기에서)
    RegisterInternalCalls();
    return true;
}

void MonoManager::Shutdown()
{
    UnloadAllAssemblies();
    DestroyAppDomain();

    if (m_rootDomain)
    {
        mono_jit_cleanup(m_rootDomain);
        m_rootDomain = nullptr;
    }
}

bool MonoManager::CreateAppDomain(const char* domainName)
{
    assert(m_rootDomain);
    mono_domain_set(m_rootDomain, /* force */ false);

    m_appDomain = mono_domain_create_appdomain(const_cast<char*>(domainName), nullptr);
    if (!m_appDomain)
    {
        std::cerr << "[Mono] Failed to create app domain\n";
        return false;
    }

    mono_domain_set(m_appDomain, /* force */ false);
    return true;
}

void MonoManager::DestroyAppDomain()
{
    if (m_appDomain)
    {
        // appdomain unload는 루트 도메인 컨텍스트에서 실행해야 함
        mono_domain_set(m_rootDomain, /* force */ false);
        mono_domain_unload(m_appDomain);
        m_appDomain = nullptr;
        mono_gc_collect(mono_gc_max_generation());

    }
}

std::optional<MonoManager::AssemblyPack>
MonoManager::LoadAssembly(const std::string& name, const std::string& path)
{
    assert(m_appDomain);
    mono_domain_set(m_appDomain, false);

    MonoAssembly* assembly = mono_domain_assembly_open(m_appDomain, path.c_str());
    if (!assembly)
    {
        std::cerr << "[Mono] Failed to load assembly: " << path << "\n";
        return std::nullopt;
    }
    MonoImage* image = mono_assembly_get_image(assembly);
    if (!image)
    {
        std::cerr << "[Mono] Failed to get image: " << path << "\n";
        return std::nullopt;
    }

    AssemblyPack pack;
    pack.assembly = assembly;
    pack.image = image;
    pack.name = name;
    pack.path = path;

    m_assemblies[name] = pack;
    return pack;
}

void MonoManager::UnloadAllAssemblies()
{
    // Mono API로 개별 어셈블리 unload는 안 됨. AppDomain을 통째로 언로드해야 함.
    m_assemblies.clear();
}

bool MonoManager::ReloadAll(const std::vector<std::pair<std::string, std::string>>& assemblies)
{
    // 1) 기존 AppDomain 파괴
    UnloadAllAssemblies();
    DestroyAppDomain();

    // 2) 새 AppDomain 생성
    if (!CreateAppDomain("ScriptDomain"))
        return false;

    // 3) 재로딩
    for (auto& p : assemblies)
    {
        auto opt = LoadAssembly(p.first, p.second);
        if (!opt) return false;
    }

    // 4) 내부 호출 재등록(도메인마다 필요)
    RegisterInternalCalls();
    return true;
}

MonoThread* MonoManager::AttachCurrentThread()
{
    // 이미 붙어 있으면 nullptr 반환 가능. Attach 보장 위해 결과 체크는 생략
    return mono_thread_attach(m_appDomain);
}

void MonoManager::DetachCurrentThread()
{
    mono_thread_detach(mono_thread_current());
}

void MonoManager::RegisterInternalCalls()
{
    // 여기에 엔진의 모든 바인딩 등록 함수 호출
    Register_Object_ICalls();
    // Register_Component_ICalls();
    // Register_Transform_ICalls();
    // ...
}

MonoClass* MonoManager::GetClass(const char* nameSpace, const char* klassName, MonoImage* image) const
{
    if (!image)
    {
        // 기본: 첫 번째(또는 특정) 스크립트 어셈블리 이미지 사용하고 싶으면 이름을 통일해서 꺼내 쓰자.
        // 여기선 대략 첫 번째 엔트리를 사용
        if (m_assemblies.empty()) return nullptr;
        image = m_assemblies.begin()->second.image;
    }
    return mono_class_from_name(image, nameSpace, klassName);
}

MonoMethod* MonoManager::GetMethod(MonoClass* klass, const char* methodName, int paramCount) const
{
    if (!klass) return nullptr;
    return mono_class_get_method_from_name(klass, methodName, paramCount);
}

MonoObject* MonoManager::InvokeStatic(MonoClass* klass, const char* methodName, void** args, int paramCount, MonoObject** outException)
{
    if (!klass) return nullptr;
    MonoMethod* method = GetMethod(klass, methodName, paramCount);
    if (!method) return nullptr;

    MonoObject* exception = nullptr;
    MonoObject* ret = mono_runtime_invoke(method, /*obj*/nullptr, args, &exception);
    if (outException) *outException = exception;

    if (exception)
    {
        std::cerr << "[Mono.Exception] " << FormatException(exception) << "\n";
    }
    return ret;
}

MonoObject* MonoManager::CreateInstance(MonoClass* klass)
{
    if (!klass) return nullptr;
    MonoObject* obj = mono_object_new(m_appDomain, klass);
    if (!obj) return nullptr;
    mono_runtime_object_init(obj); // .ctor() 호출
    return obj;
}

MonoObject* MonoManager::Invoke(MonoObject* instance, const char* methodName, void** args, int paramCount, MonoObject** outException)
{
    if (!instance) return nullptr;
    MonoClass* klass = mono_object_get_class(instance);
    MonoMethod* method = GetMethod(klass, methodName, paramCount);
    if (!method) return nullptr;

    MonoObject* exception = nullptr;
    MonoObject* ret = mono_runtime_invoke(method, instance, args, &exception);
    if (outException) *outException = exception;

    if (exception)
    {
        std::cerr << "[Mono.Exception] " << FormatException(exception) << "\n";
    }
    return ret;
}

MonoString* MonoManager::ToMonoString(const std::string& s) const
{
    return mono_string_new(mono_domain_get(), s.c_str());
}

std::string MonoManager::FromMonoString(MonoString* ms) const
{
    if (!ms) return {};
    char* utf8 = mono_string_to_utf8(ms);
    std::string out = utf8 ? utf8 : "";
    if (utf8) mono_free(utf8);
    return out;
}

std::string MonoManager::FormatException(MonoObject* exception)
{
    if (!exception) return {};

    MonoClass* exClass = mono_object_get_class(exception);
    MonoMethod* toString = mono_class_get_method_from_name(exClass, "ToString", 0);
    if (!toString) return "<no ToString>";

    MonoObject* exc = nullptr;
    MonoString* str = (MonoString*)mono_runtime_invoke(toString, exception, nullptr, &exc);
    if (exc) return "<exception while formatting exception>";

    char* utf8 = mono_string_to_utf8(str);
    std::string out = utf8 ? utf8 : "";
    if (utf8) mono_free(utf8);
    return out;
}

void MonoManager::GCCollect()
{
    mono_gc_collect(mono_gc_max_generation());
}

void MonoManager::GCWaitForPendingFinalizers()
{
    // corlib(mscorlib) 이미지에서 System.GC 클래스를 직접 얻어 호출
    MonoImage* corlib = mono_get_corlib(); // corlib 이미지 핸들
    if (!corlib) {
        std::cerr << "[Mono] corlib image not found\n";
        return;
    }

    MonoClass* gcClass = mono_class_from_name(corlib, "System", "GC");
    if (!gcClass) {
        std::cerr << "[Mono] System.GC class not found\n";
        return;
    }

    MonoMethod* waitMethod = mono_class_get_method_from_name(gcClass, "WaitForPendingFinalizers", 0);
    if (!waitMethod) {
        std::cerr << "[Mono] GC.WaitForPendingFinalizers() not found\n";
        return;
    }

    MonoObject* exception = nullptr;
    mono_runtime_invoke(waitMethod, /*obj*/nullptr, /*args*/nullptr, &exception);
    if (exception) {
        std::cerr << "[Mono.Exception] " << FormatException(exception) << "\n";
    }
}

MonoImage* MonoManager::GetImage(const std::string& assemblyName) const
{
    auto it = m_assemblies.find(assemblyName);
    if (it == m_assemblies.end()) return nullptr;
    return it->second.image;
}
#endif // UNUSE_MONO_LIB