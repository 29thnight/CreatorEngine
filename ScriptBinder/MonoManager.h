#pragma once
#define UNUSE_MONO_LIB // ��� ���� ��� Ʈ����
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
    // �ʼ� �����ֱ�
    bool Initialize(const char* domainName,
        const char* monoLibDir,      // ex) "<mono>/lib"
        const char* monoEtcDir,      // ex) "<mono>/etc"
        bool enableDebug = false);   // mdb/pdb �ɺ� ����

    void Shutdown();

    // ����� �ε�/��ε�
    std::optional<AssemblyPack> LoadAssembly(const std::string& name, const std::string& path);
    void UnloadAllAssemblies();
    bool ReloadAll(const std::vector<std::pair<std::string, std::string>>& assemblies);

    // �⺻ ������/������ ����
    MonoDomain* GetRootDomain() const { return m_rootDomain; }
    MonoDomain* GetAppDomain()  const { return m_appDomain; }

    // ������ ����
    MonoThread* AttachCurrentThread();
    void        DetachCurrentThread();

    // ���� ȣ�� ���(���⼭ �� ����)
    void RegisterInternalCalls();

    // Ŭ����/�޼��� �����
    MonoClass* GetClass(const char* nameSpace, const char* klassName, MonoImage* image = nullptr) const;
    MonoMethod* GetMethod(MonoClass* klass, const char* methodName, int paramCount) const;

    // ���� �޼��� ȣ��
    MonoObject* InvokeStatic(MonoClass* klass, const char* methodName, void** args, int paramCount, MonoObject** outException = nullptr);

    // �ν��Ͻ� ����/�޼��� ȣ��
    MonoObject* CreateInstance(MonoClass* klass);
    MonoObject* Invoke(MonoObject* instance, const char* methodName, void** args, int paramCount, MonoObject** outException = nullptr);

    // ���ڿ� ��ȯ
    MonoString* ToMonoString(const std::string& s) const;
    std::string FromMonoString(MonoString* ms) const;

    // ���� ���� ���
    static std::string FormatException(MonoObject* exception);

    // GC/��ƿ
    void GCCollect();
    void GCWaitForPendingFinalizers();

    // �̹���/����� ����
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
