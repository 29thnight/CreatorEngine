#pragma once
#include "Object.h"
#include "AssetBundle.h"
#include "ReflectionYml.h"
#include "AuthoringDocument.h"
#include "AuthoringNodeView.h" // D3-a-5
#include "ClassProperty.h"
// Index만 필요한데 Entity.h 전체를 물지 않으려고 경량 헤더를 쓴다
// (GameObjectIndex.h 상단 주석 참고). LoadIndexEntry/LoadIndexBatch가 이걸 쓴다.
#include "GameObjectIndex.h"
#include "DetachedEntityTransfer.h"
#include "ScenePhase.h"
#include <future>
#include <thread>

class Scene;
class Entity;
class MeshRenderer;
class Material;
class RenderScene;
class InputActionManager;
class SceneManager : public Singleton<SceneManager>
{
private:
    friend class Singleton<SceneManager>;
    SceneManager() = default;
    ~SceneManager();

public:
	void ManagerInitialize();
    void Editor();

    // ── 씬 구조 변경 (렌더 정지 구간에서만 수행) ──
    //
    // 재생/정지 전환은 씬 오브젝트 목록을 통째로 갈아엎는다. 이 작업을 게임 로직
    // 도중에 하면 커맨드 빌드 스레드와 그 워커 풀이 같은 목록을 순회하는 중일 수
    // 있어, 벡터 재할당 한 번에 반복자가 무효가 된다(GizmoPass에서 재현됨).
    //
    // 그래서 판단과 실행을 나눠, 실행은 렌더 배리어의 두 랑데뷰 사이에서만 한다.
    // 그 구간에서는 커맨드 빌드/실행 스레드가 이번 프레임 작업을 마치고 두 번째
    // 랑데뷰에 묶여 있으므로(빌드 스레드는 워커까지 기다린 뒤 도달한다) 안전하다.
    bool HasPendingSceneStructureChange() const;
    void ApplyPendingSceneStructureChange();

    // ── 씬 스냅샷 / 시뮬레이션 primitive (E3-1) ──
    //
    // 재생 왕복은 성격이 다른 세 가지 일이 한 함수에 뭉쳐 있었다: 씬을 직렬화해
    // 백업하는 것, 백업으로 되채우는 것, 엔티티 phase를 전이시키는 것. 거기에
    // Editor 정책(Undo 비우기·선택 해제)까지 같은 자리에 섞여 있어, 호출부에서
    // 무엇이 런타임 primitive이고 무엇이 Editor 관심사인지 가릴 수 없었다.
    // E3-2/E3-3이 Editor 몫을 들어내려면 런타임 몫에 먼저 이름이 있어야 한다.
    //
    // ⚠ 옛 이름 CreateEditorOnlyPlayScene은 사실과 어긋났다. 씬을 만들지 않고,
    //   에디터 전용도 아니다 — **Player의 유일한 재생 진입 경로가 이 함수다.**
    //   Player는 씬 로드 시 SceneManager.cpp의 Player 모드 분기가 SetGameStart(true)를
    //   부르고, 다음 프레임의 ApplyPendingSceneStructureChange가 같은 코드를
    //   탄다. 이름만 믿고 통째로 Editor로 옮기면 Player는 씬을 로드하고도
    //   스크립트가 한 번도 돌지 않는 정지화면이 된다.
    //
    // ⚠ Player는 정지하지 않는다. m_isGameStart에 쓰는 곳은 SetGameStart 하나뿐이고
    //   Player는 true만 부르므로, Player에서 스냅샷은 한 번 뜨고 **아무도 읽지 않는다**.
    //   E3-6이 Player 분기를 걷어낼 때 이 죽은 직렬화도 함께 없어져야 한다.
    bool CaptureSceneSnapshot();
    bool RestoreSceneSnapshot();
    bool HasSceneSnapshot() const;
    void DiscardSceneSnapshot();
    void SetSimulationPhase(ScenePhase phase);
    void Initialization();
    void Physics(float deltaSecond);
    void InputEvents(float deltaSecond);
    // ⚠ 기본 인자를 두지 않는다(E3-7). 예전에는 `= 0`이라 편집 모드 호출부가
    //   인자를 생략해 delta 0이 조용히 들어갔고, "delta 0은 일시정지에서만"이라는
    //   규약이 지켜지는지 호출부만 봐서는 알 수 없었다. 0을 넘길 이유가 있으면
    //   호출부가 0.0f라고 적는다.
    void GameLogic(float deltaSecond);
    void SceneRendering(float deltaSecond);
	void OnDrawGizmos();
    void GUIRendering();
    void EndOfFrame();
    void Pausing();
    void DisableOrEnable();
    void Decommissioning();
    // Main-thread teardown boundary, before managed script state is released.
    void DrainAIUpdates();
    // Cancel and drain owned loads before CLR/DataSystem/common workers are torn down.
    void DrainSceneLoads();
    void SetDecommissioning();
    bool IsDecommissioning() const { return m_exitCommand; }

    Scene* GetActiveScene() { return m_activeScene; }
    Scene* GetScene(size_t index) { return m_scenes[index]; }

    Scene* CreateScene(std::string_view name = "SampleScene");
	Scene* SaveScene(std::string_view name = "SampleScene");
    Scene* LoadSceneImmediate(std::string_view name = "SampleScene");
	Scene* LoadScene(std::string_view name = "SampleScene");

	void SaveSceneAsync(std::string_view name = "SampleScene");
    // Owner thread only. Parsing/assets use engine jobs; entities are constructed at
    // ApplyPendingSceneStructureChange or WaitForSceneLoad (scene structure boundary).
    // future is only a result channel: do not block the owner with get() before pumping.
    // SceneManager owns successful results, including an abandoned future. Failure or
    // cancellation yields nullptr. Callback requests replace earlier pending callbacks.
	std::future<Scene*> LoadSceneAsync(std::string_view name = "SampleScene");
    void LoadSceneAsyncAndWaitCallback(std::string_view name = "SampleScene");
    void ActivateScene(Scene* sceneToActivate, bool isOldSceneDelete = true);
	void BeforeAwakeSceneLoad();
	bool IsSceneLoading() const;
    // Blocks for preparation and constructs results; activation remains frame-boundary work.
    void WaitForSceneLoad();

    RenderScene* GetRenderScene() { return m_ActiveRenderScene; }
    void SetRenderScene(RenderScene* renderScene) { m_ActiveRenderScene = renderScene; }
    void AddDontDestroyOnLoad(Object* objPtr);
	void RemoveDontDestroyOnLoad(Object* objPtr);
	void RebindEventDontDestroyOnLoadObjects(Scene* scene);
    
	std::vector<Scene*>& GetScenes() { return m_scenes; }
	std::vector<Object*>& GetDontDestroyOnLoadObjects() { return m_dontDestroyOnLoadObjects; }
	void SetActiveScene(Scene* scene) { m_activeScene = scene; }
	void SetActiveSceneIndex(size_t index) { m_activeSceneIndex = index; }
	size_t GetActiveSceneIndex() { return m_activeSceneIndex; }
	bool IsGameStart() const { return m_isGameStart; }
	void SetGameStart(bool isStart);

	// ── 재생 상태 신호 셋 (PHASE 21 W5 선행 1) ──
	//
	// `IsGameStart` 는 **요청**이다 — 버튼·CLI 가 세우고 그 자리에서 참이 된다.
	// 실제 전이는 프레임 경계의 ApplyPendingSceneStructureChange 가 하고, 그 안의
	// 스냅샷이 실패하면 전이는 없다. 요청 하나만 읽는 UI 는 그때 "Stop 아이콘이
	// 뜬 채 시뮬레이션은 없는" 상태를 보인다(계획서 §1.6 실측). 그래서 셋이다:
	//
	//   요청   IsGameStart()                       버튼을 눌렀는가
	//   진행   HasPendingSceneStructureChange()    요청은 섰고 전이는 아직인가
	//   확정   IsPlayCommitted()                   스냅샷·phase·통지까지 끝났는가
	//
	// 확정은 스냅샷이 뜬 **뒤**에만 참이 되고, 실패하면 요청까지 되돌린다 —
	// 되돌리지 않으면 정지할 때 "백업이 없어 복원하지 못했다" 로 편집 씬을 잃는다.
	bool IsPlayCommitted() const { return m_isPlayCommitted; }

	/// 전이가 거부된 횟수와 마지막 사유. 게이트가 "실패했는데 재생으로 보이지
	/// 않는다" 를 단정하려면 실패 자체가 밖에서 보여야 한다. 사유 문자열은 전이가
	/// 도는 게임 스레드에서만 쓰고 읽는다(CLI 도 같은 스레드).
	std::uint32_t PlayFailureCount() const { return m_playFailureCount; }
	const std::string& LastPlayFailure() const { return m_lastPlayFailure; }

	/// 다음 `count` 번의 스냅샷을 실패시킨다 — 검증용 주입 자리.
	///
	/// 실제 실패(직렬화 예외·빈 엔티티 목록)는 살아 있는 에디터에서 재현할 손잡이가
	/// 없다. 새 씬은 루트 엔티티를 하나 갖고 나서 "비어 있음" 에 닿지 않고, 예외는
	/// 자산을 망가뜨려야 난다. 실패 경로가 한 번도 돌지 않는 게이트는 그 경로를
	/// 재지 않으므로 주입을 둔다. 제품 코드는 이것을 부르지 않는다.
	void InjectPlaySnapshotFailure(std::uint32_t count) { m_injectedSnapshotFailures = count; }

	bool IsGamePaused() const { return m_isGamePaused; }
	void SetGamePaused(bool isPaused);
	void ToggleGamePaused();

	bool IsEditorSceneLoaded() const { return m_isEditorSceneLoaded; }
    InputActionManager* GetInputActionManager() { return m_inputActionManager; }
    void SetInputActionManager(InputActionManager* inputActionManager) { m_inputActionManager = inputActionManager;}

    std::vector<MeshRenderer*> GetAllMeshRenderers() const;
    // GT frame sealing용. 활성 Scene의 mesh/foliage가 실제 소유한 Material을
    // cache 소속 여부와 무관하게 owner snapshot으로 돌려준다.
    std::vector<std::shared_ptr<Material>> CaptureRequiredRenderMaterials() const;

	void VolumeProfileApply();
	bool IsVolumeProfileApply() const { return m_volumeProfileApply; }
	void ResetVolumeProfileApply() { m_volumeProfileApply = false; }

public:
	// 재생 진입/이탈 통지 (E3-2). 인자는 isEntering — true면 진입, false면 이탈.
	//
	// Editor가 재생 전환에서 자기 정책을 걸 자리다. Core는 여기서 무엇이 일어나는지
	// 알지 못하고 응답도 쓰지 않는다. Player는 EngineEntry를 링크하지 않으므로
	// 구독자가 없고, 그러면 Broadcast는 아무 일도 하지 않는다 — 출하 게임의 재생
	// 진입에는 순수 런타임 동작만 남는다.
	//
	// 선언만 있고 Broadcast·구독 0건인 죽은 이벤트였어서(E3-2 착수 전 실측) 시그니처를
	// 바꿔도 깨질 소비자가 없었다.
	//
	// ⚠ 선택 해제(resetSelectedObjectEvent)는 이쪽으로 오지 않는다. 이름과 달리 그것은
	//   Editor 정책이 아니라 댕글링 방지 안전장치이고, AllDestroyMark 이전이라는 위치가
	//   그 안전을 만든다 — 빼면 재생 정지에서 ACCESS_VIOLATION으로 죽는다(실측).
	Core::Delegate<void, bool>          PlayModeEvent{};
    //for Game Logic
    Core::Delegate<void, float>         InputEvent{};
    //for RenderEngine
    Core::Delegate<void, float>         SceneRenderingEvent{};
	Core::Delegate<void>                OnDrawGizmosEvent{};
    Core::Delegate<void>                GUIRenderingEvent{};
    Core::Delegate<void, float>         InternalAnimationUpdateEvent{};
    //Manager Events
    Core::Delegate<void>                activeSceneChangedEvent{};

    // 활성 씬이 바뀌었음을 알린다. 프로파일러의 '길이 없는 사건'(§7.3)을
    // 함께 찍으므로 **이 함수를 거쳐야** 타임라인의 경계 띠에 남는다.
    //
    // ★ Broadcast 를 직접 부르는 자리가 넷이었다. 넷에 같은 줄을 베끼면
    //   나중에 생기는 다섯째가 조용히 빠진다.
    void NotifyActiveSceneChanged();

    Core::Delegate<void>                sceneLoadedEvent{};
    Core::Delegate<void>                sceneUnloadedEvent{};
    Core::Delegate<void>                newSceneCreatedEvent{};
	Core::Delegate<void>                resetSelectedObjectEvent{};
    Core::Delegate<void>                endOfFrameEvent{};
	Core::Delegate<void>                resourceTrimEvent{};
    
	Core::Delegate<void>                AssetLoadEvent{};

    std::atomic_bool                    m_isGameStart{ false };
    std::atomic_bool                    m_isGamePaused{ false };
	std::atomic_bool			        m_isEditorSceneLoaded{ false };
	std::atomic_bool                    m_isPlayCommitted{ false };
	std::atomic_uint32_t                m_playFailureCount{ 0 };
	std::string                         m_lastPlayFailure{};
	std::uint32_t                       m_injectedSnapshotFailures{ 0 };
	std::atomic_bool                    m_isInitialized{ false };
	size_t 					            m_EditorSceneIndex{ 0 };

    InputActionManager*                 m_inputActionManager{ nullptr };
private:
    struct PendingSceneLoad;
    std::future<Scene*> BeginSceneLoad(std::string_view path, bool autoActivate);
    void CompleteSceneLoads(bool wait);
    Scene* BuildPreparedScene(const PendingSceneLoad& load);
    void RequireSceneLoadOwner() const;
    std::thread::id m_sceneLoadOwner{std::this_thread::get_id()};
    std::vector<std::shared_ptr<PendingSceneLoad>> m_pendingSceneLoads;
    std::atomic_size_t m_pendingSceneLoadCount{0};
    size_t m_sceneLoadEpoch = 0;
    Scene* m_asyncSceneToActivate = nullptr;

    // Edit→Play→Stop transaction. 위의 primitive들을 조립한다. Editor 정책(Undo)은
    // E3-2 가 EditorPlayModeController 로 들어냈고, 선택 해제는 안전장치라 남는다.
    //
    // 진입은 **스냅샷 → phase → 통지** 순이고, 스냅샷이 실패하면 아무것도 바꾸지
    // 않은 채 false 를 돌려준다(PHASE 21 W5 선행 1). 호출자가 요청을 되돌린다.
    bool BeginPlayTransaction();
	void EndPlayTransaction();
    void NotePlayFailure(std::string reason);

    // ── 배치 단위 인덱스 리매핑 (SceneLoaderBatchRemapPlan) ──
    //
    // E1(슬롯맵)이 파괴 시 전원 재인덱싱을 없애면서, 그 재인덱싱 패스가 몰래
    // 겸하던 일도 함께 사라졌다 — 저장 순회 순서와 로드 순회 순서가 달라 파일의
    // m_index가 실제 슬롯 위치와 어긋난 씬을 로드 시점에 수선해 주던 것이다.
    // (FT_Material: 파일 인덱스 14건 전부 반전, Gunner_F_Mythic: 본 60여 개 어긋남 — 실측)
    //
    // Deserialize는 Entity당 하나씩 불리므로, 그 안에서는 "이 배치에 아직
    // 로드되지 않은 다른 Entity"를 알 수 없다. H3부터 파일 인덱스 스킴의
    // parent/root/children은 Entity 필드에 임시 적재하지 않고 이 DTO가 보존한다.
    // 로드 루프가 끝난 직후 RemapLoadBatchIndices가 Store에 슬롯 인덱스로 쓴다.
    struct LoadIndexEntry
    {
        Entity* object{ nullptr };
        GameObjectIndex fileIndex{ -1 };
		GameObjectIndex fileParentIndex{ -1 };
		GameObjectIndex fileRootIndex{ 0 };
		std::vector<GameObjectIndex> fileChildrenIndices{};
		// DDOL 파일 절은 계층 파일 인덱스가 슬롯 인덱스로 리맵된 뒤에만
		// SetDontDestroyOnLoad를 실행한다. 로드 중간의 Store에는 bootstrap 값만
		// 있고 파일 스킴은 위 DTO에 격리되어 있다.
		bool makeDontDestroy{ false };
    };
    using LoadIndexBatch = std::vector<LoadIndexEntry>;

    void DesirealizeGameObject(const Meta::Type* type, const Authoring::NodeView& itNode, LoadIndexBatch* batch = nullptr);
    void DesirealizeGameObject(Scene* targetScene, const Meta::Type* type, const Authoring::NodeView& itNode, LoadIndexBatch* batch = nullptr);
	void DesirealizeDontDestroyOnLoadObjects(Scene* targetScene, const Meta::Type* type, const Authoring::NodeView& itNode, LoadIndexBatch* batch = nullptr);

    // 배치가 끝난 직후 한 번 호출. targetScene이 null이거나 batch가 비어 있으면
    // 아무것도 하지 않는다(로더별 타깃 씬이 갈리는 경우 배치도 나눠 호출한다 —
    // SceneManager.cpp의 각 호출부 주석 참고).
    void RemapLoadBatchIndices(Scene* targetScene, LoadIndexBatch& batch);
private:
    std::atomic<Scene*>                 m_sceneToActivate{};
    std::vector<Scene*>                 m_scenes{};

    // 재생 시작 직전의 에디터 씬 스냅샷. 정지하면 이 노드로 같은 Scene 객체를
    // 되채운다(EnterPlayMode/ExitPlayMode 주석 참조).
    // D3-a-3: backend 노드를 값으로 들지 않고 문서 소유 타입이 감싼다(§3.3).
    // 이 헤더는 이 멤버 때문에 포맷 타입을 알 필요가 없다 — 실제 노드
    // 접근은 SceneManager.cpp가 `AuthoringDocumentAccess.h`로 얻는다.
    Authoring::Document                 m_editorSceneBackup{};
    // 소유는 활성 Scene(또는 이송 중 아래 transfer vector)에만 있다.
    std::vector<Object*>                m_dontDestroyOnLoadObjects{};
    std::vector<DetachedEntityTransfer> m_detachedDontDestroyOnLoadObjects{};
    AssetBundle                         m_dontDestroyOnLoadAssetsBundle{};
    std::atomic<Scene*>                 m_activeScene{};
    std::atomic<RenderScene*>           m_ActiveRenderScene{ nullptr };
	std::string                         m_LoadSceneName{};
    std::atomic_size_t                  m_activeSceneIndex{};
	std::atomic_bool                    m_volumeProfileApply{ false };
    std::atomic_bool                    m_exitCommand{ false };
    std::atomic_bool                    m_isOldSceneDelete = true;
};

static auto SceneManagers = SceneManager::GetInstance();
#pragma region SceneManagerEvents
static auto& PlayModeEvent = SceneManager::GetInstance()->PlayModeEvent;
static auto& InputEvent = SceneManager::GetInstance()->InputEvent;
static auto& SceneRenderingEvent = SceneManager::GetInstance()->SceneRenderingEvent;
static auto& OnDrawGizmosEvent = SceneManager::GetInstance()->OnDrawGizmosEvent;
static auto& GUIRenderingEvent = SceneManager::GetInstance()->GUIRenderingEvent;
static auto& InternalAnimationUpdateEvent = SceneManager::GetInstance()->InternalAnimationUpdateEvent;
static auto& activeSceneChangedEvent = SceneManager::GetInstance()->activeSceneChangedEvent;
static auto& sceneLoadedEvent = SceneManager::GetInstance()->sceneLoadedEvent;
static auto& sceneUnloadedEvent = SceneManager::GetInstance()->sceneUnloadedEvent;
static auto& newSceneCreatedEvent = SceneManager::GetInstance()->newSceneCreatedEvent;
static auto& resetSelectedObjectEvent = SceneManager::GetInstance()->resetSelectedObjectEvent;
static auto& endOfFrameEvent = SceneManager::GetInstance()->endOfFrameEvent;
static auto& resourceTrimEvent = SceneManager::GetInstance()->resourceTrimEvent;
#pragma endregion
