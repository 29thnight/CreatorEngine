#pragma once
#include <algorithm>
#include <atomic>
#include <vector>
#include <cinttypes>
#include <mutex>
#include <array>
#include <span>
#include <unordered_map>
#include <assert.h>

// 접두사 없는 매크로를 헤더에서 뿌리면 이 헤더를 include한 모든 TU의 전역 이름을
// 오염시킨다. 실제로 `check`가 PhysX(PxHashInternals.h)와 fmt(ranges.h·std.h)의
// 멤버 함수 `check`를 치환해 ASan 구성(유니티 빌드 off)에서 컴파일이 멈췄다.
// 이 헤더는 17개 파일이 include한다 — 접두사를 붙여 격리한다.
//
// assert는 NDEBUG에서 통째로 사라지므로 부작용이 있는 식을 넣지 말 것.
// 값을 먼저 계산해 변수에 담고 그 변수를 검사한다(Profiler.cpp의 사용례 참고).
#define PROFILER_CHECK(op, ...) assert(op)
#define PROFILER_VERIFY_HR(op) assert(SUCCEEDED(op))

#define _STRINGIFY(a) #a
#define STRINGIFY(a) _STRINGIFY(a)
#define CONCAT_IMPL( x, y ) x##y
#define MACRO_CONCAT( x, y ) CONCAT_IMPL( x, y )

using uint64 = uint64_t;
using uint32 = uint32_t;
using uint16 = uint16_t;
template<typename T>
using Span = std::span<T>;

struct URange
{
	uint32 Begin;
	uint32 End;
};


/*
	General
*/
#define PROFILER_INITIALIZE(size_T, size)			gCPUProfiler.Initialize(size_T, size)
#define PROFILER_SHUTDOWN()							gCPUProfiler.Shutdown()

// Usage:
//		PROFILE_REGISTER_THREAD(const char* pName)
//		PROFILE_REGISTER_THREAD()
#define PROFILE_REGISTER_THREAD(...)				gCPUProfiler.RegisterThread(__VA_ARGS__)

/// Usage:
//		PROFILE_FRAME()
#define PROFILE_FRAME()								gCPUProfiler.Tick()

/*
	CPU Profiling
*/

// Usage:
//		PROFILE_CPU_SCOPE(const char* pName)
//		PROFILE_CPU_SCOPE()
#define PROFILE_CPU_SCOPE(...)						CPUProfileScope MACRO_CONCAT(profiler, __COUNTER__)(__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

// Usage:
//		PROFILE_CPU_BEGIN(const char* pName)
//		PROFILE_CPU_BEGIN()
#define PROFILE_CPU_BEGIN(...)						gCPUProfiler.BeginEvent(__VA_ARGS__)
// Usage:
//		PROFILE_CPU_END()
#define PROFILE_CPU_END()							gCPUProfiler.EndEvent()

// Simple Linear Allocator
class LinearAllocator
{
public:
	explicit LinearAllocator(uint32 size)
		: m_pData(new char[size]), m_Size(size), m_Offset(0)
	{
	}

	~LinearAllocator()
	{
		delete[] m_pData;
	}

	LinearAllocator(LinearAllocator&) = delete;
	LinearAllocator& operator=(LinearAllocator&) = delete;

	void Reset()
	{
		m_Offset = 0;
		m_Overflowed = false;
	}

	template<typename T, typename... Args>
	T* Allocate(Args... args)
	{
		void* pData = Allocate(sizeof(T));
		if (!pData)
			return nullptr;
		T* pValue = new (pData) T(std::forward<Args>(args)...);
		return pValue;
	}

	// 예산을 넘으면 nullptr을 돌려준다. 예전에는 assert만 하고 m_pData + offset을
	// 그대로 반환했다 — NDEBUG(Release·GameBuild)에서는 assert가 사라지므로
	// 프레임 문자열 예산(16KB)을 넘긴 순간 힙 밖에 썼다.
	void* Allocate(uint32 size)
	{
		const uint32 offset = m_Offset.fetch_add(size);
		if (offset > m_Size || size > m_Size - offset)
		{
			m_Overflowed = true;
			return nullptr;
		}
		return m_pData + offset;
	}

	// 예산 소진 시 nullptr. 대체 이름을 정하는 것은 호출자의 몫이다.
	const char* String(const char* pStr)
	{
		const uint32 len = (uint32)strlen(pStr) + 1;
		char* pData = (char*)Allocate(len);
		if (!pData)
			return nullptr;
		strcpy_s(pData, len, pStr);
		return pData;
	}

	uint32 GetUsedBytes() const
	{
		const uint32 offset = m_Offset.load();
		return offset < m_Size ? offset : m_Size;
	}
	uint32 GetCapacityBytes() const { return m_Size; }
	bool HasOverflowed() const { return m_Overflowed.load(); }

private:
	char* m_pData;
	uint32 m_Size;
	std::atomic<uint32> m_Offset;
	std::atomic<bool> m_Overflowed{ false };
};

// 표시(HUD)는 이 코어의 소관이 아니다 — DrawProfilerHUD 선언은
// ImGuiHelper/ProfilerHUD.h로 분리됐다(P1a). 이 헤더는 수집·보관만 안다.

//-----------------------------------------------------------------------------
// [SECTION] CPU Profiler
//-----------------------------------------------------------------------------

// Global CPU Profiler
extern class CPUProfiler gCPUProfiler;

struct CPUProfilerCallbacks
{
	using EventBeginFn = void(*)(const char* /*pName*/, void* /*pUserData*/);
	using EventEndFn = void(*)(void* /*pUserData*/);

	EventBeginFn	OnEventBegin = nullptr;
	EventEndFn		OnEventEnd = nullptr;
	void* pUserData = nullptr;
};

// CPU Profiler
// Also responsible for updating GPU profiler
// Also responsible for drawing HUD
class CPUProfiler
{
	int ccccc = 0;
public:
	void Initialize(uint32 historySize, uint32 maxEvents);
	void Shutdown();

	// Start and push an event on the current thread
	void BeginEvent(const char* pName, const char* pFilePath = nullptr, uint32 lineNumber = 0);

	// End and pop the last pushed event on the current thread
	void EndEvent();

	// Resolve the last frame and advance to the next frame.
	// Call at the START of the frame.
	void Tick();

	// Initialize a thread with an optional name
	void RegisterThread(const char* pName = nullptr);

	// 현재 스레드의 등록을 해제한다. 등록한 스레드가 이것 없이 종료하면
	// ThreadData가 죽은 thread_local을 계속 가리키고, 이후 모든 Tick()이
	// 해제된 저장소를 읽는다(수명이 짧은 워커·임시 스레드에서 실제로 터진다).
	// 슬롯은 남기고 pTLS만 끊는다 — 이벤트에 박힌 ThreadIndex가 위치이므로
	// 슬롯을 지우면 이미 수집된 프레임의 스레드 귀속이 어긋난다.
	void UnregisterThread();

	// 프로파일러 자신의 비용과 용량 소진을 드러내는 계측.
	// 관측 도구가 관측 대상을 얼마나 밀어내는지 모르면 그 수치는 못 믿는다.
	struct Stats
	{
		uint64 LastTickTicks = 0;		// 마지막 Tick()의 소요(QPC 틱)
		uint64 PeakTickTicks = 0;
		uint64 TotalTickTicks = 0;
		uint64 TickCount = 0;

		uint32 LastFrameEvents = 0;		// 마지막 프레임에 수집한 이벤트 수
		uint32 PeakFrameEvents = 0;
		uint32 EventCapacity = 0;		// 프레임당 상한(Initialize의 maxEvents)

		uint32 LastFrameNameBytes = 0;	// 마지막 프레임의 이름 문자열 소비
		uint32 PeakFrameNameBytes = 0;
		uint32 NameCapacity = 0;		// EventData::ALLOCATOR_SIZE

		uint64 TotalDroppedEvents = 0;
		uint64 TotalDroppedNames = 0;
		uint32 RetiredThreads = 0;		// 현재 끊겨 있는 슬롯 수
		uint32 MalformedScopes = 0;		// 스코프가 열린 채 은퇴한 건수(Begin/End 불균형)
	};

	// 게임 스레드(Tick 호출자)가 쓰고 UI/CLI가 읽는다. 원자적 스냅샷이 아니라
	// 표시용 근사치다 — 판정에 쓰는 값은 Tick 경계에서 읽을 것.
	const Stats& GetStats() const { return m_Stats; }
	void ResetStats() { m_Stats = Stats{}; m_Stats.EventCapacity = m_EventCapacity; m_Stats.NameCapacity = EventData::ALLOCATOR_SIZE; }

	// QPC 주파수(틱/초). 틱을 시간으로 바꾸는 유일한 기준.
	static uint64 GetTicksPerSecond();

	// Struct containing all sampling data of a single frame
	struct EventData
	{
		static constexpr uint32 ALLOCATOR_SIZE = 1 << 14;

		EventData()
			: Allocator(ALLOCATOR_SIZE)
		{
		}

		// Structure representating a single event
		struct Event
		{
			const char* pName = "";		// Name of the event
			const char* pFilePath = nullptr;	// File path of file in which this event is recorded
			uint64		TicksBegin = 0;		// The ticks at the start of this event
			uint64		TicksEnd = 0;		// The ticks at the end of this event
			uint32		LineNumber : 16;		// Line number of file in which this event is recorded
			uint32		ThreadIndex : 11;		// Thread Index of the thread that recorderd this event
			uint32		Depth : 10;		// Depth of the event
		};

		std::vector<Span<const Event>>	EventsPerThread;	// Events per thread of the frame
		std::vector<Event>				Events;				// All events of the frame
		LinearAllocator					Allocator;			// Scratch allocator storing all dynamic allocations of the frame
		std::atomic<uint32>				NumEvents = 0;		// The number of events

		// 용량을 넘겨 버린 양. 누락을 0으로 가장하지 않는다 — 프레임이 정상인 척
		// 하면 그 프레임을 근거로 내리는 판단이 전부 틀어진다.
		std::atomic<uint32>				DroppedEvents = 0;	// Events.size()를 넘겨 버린 이벤트 수
		std::atomic<uint32>				DroppedNames = 0;	// 문자열 예산 소진으로 이름을 잃은 이벤트 수
	};

	// Thread-local storage to keep track of current depth and event stack
	struct TLS
	{
		static constexpr int MAX_STACK_DEPTH = 32;
		static constexpr int EVENT_BUFFER_SIZE = 1024;

		template<typename T, uint32 N>
		struct FixedStack
		{
		public:
			T& Pop()
			{
				PROFILER_CHECK(Depth > 0);
				--Depth;
				return StackData[Depth];
			}

			T& Push()
			{
				Depth++;
				PROFILER_CHECK(Depth < N);
				return StackData[Depth - 1];
			}

			T& Top()
			{
				PROFILER_CHECK(Depth > 0);
				return StackData[Depth - 1];
			}

			uint32 GetSize() const { return Depth; }

			// 스레드가 은퇴·재등록할 때 깊이를 되돌린다. 이것 없이 어긋난 깊이가
			// 남으면 다음 등록에서 그대로 이어져 누적되고, 32를 넘는 순간
			// Push()가 StackData 밖(=TLS의 다음 멤버)을 가리킨다.
			// NDEBUG에서는 PROFILER_CHECK도 사라지므로 조용히 힙을 망가뜨린다.
			void Reset() { Depth = 0; }

		private:
			uint32 Depth = 0;
			T StackData[N]{};
		};


		FixedStack<uint32, MAX_STACK_DEPTH> EventStack;
		uint32								ThreadIndex = 0;
		bool								IsInitialized = false;

		std::vector<EventData::Event> EventBuffer;
		std::atomic<uint32> NumEvents = 0;
	};

	// Structure describing a registered thread
	struct ThreadData
	{
		char		Name[128]{};
		uint32		ThreadID = 0;
		uint32		Index = 0;
		const TLS* pTLS = nullptr;

		// 은퇴한 프레임. 슬롯을 곧바로 되쓰면 히스토리에 아직 남아 있는 옛 주인의
		// 이벤트가 새 주인의 이름으로 보인다 — 누가 무엇을 했는지가 목적인 도구에서
		// 그건 크래시보다 나쁘다. 히스토리가 한 바퀴 돈 뒤에만 재사용한다.
		uint32		RetiredAtFrame = 0;
	};

	URange GetFrameRange() const
	{
		const uint32 current = m_FrameIndex.load();
		uint32 begin = current - (((current) < (m_HistorySize)) ? (current) : (m_HistorySize)) + 1;
		uint32 end = current;
		return URange(begin, end);
	}

	Span<const EventData::Event> GetEventsForThread(const ThreadData& thread, uint32 frame) const
	{
		PROFILER_CHECK(frame >= GetFrameRange().Begin && frame < GetFrameRange().End);
		const EventData& data = m_pEventData[frame % m_HistorySize];
		if (thread.Index < data.EventsPerThread.size())
			return data.EventsPerThread[thread.Index];
		return {};
	}

	// Get the ticks range of the history
	void GetHistoryRange(uint64& ticksMin, uint64& ticksMax) const
	{
		URange range = GetFrameRange();
		ticksMin = GetData(range.Begin).Events[0].TicksBegin;
		ticksMax = GetData(range.End).Events[0].TicksEnd;
	}

	Span<const ThreadData> GetThreads() const { return m_ThreadData; }

	void SetEventCallback(const CPUProfilerCallbacks& inCallbacks) { m_EventCallback = inCallbacks; }
	void SetPaused(bool paused) { m_QueuedPaused = paused; }
	bool IsPaused() const { return m_Paused; }

private:
	// Retrieve thread-local storage without initialization
	static TLS& GetTLSUnsafe()
	{
		static thread_local TLS tls;
		return tls;
	}

	// Retrieve the thread-local storage
	TLS& GetTLS()
	{
		TLS& tls = GetTLSUnsafe();
		if (!tls.IsInitialized)
			RegisterThread();
		return tls;
	}

	// Return the sample data of the current frame
	EventData& GetData() { return GetData(m_FrameIndex.load()); }
	EventData& GetData(uint32 frameIndex) { return m_pEventData[frameIndex % m_HistorySize]; }
	const EventData& GetData(uint32 frameIndex)	const { return m_pEventData[frameIndex % m_HistorySize]; }

	CPUProfilerCallbacks m_EventCallback;

	std::mutex				m_ThreadDataLock;				// Mutex for accesing thread data
	std::vector<ThreadData> m_ThreadData;					// Data describing each registered thread

	EventData* m_pEventData = nullptr;	// Per-frame data
	uint32					m_HistorySize = 0;		// History size
	uint32					m_EventCapacity = 0;	// 프레임당 이벤트 상한(Initialize의 maxEvents)
	// 게임 스레드가 Tick()에서 늘리고, UI(GetFrameRange)와 워커(RegisterThread의
	// 슬롯 재사용 판정)가 읽는다. 평범한 uint32로 두면 그 자체로 데이터 레이스다.
	std::atomic<uint32>		m_FrameIndex{ 0 };		// The current frame index

	// 게임 스레드가 Tick()에서 쓰고 워커가 Begin/EndEvent에서 읽는다. 평범한 bool로
	// 두면 그 자체로 데이터 레이스(UB)다 — 값이 찢기지 않는 플랫폼이라도 컴파일러가
	// 재배치·캐싱할 자유가 있다.
	std::atomic<bool>		m_Paused{ false };		// The current pause state
	std::atomic<bool>		m_QueuedPaused{ false };// The queued pause state

	// 오직 게임 스레드(Tick 호출자)만 쓴다. 다른 스레드가 늘려야 하는 값은
	// 아래 원자 변수로 받아 Tick()이 여기로 옮긴다 — 그래야 기록자가 하나로 남는다.
	Stats					m_Stats;

	// 워커 스레드가 은퇴하며 늘린다(UnregisterThread).
	std::atomic<uint32>		m_MalformedScopes{ 0 };
};

// Helper RAII-style structure to push and pop a CPU sample region
struct CPUProfileScope
{
	CPUProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber, const char* pName)
	{
		gCPUProfiler.BeginEvent(pName, pFilePath, lineNumber);
	}

	CPUProfileScope(const char* pFunctionName, const char* pFilePath, uint32 lineNumber)
	{
		gCPUProfiler.BeginEvent(pFunctionName, pFilePath, lineNumber);
	}

	~CPUProfileScope()
	{
		gCPUProfiler.EndEvent();
	}

	CPUProfileScope(const CPUProfileScope&) = delete;
	CPUProfileScope& operator=(const CPUProfileScope&) = delete;
};

