#pragma once
#include "Core.Minimal.h"
#include "DLLAcrossSingleton.h"
#include "Prefab.h"

class PrefabUtility : public DLLCore::Singleton<PrefabUtility>
{
private:
    friend class DLLCore::Singleton<PrefabUtility>;
    PrefabUtility() = default;
    ~PrefabUtility() = default;

public:
    Core::Delegate<void, GameObject&> prefabInstanceUpdated;
    Core::Delegate<void, GameObject&> prefabInstanceApplied;
    Core::Delegate<void, GameObject&> prefabInstanceReverted;
    Core::Delegate<void, GameObject&> prefabInstanceUnpacked;

    // 반환은 비소유 관찰 포인터다 — 소유는 m_createdPrefabs가 갖는다. 호출자는 delete하지 않는다.
    Prefab* CreatePrefab(const GameObject* source, std::string_view name = "");
    GameObject* InstantiatePrefab(const Prefab* prefab, std::string_view name = "");
	GameObject* InstantiatePrefab(const Prefab* prefab, Scene* targetScene, std::string_view name = "");
    void RegisterInstance(GameObject* instance, const Prefab* prefab);

    // 파괴되는 오브젝트를 인스턴스 목록에서 뺀다.
    //
    // 지금까지 넣기만 하고 빼는 곳이 없어서, 죽은 오브젝트가 목록에 그대로 남고
    // UpdateInstances가 그것을 역참조했다. GameObject::Destroy가 부른다.
    // (P2에서 목록이 핸들이 되면 세대 검사가 이 일을 대신하므로 이 경로는 사라진다)
    void UnregisterInstance(GameObject* instance);

    void UpdateInstances(const Prefab* prefab);
    bool SavePrefab(const Prefab* prefab, const std::string& path);

    // 아래 세 Load*는 모두 비소유 관찰 포인터를 반환한다 — 소유는 m_prefabCache가
    // 갖는다(경로 키로 캐시·재사용). 호출자는 delete하지 않는다.
    Prefab* LoadPrefabFullPath(const std::string& path);
    Prefab* LoadPrefab(const std::string& path);
	Prefab* LoadPrefabGuid(const FileGuid& guid);

    // 진단용 집계(prefab.status). 왕복 회귀가 "연결이 저장·로드를 건넜는가"를
    // 밖에서 볼 수 있는 유일한 창이다 — 연결이 끊겨도 화면은 멀쩡해 보인다.
    size_t RegisteredInstanceCount() const;
    size_t OwnedPrefabCount() const;

private:
    // 경로를 캐시 키로 정규화한다(절대 경로 + 소문자).
    static std::string CacheKey(const std::string& path);

    // 로드된 프리팹의 소유자.
    //
    // 예전에는 로드할 때마다 new Prefab()을 하고 아무도 지우지 않았다. 게임플레이가
    // 이 경로를 계속 부르기 때문에(적이 죽을 때마다 LoadPrefab("EnemyDeathEffect"))
    // 한 번의 실수가 아니라 계속 새는 구조였다. 이제 여기서 소유하고 재사용한다.
    //
    // 키가 FileGuid가 아니라 경로인 이유: LoadPrefabFullPath는 make_file_guid(path)로,
    // LoadPrefab은 DataSystem이 매긴 GUID로 서로 다르게 매긴다. 같은 파일이 호출
    // 경로에 따라 다른 GUID를 갖는 셈이라 캐시 키로 쓸 수 없다. (그 불일치 자체는
    // P2에서 정리한다)
    std::unordered_map<std::string, std::unique_ptr<Prefab>> m_prefabCache{};

    // 새로 만든 프리팹의 소유자.
    //
    // 로드 경로만 막으면 절반만 막는 것이다 — CreatePrefab도 new Prefab을 돌려주고,
    // 호출자 넷(모델 임포트 셋, 콘솔 prefab.create)이 전부 저장만 하고 버린다.
    // 캐시와 달리 키가 없다(저장 전이라 경로가 아직 없다).
    std::vector<std::unique_ptr<Prefab>> m_createdPrefabs{};

    std::unordered_map<FileGuid, std::vector<GameObject*>> m_instanceMap{};
};

static auto PrefabUtilitys = PrefabUtility::GetInstance();
