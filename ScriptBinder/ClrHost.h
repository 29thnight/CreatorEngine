#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "SpinLock.h"
#include "ScriptObjectRegistry.h"

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
	void TickFixedUpdate(float deltaTime);
	void TickUpdate(float deltaTime);
	void TickLateUpdate(float deltaTime);
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
		GameObject* other, const std::vector<Mathf::Vector3>& contactPoints);

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
	void QueueAniEvent(int instanceId, AniEventKind kind, float deltaTime, GameObject* owner);
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

	// ── 인스턴스 ──
	// 성공하면 0 이상의 인스턴스 id, 실패하면 음수.
	int CreateBehaviour(GameObject* owner, std::string_view typeName);
	bool DestroyBehaviour(int instanceId);

	// 등록된 스크립트 타입 이름 목록 — 에디터의 컴포넌트 추가 메뉴용.
	// 선택 바인딩이라 구 어셈블리에서는 빈 목록을 돌려줄 수 있다.
	std::vector<std::string> GetBehaviourTypeNames();


	// 마지막 틱에서 관리 측이 보고한 활성 스크립트 수(경계 로그·진단용).
	int LastActiveCount() const { return m_lastActiveCount; }

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
	// 저장·복원은 GameObject 포인터 단위로 주고받는다.
	GameObject* GetFieldObject(int instanceId, int index);
	void        SetFieldObject(int instanceId, int index, GameObject* object);

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
	TickFn       m_fnFixedUpdate{ nullptr };
	TickFn       m_fnUpdate{ nullptr };
	TickFn       m_fnLateUpdate{ nullptr };
	AwakeFn      m_fnSceneUnload{ nullptr };
	AwakeFn      m_fnFlushPendingAwake{ nullptr };

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
	std::vector<ScriptMessage> m_scriptMessages;
	std::atomic_flag           m_scriptMessageFlag{};   // 잡 스레드가 함께 담는다
	CreateFn     m_fnCreateBehaviour{ nullptr };
	DestroyFn    m_fnDestroyBehaviour{ nullptr };
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





