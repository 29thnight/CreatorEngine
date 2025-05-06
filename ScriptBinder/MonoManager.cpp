#include "MonoManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "MonoObjectBinding.h"


bool MonoManager::Initialize(const std::string& monoLibDir, const std::string& monoEtcDir)
{
    mono_set_dirs(monoLibDir.c_str(), monoEtcDir.c_str());
    m_domain = mono_jit_init("GameDomain");
    return m_domain != nullptr;
}

bool MonoManager::LoadAssembly(const std::string& assemblyPath)
{
    m_assembly = mono_domain_assembly_open(m_domain, assemblyPath.c_str());
    if (!m_assembly) return false;
    m_image = mono_assembly_get_image(m_assembly);
    return m_image != nullptr;
}

void MonoManager::RegisterInternalCalls()
{
    RegisterObjectBindings(m_image);
    RegisterGameObjectBindings(m_image);
    RegisterComponentBindings(m_image);
}

void MonoManager::ScanMonoBehaviors()
{
    m_behaviorTypeMap.clear();
    MonoClass* baseCls = mono_class_from_name(m_image, "Engine", "MonoBehavior");
    if (!baseCls) return;

    const MonoTableInfo* tinfo = mono_image_get_table_info(m_image, MONO_TABLE_TYPEDEF);
    int rows = mono_table_info_get_rows(tinfo);
    for (int i = 0; i < rows; ++i)
    {
        uint32_t cols[MONO_TYPEDEF_SIZE];
        mono_metadata_decode_row(tinfo, i, cols, MONO_TYPEDEF_SIZE);
        const char* ns = mono_metadata_string_heap(m_image, cols[MONO_TYPEDEF_NAMESPACE]);
        const char* name = mono_metadata_string_heap(m_image, cols[MONO_TYPEDEF_NAME]);
        MonoClass* cls = mono_class_from_name(m_image, ns, name);
        if (!cls || !mono_class_is_subclass_of(cls, baseCls, false))
            continue;

        MonoBehaviorInfo info;
        info.klass = cls;
        info.start = mono_class_get_method_from_name(cls, "Start", 0);
        info.update = mono_class_get_method_from_name(cls, "Update", 1);
        info.onDisable = mono_class_get_method_from_name(cls, "OnDisable", 0);
        //TODO: 모든 이벤트 함수들 검색
        m_behaviorTypeMap[cls] = info;
    }
}

MonoBehaviorHandle MonoManager::RegisterBehaviorInstance(MonoObject* behaviorObj)
{
    MonoBehaviorHandle h = m_nextBehaviorHandle++;
    MonoClass* cls = mono_object_get_class(behaviorObj);
    auto it = m_behaviorTypeMap.find(cls);
    if (it == m_behaviorTypeMap.end()) return 0;
    const MonoBehaviorInfo& info = it->second;

    MonoBehaviorRecord rec;
    rec.id = h;
    rec.instance = behaviorObj;

    // Start (인자 없음)
    if (info.start)
    {
        Core::DelegateHandle dh = SceneManagers->GetActiveScene()->StartEvent.AddLambda(
            [h]() {
                    auto& r = MonoManager::GetInstance()->m_behaviors[h];
                    mono_runtime_invoke(r.events[0].method, r.instance, nullptr, nullptr);
            }
        );
        rec.events.emplace_back(info.start, dh);
    }

    // Update (float 인자)
    if (info.update) {
        Core::DelegateHandle dh = SceneManagers->GetActiveScene()->UpdateEvent.AddLambda(
            [h](float dt) {
                    auto& r = MonoManager::GetInstance()->m_behaviors[h];
                    void* args[1] = { &dt };
                    mono_runtime_invoke(r.events[1].method, r.instance, args, nullptr);
            }
        );
        rec.events.emplace_back(info.update, dh);
    }

    // OnDisable (인자 없음)
    if (info.onDisable)
    {
        Core::DelegateHandle dh = SceneManagers->GetActiveScene()->OnDisableEvent.AddLambda(
            [h]() {
                    auto& r = MonoManager::GetInstance()->m_behaviors[h];
                    mono_runtime_invoke(r.events[2].method, r.instance, nullptr, nullptr);
            }
        );
        rec.events.emplace_back(info.onDisable, dh);
    }

    m_behaviors.emplace(h, std::move(rec));
    return h;
}

void MonoManager::UnregisterBehavior(MonoBehaviorHandle h)
{
    auto it = m_behaviors.find(h);
    if (it == m_behaviors.end()) return;
    auto& rec = it->second;

    for (auto& [method, dh] : rec.events) {
        SceneManagers->GetActiveScene()->StartEvent.Remove(dh);
        SceneManagers->GetActiveScene()->UpdateEvent.Remove(dh);
        SceneManagers->GetActiveScene()->OnDisableEvent.Remove(dh);
    }
    m_behaviors.erase(it);
}
