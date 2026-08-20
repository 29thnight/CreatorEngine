#pragma once
#include "Core.Minimal.h"
#include "ClassProperty.h"
#include "EntityHandle.h"
#include "Prefab.h"

class PrefabUtility : public Singleton<PrefabUtility>
{
private:
    friend class Singleton<PrefabUtility>;
    PrefabUtility() = default;
    ~PrefabUtility() = default;

public:
    Core::Delegate<void, Entity&> prefabInstanceUpdated;
    Core::Delegate<void, Entity&> prefabInstanceApplied;
    Core::Delegate<void, Entity&> prefabInstanceReverted;
    Core::Delegate<void, Entity&> prefabInstanceUnpacked;

    // 같은 타입 컴포넌트들 중 이 컴포넌트가 몇 번째인가 (0-based, 못 찾으면 -1).
    //
    // PrefabOverride::m_componentSlot의 **정본 규칙**이다. 기록하는 쪽과
    // 소비하는 쪽(ApplyComponentDiff)이 각자 세면 조용히 어긋나므로 한 곳으로
    // 모았다 — 순서는 obj.m_components의 원래 순서이고, 이는 ApplyComponentDiff가
    // existingByType으로 큐잉하는 순서와 같다.
    static int ComputeComponentSlot(const Entity& obj, const Component* target);

    // 반환은 비소유 관찰 포인터다 — 소유는 m_createdPrefabs가 갖는다. 호출자는 delete하지 않는다.
    Prefab* CreatePrefab(const Entity* source, std::string_view name = "");
    Entity* InstantiatePrefab(const Prefab* prefab, std::string_view name = "");
    Entity* InstantiatePrefab(const Prefab* prefab, Scene* targetScene, std::string_view name = "");
    void RegisterInstance(Entity* instance, const Prefab* prefab);

    // 파괴되는 오브젝트를 인스턴스 목록에서 뺀다.
    //
    // (P2) 추적이 EntityHandle{index,generation}으로 바뀐 뒤로는 세대 검사가 죽은
    // 인스턴스를 읽는 시점에 자동으로 걸러내므로, 이 함수는 더 이상 정합성의
    // 마지막 방어선이 아니다 — 안 불러도 다음 UpdateInstances/RegisteredInstanceCount가
    // 스스로 걸러낸다. 그래도 즉시 지우는 편이 죽은 항목을 다음 읽기까지 들고
    // 있는 것보다 싸므로 계속 부른다. GameObject::Destroy가 부른다.
    void UnregisterInstance(Entity* instance);

    void UpdateInstances(const Prefab* prefab);
    bool SavePrefab(const Prefab* prefab, const std::string& path);

    // 아래 세 Load*는 모두 비소유 관찰 포인터를 반환한다 — 소유는 m_prefabCache가
    // 갖는다(경로 키로 캐시·재사용). 호출자는 delete하지 않는다.
    Prefab* LoadPrefabFullPath(const std::string& path);
    Prefab* LoadPrefab(const std::string& path);
	Prefab* LoadPrefabGuid(const FileGuid& guid);

    // 씬이 통째로 사라질 때 그 씬 소속 인스턴스 항목을 지운다(SceneManager.cpp의
    // delete 지점들이 부른다). EntityHandle은 씬 스코프라 Scene*가 죽으면 그
    // 핸들도 함께 의미를 잃는다 — 안 부르면 댕글링 Scene*를 들고 있게 된다.
    // (소유 파일 밖의 씬 삭제 경로(PrefabEditor.cpp 등)는 Resolve가 SceneManager의
    // 생존 씬 목록으로 방어한다 — 아래 Resolve 주석 참고)
    void ForgetScene(const Scene* scene);

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
    // 이 트랙의 범위 밖이다 — DataSystem 소관)
    std::unordered_map<std::string, std::unique_ptr<Prefab>> m_prefabCache{};

    // 새로 만든 프리팹의 소유자.
    //
    // 로드 경로만 막으면 절반만 막는 것이다 — CreatePrefab도 new Prefab을 돌려주고,
    // 호출자 넷(모델 임포트 셋, 콘솔 prefab.create)이 전부 저장만 하고 버린다.
    // 캐시와 달리 키가 없다(저장 전이라 경로가 아직 없다).
    std::vector<std::unique_ptr<Prefab>> m_createdPrefabs{};

    // 프리팹 인스턴스 추적 (SceneGraphRedesignPlan P2).
    //
    // 예전에는 raw Entity*를 담은 벡터였다 — 메모리 전용이라 씬을 저장했다가
    // 다시 불러오면 등록이 0으로 돌아갔다(재로드된 오브젝트는 새 GameObject*라
    // 옛 포인터와 절대 같을 수 없다). EntityHandle{index,generation}은 씬의
    // 슬롯맵(SceneGraphRedesignPlan 트랙 E1)이 쥔 정체성이라 씬을 다시 열어도
    // Resolve 한 번이면 지금 살아있는 오브젝트를 되찾는다.
    //
    // EntityHandle은 씬 스코프다(같은 index+generation이 씬마다 다른 오브젝트를
    // 가리킬 수 있다) — 그래서 Scene*를 같이 저장한다. 그 씬이 사라지면 핸들도
    // 무의미해지므로 ForgetScene으로 함께 지운다.
    struct InstanceRef
    {
        Scene* scene{ nullptr };
        EntityHandle handle{};
    };
    std::unordered_map<FileGuid, std::vector<InstanceRef>> m_instanceMap{};

    // ref를 지금 살아있는 Entity*로 푼다. 셋 중 하나라도 아니면 nullptr:
    //   - scene이 비었거나 handle이 무효
    //   - scene이 SceneManager의 생존 씬 목록에 없다(ForgetScene이 못 따라잡는
    //     소유 파일 밖 삭제 경로 방어 — 포인터 값 비교만 하고 역참조하지 않는다)
    //   - scene->Resolve(handle)의 세대가 어긋난다(그 슬롯이 다른 오브젝트로 재사용됨)
    static Entity* Resolve(const InstanceRef& ref);
};

static auto PrefabUtilitys = PrefabUtility::GetInstance();
