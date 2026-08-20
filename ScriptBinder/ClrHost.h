#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "SpinLock.h"
#include "ScriptObjectRegistry.h"
#include "ScriptLifecyclePhase.h"

// CoreCLR 호스팅.
//
// 설계 문서 02절의 "틱당 한 번만 경계를 넘는다"를 그대로 구현한다.
// 여기 있는 Tick* 함수가 경계의 전부이고, 스크립트가 몇 개든 호출 횟수는 고정이다.
//
// 관리 어셈블리(ScriptCore.dll)는 실행 파일 옆 Managed\ 폴더에서 찾는다.
// 없으면 조용히 비활성 상태로 남는다 — 스크립트 없이도 에디터는 떠야 하기 때문이다.
class ClrHost
{
public:
	static ClrHost& Get();

	// 런타임을 올리고 진입점을 바인딩한다. 실패해도 엔진은 계속 동작한다.
	bool Initialize();
	void Shutdown();

	bool IsReady() const { return m_ready; }

	// ── 틱 진입점 (게임 스레드에서만 호출할 것) ──
	// 관리 코드 호출은 게임 스레드로 한정한다. CoreCLR GC가 스레드를 정지시키므로
	// 렌더 스레드가 물리면 프레임이 통째로 GC에 묶인다(설계 문서 01절).
	void TickAwake();
	// 틱 축은 물리 기준 두 지점이다(설계 문서 §4 트랙 L5). 옛 FixedUpdate/Update/
	// LateUpdate 셋을 대체한다 — 셋 다 실제로는 물리 뒤였고, 이름이 그 사실을
	// 감추고 있었다. 프레임 루프(EditorMain)가 이 둘을 물리 앞뒤로 나눠 부른다.
	void TickPrePhysics(float deltaTime);
	void TickPostPhysics(float deltaTime);

	/// 씬 하나가 완전히 헐린 뒤에 부른다. 관리 측이 고아 인스턴스를 거둔다.
	///
	/// ⚠ 반드시 scene->EndFramePass() '뒤'에서 부를 것. 순서가 이 함수의 전부다.
	///
	/// 파괴 전에 부르면 아직 살아 있는 스크립트를 거두게 되고, 그 직후 컴포넌트의
	/// OnDestroy가 이미 사라진 인스턴스를 해제하려 든다. sceneUnloadedEvent가 정확히
	/// 그 자리(파괴 전)라 그 이벤트에 붙일 수 없다 — 편해 보이지만 틀린 자리다.
	///
	/// 관리 측이 '전부 비우기'가 아니라 '소유자가 죽은 것만 거두기'인 것도 순서와
	/// 같은 이유에서다. Scene::AllDestroyMark가 DontDestroyOnLoad 오브젝트를 건너뛰므로
	/// 그 스크립트는 파괴 표시조차 되지 않은 채 관리 목록에 남는다. 통째로 비우면
	/// 오브젝트는 살아서 다음 씬으로 넘어가는데 스크립트만 죽는다 — 크래시가 아니라
	/// '저 오브젝트만 스크립트가 안 돈다'로 나타나 원인을 짚기 어려운 종류다.
	///
	/// 실측(씬 왕복 2회, 스크립트·BT 적재/해제)으로는 거둘 것이 0건이다. 즉 이것은
	/// 청소부가 아니라 그물이고, 무언가 걸리면 관리 측이 로그를 남긴다.
	void NotifySceneUnload();

	// 방금 만들어진 스크립트를 즉시 깨운다(프리팹 스폰 직후).
	void FlushPendingAwake();

	// 관리 측 Float3와 배치가 같아야 한다.
	struct ScriptFloat3 { float x{}, y{}, z{}; };

	// UI 좌표·오프셋용. 관리 측 Float2와 배치가 같다.
	struct ScriptFloat2 { float x{}, y{}; };

	// ── 물리 콜백 ──
	//
	// 충돌마다 경계를 넘으면 "틱당 1회" 원칙이 무너진다. 발생 시점에는 큐에만 담고,
	// 틱 경계에서 배열 하나로 넘긴다(설계 문서 02절 "부분" 방식).
	enum class PhysicsEventKind : int
	{
		TriggerEnter = 0, TriggerStay = 1, TriggerExit = 2,
		CollisionEnter = 3, CollisionStay = 4, CollisionExit = 5,
	};

	// 관리 측 Collision·PhysicsEvent와 배치가 같아야 한다.
	struct ScriptCollision
	{
		ScriptObjectHandle other;
		int contactCount{ 0 };
		ScriptFloat3 contact;
	};

	struct ScriptPhysicsEvent
	{
		int instanceId{ -1 };
		int kind{ 0 };
		ScriptCollision collision;
	};

	// 물리 이벤트를 큐에 담는다(발생 시점에 호출).
	void QueuePhysicsEvent(int instanceId, PhysicsEventKind kind,
		Entity* other, const std::vector<Mathf::Vector3>& contactPoints);

	// 큐에 모인 것을 한 번에 관리 측으로 넘긴다(틱 경계에서 호출).
	void FlushPhysicsEvents();

	// ── 애니메이션 상태 스크립트 ──
	//
	// 수명은 AnimationState가 쥐고 있다. 콜백은 물리와 같이 큐에 모았다가
	// 틱 경계에서 한 번에 넘긴다 — 상태 전이마다 경계를 넘지 않기 위해서다.
	enum class AniEventKind : int { Enter = 0, Update = 1, Exit = 2 };

	// 관리 측 AniEvent와 배치가 같아야 한다.
	struct ScriptAniEvent
	{
		int instanceId;
		int kind;
		float deltaTime;
		ScriptObjectHandle owner;
	};

	bool HasAniBehaviour(std::string_view typeName);
	int  CreateAniBehaviour(std::string_view typeName);
	void DestroyAniBehaviour(int instanceId);
	void QueueAniEvent(int instanceId, AniEventKind kind, float deltaTime, Entity* owner);
	void FlushAniEvents();

	// 등록된 애니메이션 상태 스크립트 이름 목록 — 애니메이터 편집기의 선택 목록용.
	std::vector<std::string> GetAniBehaviourTypeNames();

	// ── 이름으로 부르는 콜백 ──
	//
	// 애니메이션 키프레임 이벤트와 입력 액션이 이 통로를 함께 쓴다. 둘 다 에셋에
	// "메서드 이름"이 저장돼 있고(구 C++ 경로가 리플렉션으로 부르던 그 이름),
	// 관리 측은 생성기가 만든 이름→직접 호출 표로 받는다.
	//
	// 물리·애니메이션 이벤트와 같은 규약이다: 발생 시점에 넘기지 않고 큐에 모았다가
	// 틱 경계에서 한 번에 전달한다. 입력과 키프레임은 프레임마다 여러 건이 몰린다.
	static constexpr int kScriptMessageNameCapacity = 64;

	// 관리 측 ScriptMessage와 배치가 같아야 한다.
	struct ScriptMessage
	{
		int  instanceId;
		char name[kScriptMessageNameCapacity];
	};

	// 주의: 큐에 담는 쪽은 스레드 안전하다. 애니메이션 갱신이 잡 스레드에서 돌고
	// 거기서 키프레임 이벤트가 발생하기 때문이다(AnimationJob의 스레드 풀 람다).
	// 반대로 Flush는 게임 스레드 전용이다 — 관리 측 호출은 GC 때문에 그래야 한다.
	void QueueScriptMessage(int instanceId, std::string_view methodName);
	void FlushScriptMessages();

	// ── 행동 트리 (PHASE 9-8) ──
	//
	// 트리 전체가 관리 측에 있다. BT의 틱은 root->Tick이 재귀로 내려가며 각 노드의
	// 반환값으로 분기하므로 본질적으로 동기다 — 노드 하나만 관리 측에 두면 크로싱이
	// 노드 수만큼 생기고 블랙보드 접근에서 또 중첩된다. 트리째 옮기면 크로싱은
	// "이 트리를 틱하라" 한 번으로 끝난다(설계 문서 BehaviorTreeManagedPlan §1).

	static constexpr int kBTNameCapacity = 64;

	// 관리 측 BTNodeDesc와 배치가 같아야 한다.
	//
	// 저작 그래프(BTBuildGraph)는 포인터 맵이라 그대로 넘길 수 없고, 노드마다 넘기면
	// 위의 이유가 무너진다. 넘기는 순간에만 평평하게 펴고 부모·자식은 GUID로 잇는다.
	// 저작 데이터의 형식(YAML)은 건드리지 않는다 — 기존 에셋이 그대로 열려야 한다.
	struct ScriptBTNodeDesc
	{
		uint64_t id{ 0 };
		uint64_t parentId{ 0 };
		int32_t  type{ 0 };        // BehaviorNodeType
		int32_t  policy{ 0 };      // ParallelPolicy
		int32_t  isRoot{ 0 };      // bool은 배치가 흔들려 int로 고정
		int32_t  hasScript{ 0 };
		float    weight{ 0.0f };   // 부모가 WeightedSelector일 때의 가중치
		int32_t  childOrder{ 0 };  // 부모의 자식 목록에서의 자리 = 실행 순서
		char     name[kBTNameCapacity]{};
		char     scriptName[kBTNameCapacity]{};
	};

	// 배치가 어긋나면 경고 없이 쓰레기 값이 넘어간다 — 그럴듯한 숫자가 나오는
	// 종류라 눈으로 알아채기 어렵다(9-7의 GcStats에서 같은 이유로 못을 박았다).
	// uint64 둘(16) + int32 넷(16) + float(4) + int32(4) + 이름 둘(128) = 168.
	// 가장 큰 멤버가 uint64라 8바이트 정렬이고 168은 8의 배수라 패딩이 없다.
	static_assert(sizeof(ScriptBTNodeDesc) == 168,
		"ScriptBTNodeDesc 배치가 관리 측 BTNodeDesc와 어긋났다 (ScriptCore/BTGraphBuilder.cs)");

	static constexpr int kBBKeyCapacity = 64;
	static constexpr int kBBStringCapacity = 128;

	// 관리 측 BBEntry와 배치가 같아야 한다.
	//
	// 저작된 블랙보드 값이 관리 측 트리의 초기 상태가 되어야 기존 BT 에셋이 같은
	// 행동을 한다. 비워 두면 노드가 읽는 값이 전부 기본값이 되는데, 그 차이는
	// 크래시가 아니라 "AI가 좀 이상하다"로 나타나 원인을 짚기 어렵다.
	struct ScriptBBEntry
	{
		int32_t type{ 0 };       // BlackBoardType
		int32_t boolValue{ 0 };
		int32_t intValue{ 0 };
		float   floatValue{ 0.0f };
		float   x{ 0.0f }, y{ 0.0f }, z{ 0.0f }, w{ 0.0f };
		char    key[kBBKeyCapacity]{};
		char    stringValue[kBBStringCapacity]{};
	};

	// int32 넷(16) + float 넷(16) + 키(64) + 문자열(128) = 224.
	// 최대 정렬이 4바이트라 패딩이 없다.
	static_assert(sizeof(ScriptBBEntry) == 224,
		"ScriptBBEntry 배치가 관리 측 BBEntry와 어긋났다 (ScriptCore/BTGraphBuilder.cs)");

	/// 그래프와 저작 블랙보드를 함께 넘겨 트리를 만든다.
	/// 성공하면 0 이상의 인스턴스 id, 실패하면 음수.
	int CreateBehaviorTree(Entity* owner, const ScriptBTNodeDesc* nodes, int count,
		const ScriptBBEntry* entries, int entryCount);
	bool DestroyBehaviorTree(int instanceId);

	// 관리 측 AITick과 배치가 같아야 한다.
	struct ScriptAITick
	{
		int32_t instanceId{ -1 };
		float   deltaTime{ 0.0f };
	};

	// int32 + float = 8. 다른 전달 구조체와 같은 규약으로 못을 박는다.
	static_assert(sizeof(ScriptAITick) == 8,
		"ScriptAITick 배치가 관리 측 AITick과 어긋났다 (ScriptCore/BTGraphBuilder.cs)");

	// ⚠ 담는 쪽은 스레드 안전해야 한다.
	//
	// AI 갱신은 게임 스레드가 아니라 std::async가 띄운 스레드에서 돈다
	// (Scene::OnDestroy 말미의 m_AIFuture). 즉 QueueAITick은 그 스레드에서 불린다.
	// 반대로 Flush는 게임 스레드 전용이다 — 관리 측 호출은 GC 때문에 그래야 한다.
	// 애니메이션 키프레임(ScriptMessage)이 같은 상황이고 같은 규약을 쓴다.
	void QueueAITick(int instanceId, float deltaTime);
	void FlushAITicks();

	// ── BT 진단 (PHASE 9-8 완료 기준 1·3) ──
	//
	// 트리 생성·틱은 실패할 때만 로그를 남긴다. 그래서 "트리가 안 서서 AI가 가만히
	// 있다"와 "정상 동작"이 밖에서 구분되지 않는다 — 둘 다 무음이다. 회귀 세트도
	// BT를 쓰는 씬을 열지 않으므로, 전부 통과해도 BT 코드는 한 줄도 실행되지 않은
	// 채 통과할 수 있다. 이 표가 그 자리를 메운다.

	// 관리 측 BTStats와 배치가 같아야 한다.
	struct ScriptBTStats
	{
		int32_t treeCount{ 0 };
		int32_t nodeTypeCount{ 0 };
		int64_t tickCount{ 0 };
		int64_t skippedCount{ 0 };
	};

	// 배치가 어긋나면 경고 없이 쓰레기 값이 넘어온다 — 그럴듯한 숫자가 나오는
	// 종류라 눈으로 알아채기 어렵다(GcStats에서 같은 이유로 못을 박았다).
	// int32 둘(8) + int64 둘(16) = 24. 최대 정렬이 8이고 24는 8의 배수라 패딩이 없다.
	static_assert(sizeof(ScriptBTStats) == 24,
		"ScriptBTStats 배치가 관리 측 BTStats와 어긋났다 (ScriptCore/BTGraphBuilder.cs)");

	/// 관리 측 지표를 읽는다. 스크립트 계층이 비활성이거나 구 어셈블리면 false.
	bool GetBehaviorTreeStats(ScriptBTStats& outStats);

	/// 관리 측 누계를 0으로 되돌린다(트리는 그대로).
	void ResetBehaviorTreeStats();

	// 네이티브 쪽 경계 계수. "프레임당 크로싱 1회"는 관리 측에서는 볼 수 없다 —
	// 넘어온 뒤에는 몇 번에 나눠 왔는지 알 수 없기 때문이다. 여기서만 셀 수 있다.
	struct AICrossingCounters
	{
		uint64_t flushCalls{ 0 };     // FlushAITicks 호출 수 = BT를 흘려보낸 프레임 수
		uint64_t crossings{ 0 };      // 실제로 경계를 넘은 횟수(큐가 비면 넘지 않는다)
		uint64_t ticksDelivered{ 0 }; // 그 크로싱으로 전달한 틱 총량
		uint64_t maxBatch{ 0 };       // 한 번에 넘긴 최대 틱 수
	};

	const AICrossingCounters& AICrossings() const { return m_aiCrossings; }
	void ResetAICrossings() { m_aiCrossings = AICrossingCounters{}; }

	// 사용자 BT 노드의 갈래. 관리 측 BTNodeKind와 값이 같아야 한다.
	enum class BTNodeKind : int { None = 0, Action = 1, Condition = 2, ConditionDecorator = 3 };

	/// 갈래별 사용자 노드 타입 이름 — BT 편집기의 선택 목록용.
	/// 편집기가 메뉴를 열 때만 도는 경로라 틱 규약과 무관하다.
	std::vector<std::string> GetBTNodeTypeNames(BTNodeKind kind);

	/// 이 이름의 사용자 노드가 등록됐는가. 편집기가 노드를 그릴 때 노드마다 부르므로
	/// 목록을 받아 훑는 대신 판정만 받는다.
	bool HasBTNodeType(std::string_view typeName);

	// ── 인스턴스 ──
	// 성공하면 0 이상의 인스턴스 id, 실패하면 음수.
	int CreateBehaviour(Entity* owner, std::string_view typeName);
	bool DestroyBehaviour(int instanceId);

	/// 관리 인스턴스 하나에 생명주기 단계 하나를 직접 전달한다(트랙 L · L3 잔여).
	/// 사유와 값 규약은 ScriptLifecyclePhase.h 상단에 있다 — 요약하면 관리 측
	/// 생명주기의 드라이버를 네이티브 하나로 모으는 전송로이고, 지금은 그중
	/// 씬 편입/이탈 두 단계만 DDOL 이송 경로에서 태운다.
	bool DispatchLifecycle(int instanceId, ScriptLifecyclePhase phase);

	// 등록된 스크립트 타입 이름 목록 — 에디터의 컴포넌트 추가 메뉴용.
	// 선택 바인딩이라 구 어셈블리에서는 빈 목록을 돌려줄 수 있다.
	std::vector<std::string> GetBehaviourTypeNames();


	// 마지막 틱에서 관리 측이 보고한 활성 스크립트 수(경계 로그·진단용).
	int LastActiveCount() const { return m_lastActiveCount; }

	// ── 관리 힙 (PHASE 9-6·9-7) ──
	//
	// CoreCLR이 들어오면서 힙이 둘이 됐다. 원칙은 "GC에 수명을 맡기지 않는다,
	// 대신 GC가 도는 시점을 엔진이 쥔다"이다 — 세대 핸들이 수명의 진실이고
	// GC가 관여하는 것은 언제 도는가뿐이다.
	//
	// 전부 게임 스레드 전용이다(위 틱 진입점과 같은 규약).

	// 관리 측 GcStats와 배치가 같아야 한다.
	struct ScriptGcStats
	{
		int32_t gen0Collections{ 0 };
		int32_t gen1Collections{ 0 };
		int32_t gen2Collections{ 0 };
		int64_t heapSizeBytes{ 0 };
		int64_t totalAllocatedBytes{ 0 };
		int64_t fragmentedBytes{ 0 };
		int64_t pauseTimePercentageX100{ 0 };
	};

	// 배치가 어긋나면 경고 없이 쓰레기 값이 넘어온다 — 지표가 틀렸다는 것을
	// 눈으로 알아채기 어려운 종류라(그럴듯한 숫자가 나온다) 여기서 못 박는다.
	// int32 셋(12) + 정렬 패딩(4) + int64 넷(32) = 48.
	static_assert(sizeof(ScriptGcStats) == 48,
		"ScriptGcStats 배치가 관리 측 GcStats와 어긋났다 (ScriptCore/GcControl.cs)");

	// 씬 경계에서 확정 수집한다. 비싸다(수십 ms) — 씬 전환에서만 부른다.
	// 프레임 루프에서 부르면 그 프레임이 통째로 GC에 묶인다.
	void CollectManagedHeap();

	// 재생 중에는 gen2 블로킹 수집을 억제한다(요청이지 보장은 아니다).
	void SetManagedLatencyMode(bool lowLatency);

	// 지표를 읽는다. 스크립트 계층이 비활성이거나 구 어셈블리면 false.
	bool GetManagedGcStats(ScriptGcStats& outStats);

	// ── 노출 필드 ──
	// 소스 제너레이터가 만든 접근자를 통해 인스펙터·직렬화가 값을 주고받는다.
	// 관리 객체의 필드 주소를 직접 잡지 않는 이유는 ScriptCore의 Behaviour 주석 참고.
	enum class ScriptFieldType : int
	{
		Unknown = 0, Float = 1, Int32 = 2, Bool = 3, Float3 = 4, String = 5, Object = 6, Float2 = 7
	};


	// ── 스크립트 어셈블리 핫리로드 ──
	//
	// 게임 스크립트만 언로드 가능한 컨텍스트에 올라간다. 리로드하면 살아 있던 인스턴스가
	// 전부 사라지므로, 호출자가 ScriptComponent들을 다시 깨워 인스턴스를 만들고
	// 저장해 둔 필드 값을 되돌려야 한다(ScriptComponent::Awake가 그 일을 한다).
	bool ReloadScripts();

	// 이전 컨텍스트가 아직 살아 있는가(참조 누수 진단).
	bool IsPreviousContextAlive();

	int  GetFieldCount(int instanceId);
	std::string GetFieldName(int instanceId, int index);
	ScriptFieldType GetFieldType(int instanceId, int index);

	float GetFieldFloat(int instanceId, int index);
	void  SetFieldFloat(int instanceId, int index, float value);
	int   GetFieldInt32(int instanceId, int index);
	void  SetFieldInt32(int instanceId, int index, int value);
	bool  GetFieldBool(int instanceId, int index);
	void  SetFieldBool(int instanceId, int index, bool value);

	std::string GetFieldString(int instanceId, int index);
	void        SetFieldString(int instanceId, int index, const std::string& value);

	ScriptFloat2 GetFieldFloat2(int instanceId, int index);
	void         SetFieldFloat2(int instanceId, int index, ScriptFloat2 value);
	ScriptFloat3 GetFieldFloat3(int instanceId, int index);
	void         SetFieldFloat3(int instanceId, int index, ScriptFloat3 value);

	// 오브젝트 참조. 핸들을 그대로 다루면 실행마다 값이 달라지므로,
	// 저장·복원은 Entity 포인터 단위로 주고받는다.
	Entity* GetFieldObject(int instanceId, int index);
	void        SetFieldObject(int instanceId, int index, Entity* object);

private:
	ClrHost() = default;
	~ClrHost() = default;

	bool LoadHostfxr();
	bool BindEntryPoints(const file::path& assemblyPath);

	bool m_ready{ false };
	int  m_lastActiveCount{ 0 };

	// 관리 진입점 함수 포인터 (UnmanagedCallersOnly라 마샬링 스텁이 없다)
	using InitializeFn      = int(__stdcall*)(void*);
	using ShutdownFn        = int(__stdcall*)();
	using TickFn            = int(__stdcall*)(float);
	using AwakeFn           = int(__stdcall*)();
	using CreateFn          = int(__stdcall*)(ScriptObjectHandle, const char*);
	using DestroyFn         = int(__stdcall*)(int);
	using LifecycleFn       = int(__stdcall*)(int, int);
	using TypeNamesFn       = int(__stdcall*)(char*, int);

	using LoadScriptsFn  = int(__stdcall*)(const char*);
	using ReloadFn       = int(__stdcall*)();
	using FieldCountFn   = int(__stdcall*)(int);
	using FieldNameFn    = int(__stdcall*)(int, int, char*, int);
	using FieldTypeFn    = int(__stdcall*)(int, int);
	using GetFloatFn     = float(__stdcall*)(int, int);
	using SetFloatFn     = void(__stdcall*)(int, int, float);
	using GetIntFn       = int(__stdcall*)(int, int);
	using SetIntFn       = void(__stdcall*)(int, int, int);

	InitializeFn m_fnInitialize{ nullptr };
	ShutdownFn   m_fnShutdown{ nullptr };
	AwakeFn      m_fnAwake{ nullptr };
	TickFn       m_fnPrePhysicsTick{ nullptr };
	TickFn       m_fnPostPhysicsTick{ nullptr };
	AwakeFn      m_fnSceneUnload{ nullptr };
	AwakeFn      m_fnFlushPendingAwake{ nullptr };

	// 관리 힙 제어·계측(9-6·9-7). 선택 바인딩이라 구 어셈블리에서는 nullptr로 남는다.
	// 행동 트리(9-8). 선택 바인딩이라 구 어셈블리에서는 nullptr로 남고 BT만 조용히 꺼진다.
	using CreateBTFn  = int(__stdcall*)(ScriptObjectHandle, const ScriptBTNodeDesc*, int, const ScriptBBEntry*, int);
	using DestroyBTFn = int(__stdcall*)(int);
	CreateBTFn   m_fnCreateBehaviorTree{ nullptr };
	DestroyBTFn  m_fnDestroyBehaviorTree{ nullptr };
	using FlushAITickFn = int(__stdcall*)(const ScriptAITick*, int);
	FlushAITickFn m_fnFlushAITicks{ nullptr };
	using BTTypeNamesFn = int(__stdcall*)(int, char*, int);
	// 다른 절의 별칭(HasAniFn)에 기대지 않는다 — 그쪽이 아래에 선언돼 있어
	// 순서에 묶이고, 한쪽을 옮기면 무관해 보이는 곳이 깨진다.
	using HasBTNodeFn   = int(__stdcall*)(const char*);
	BTTypeNamesFn m_fnGetBTNodeTypeNames{ nullptr };
	HasBTNodeFn   m_fnHasBTNodeType{ nullptr };
	using BTStatsFn = int(__stdcall*)(ScriptBTStats*);
	BTStatsFn    m_fnGetBTStats{ nullptr };
	AwakeFn      m_fnResetBTStats{ nullptr };

	// 경계 계수는 관리 측이 볼 수 없으므로 여기서만 센다(선언은 공개 절 참고).
	AICrossingCounters m_aiCrossings{};

	using GcStatsFn   = int(__stdcall*)(ScriptGcStats*);
	using GcLatencyFn = int(__stdcall*)(int);
	AwakeFn      m_fnGcCollectNow{ nullptr };
	GcLatencyFn  m_fnGcSetLatencyMode{ nullptr };
	GcStatsFn    m_fnGcGetStats{ nullptr };

	using FlushPhysicsFn = int(__stdcall*)(const ScriptPhysicsEvent*, int);
	using HasAniFn       = int(__stdcall*)(const char*);
	using CreateAniFn    = int(__stdcall*)(const char*);
	using DestroyAniFn   = int(__stdcall*)(int);
	using FlushAniFn     = int(__stdcall*)(const ScriptAniEvent*, int);
	using FlushMessageFn = int(__stdcall*)(const ScriptMessage*, int);
	FlushPhysicsFn m_fnFlushPhysicsEvents{ nullptr };
	HasAniFn       m_fnHasAniBehaviour{ nullptr };
	CreateAniFn    m_fnCreateAniBehaviour{ nullptr };
	DestroyAniFn   m_fnDestroyAniBehaviour{ nullptr };
	FlushAniFn     m_fnFlushAniEvents{ nullptr };
	FlushMessageFn m_fnFlushScriptMessages{ nullptr };
	TypeNamesFn    m_fnGetAniBehaviourTypeNames{ nullptr };

	// 한 프레임에 모이는 충돌 이벤트. 매 프레임 clear 하되 용량은 유지한다.
	std::vector<ScriptPhysicsEvent> m_physicsEvents;
	std::vector<ScriptAniEvent> m_aniEvents;

	// AI 틱 큐. 담는 쪽이 잡 스레드라 플래그로 보호한다(위 주석 참고).
	std::vector<ScriptAITick> m_aiTicks;
	std::atomic_flag m_aiTickFlag = ATOMIC_FLAG_INIT;
	std::vector<ScriptMessage> m_scriptMessages;
	std::atomic_flag           m_scriptMessageFlag{};   // 잡 스레드가 함께 담는다
	CreateFn     m_fnCreateBehaviour{ nullptr };
	DestroyFn    m_fnDestroyBehaviour{ nullptr };
	LifecycleFn  m_fnDispatchLifecycle{ nullptr };
	TypeNamesFn  m_fnGetBehaviourTypeNames{ nullptr };

	LoadScriptsFn m_fnLoadScripts{ nullptr };
	ReloadFn      m_fnReloadScripts{ nullptr };
	ReloadFn      m_fnIsPreviousContextAlive{ nullptr };

	FieldCountFn m_fnGetFieldCount{ nullptr };
	FieldNameFn  m_fnGetFieldName{ nullptr };
	FieldTypeFn  m_fnGetFieldType{ nullptr };
	GetFloatFn   m_fnGetFieldFloat{ nullptr };
	SetFloatFn   m_fnSetFieldFloat{ nullptr };
	GetIntFn     m_fnGetFieldInt32{ nullptr };
	SetIntFn     m_fnSetFieldInt32{ nullptr };
	GetIntFn     m_fnGetFieldBool{ nullptr };
	SetIntFn     m_fnSetFieldBool{ nullptr };

	using GetStringFn  = int(__stdcall*)(int, int, char*, int);
	using SetStringFn  = void(__stdcall*)(int, int, const char*);
	using GetFloat2Fn  = ScriptFloat2(__stdcall*)(int, int);
	using SetFloat2Fn  = void(__stdcall*)(int, int, ScriptFloat2);
	using GetFloat3Fn  = ScriptFloat3(__stdcall*)(int, int);
	using SetFloat3Fn  = void(__stdcall*)(int, int, ScriptFloat3);
	using GetObjectFn  = ScriptObjectHandle(__stdcall*)(int, int);
	using SetObjectFn  = void(__stdcall*)(int, int, ScriptObjectHandle);

	GetStringFn m_fnGetFieldString{ nullptr };
	SetStringFn m_fnSetFieldString{ nullptr };
	GetFloat2Fn m_fnGetFieldFloat2{ nullptr };
	SetFloat2Fn m_fnSetFieldFloat2{ nullptr };
	GetFloat3Fn m_fnGetFieldFloat3{ nullptr };
	SetFloat3Fn m_fnSetFieldFloat3{ nullptr };
	GetObjectFn m_fnGetFieldObject{ nullptr };
	SetObjectFn m_fnSetFieldObject{ nullptr };
};
#endif // !DYNAMICCPP_EXPORTS





