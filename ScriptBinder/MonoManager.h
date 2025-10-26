#pragma once
#define UNUSE_MONO_LIB // 모노 관련 기능 트리거
#ifndef UNUSE_MONO_LIB
#include <DLLAcrossSingleton.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/attrdefs.h>
#include <mono/metadata/mono-debug.h>

class MonoManager : public DLLCore::Singleton<MonoManager>
{
public:
    struct AssemblyPack
    {
        MonoAssembly* assembly{ nullptr };
        MonoImage* image{ nullptr };
        std::string   name;
        std::string   path;
    };

public:
    // 필수 수명주기
    bool Initialize(const char* domainName,
        const char* monoLibDir,      // ex) "<mono>/lib"
        const char* monoEtcDir,      // ex) "<mono>/etc"
        bool enableDebug = false);   // mdb/pdb 심볼 지원

    void Shutdown();

    // 어셈블리 로드/재로딩
    std::optional<AssemblyPack> LoadAssembly(const std::string& name, const std::string& path);
    void UnloadAllAssemblies();
    bool ReloadAll(const std::vector<std::pair<std::string, std::string>>& assemblies);

    // 기본 도메인/도메인 정보
    MonoDomain* GetRootDomain() const { return m_rootDomain; }
    MonoDomain* GetAppDomain()  const { return m_appDomain; }

    // 스레드 관리
    MonoThread* AttachCurrentThread();
    void        DetachCurrentThread();

    // 내부 호출 등록(여기서 한 번에)
    void RegisterInternalCalls();

    // 클래스/메서드 도우미
    MonoClass* GetClass(const char* nameSpace, const char* klassName, MonoImage* image = nullptr) const;
    MonoMethod* GetMethod(MonoClass* klass, const char* methodName, int paramCount) const;

    // 정적 메서드 호출
    MonoObject* InvokeStatic(MonoClass* klass, const char* methodName, void** args, int paramCount, MonoObject** outException = nullptr);

    // 인스턴스 생성/메서드 호출
    MonoObject* CreateInstance(MonoClass* klass);
    MonoObject* Invoke(MonoObject* instance, const char* methodName, void** args, int paramCount, MonoObject** outException = nullptr);

    // 문자열 변환
    MonoString* ToMonoString(const std::string& s) const;
    std::string FromMonoString(MonoString* ms) const;

    // 예외 포맷 출력
    static std::string FormatException(MonoObject* exception);

    // GC/유틸
    void GCCollect();
    void GCWaitForPendingFinalizers();

    // 이미지/어셈블리 접근
    MonoImage* GetImage(const std::string& assemblyName) const;

private:
    MonoManager() = default;
    ~MonoManager() = default;
    MonoManager(const MonoManager&) = delete;
    MonoManager& operator=(const MonoManager&) = delete;

    bool CreateAppDomain(const char* domainName);
    void DestroyAppDomain();

private:
    MonoDomain* m_rootDomain{ nullptr };
    MonoDomain* m_appDomain{ nullptr };

    std::unordered_map<std::string, AssemblyPack> m_assemblies;
};
static auto MonoManagers = MonoManager::GetInstance();
#endif // !UNUSE_MONO_LIB
