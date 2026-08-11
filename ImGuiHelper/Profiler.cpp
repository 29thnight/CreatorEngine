#ifndef BUILD_FLAG
#include "Profiler.h"

CPUProfiler gCPUProfiler;

//-----------------------------------------------------------------------------
// [SECTION] CPU Profiler
//-----------------------------------------------------------------------------


// 프레임 이름 예산이 바닥났을 때 대신 꽂는 이름. 이벤트 자체는 살리고 이름만
// 잃는다 — 표시기가 빈 포인터를 만나 죽는 것보다 낫고, 무엇을 잃었는지도 보인다.
static const char kProfilerNameBudgetExhausted[] = "<name-budget-exhausted>";

uint64 CPUProfiler::GetTicksPerSecond()
{
	static const uint64 frequency = []
	{
		uint64 value = 0;
		QueryPerformanceFrequency((LARGE_INTEGER*)(&value));
		return value ? value : 1;
	}();
	return frequency;
}


void CPUProfiler::Initialize(uint32 historySize, uint32 maxEvents)
{
	Shutdown();

	m_pEventData = new EventData[historySize];
	m_HistorySize = historySize;
	m_EventCapacity = maxEvents;

	for (uint32 i = 0; i < historySize; ++i)
		m_pEventData[i].Events.resize(maxEvents);

	ResetStats();
}


void CPUProfiler::Shutdown()
{
	delete[] m_pEventData;
}


void CPUProfiler::BeginEvent(const char* pName, const char* pFilePath, uint32 lineNumber)
{
	ccccc++;
	if (m_EventCallback.OnEventBegin)
		m_EventCallback.OnEventBegin(pName, m_EventCallback.pUserData);

	if (m_Paused)
		return;

	TLS& tls = GetTLS();
	uint32 newIndex = tls.NumEvents.fetch_add(1);
	if (newIndex >= tls.EventBuffer.size())
	{
		tls.EventBuffer.resize(newIndex + 1);
	}

	EventData::Event& newEvent = tls.EventBuffer[newIndex];
	newEvent.Depth = tls.EventStack.GetSize();
	newEvent.ThreadIndex = tls.ThreadIndex;
	newEvent.pName = pName;
	newEvent.pFilePath = pFilePath;
	newEvent.LineNumber = lineNumber;
	QueryPerformanceCounter((LARGE_INTEGER*)(&newEvent.TicksBegin));

	tls.EventStack.Push() = newIndex;
}


// End and pop the last pushed event on the current thread
void CPUProfiler::EndEvent()
{
	ccccc--;
	if (m_EventCallback.OnEventEnd)
		m_EventCallback.OnEventEnd(m_EventCallback.pUserData);

	if (m_Paused)
		return;

	TLS& tls = GetTLS();
	EventData::Event& event = tls.EventBuffer[tls.EventStack.Pop()];
	QueryPerformanceCounter((LARGE_INTEGER*)(&event.TicksEnd));
}


void CPUProfiler::Tick()
{
	m_Paused.store(m_QueuedPaused.load());
	if (m_Paused.load())
		return;

	uint64 tickBeginTicks = 0;
	QueryPerformanceCounter((LARGE_INTEGER*)(&tickBeginTicks));

	if (m_FrameIndex.load())
		EndEvent();

	EventData& frame = GetData();
	frame.NumEvents = 0;
	frame.DroppedEvents = 0;
	frame.DroppedNames = 0;

	const uint32 eventCapacity = (uint32)frame.Events.size();

	// 이 락이 지키는 것은 **스레드 표(m_ThreadData) 하나뿐**이다. 락 없이 돌던
	// 시절엔 등록이 겹치면 emplace_back의 재할당이 이 루프의 이터레이터를
	// 무효화했고, 은퇴가 겹치면 방금 읽은 pTLS가 널이 됐다.
	//
	// ★ 아직 안 지키는 것: 워커의 EventBuffer 자체다. 아래에서 pTLS->EventBuffer[i]를
	//   읽는 동안 그 워커가 BeginEvent에서 resize를 돌리면 옛 버퍼가 해제된다.
	//   지금 이것이 터지지 않는 이유는 프로파일러의 계약이 아니라 **호출부의
	//   배리어 설계** 때문이다 — 등록된 CB/CE는 PROFILE_FRAME 시점에 렌더 배리어에
	//   묶여 있다. 배리어 밖에서 등록되는 스레드가 생기면 즉시 재현된다.
	//   producer/collector 소유권 계약은 P2(sealed chunk handoff)의 몫이다.
	//   ProfilingCapturePlan.md §3.1.
	//
	// 락 범위에 BeginEvent/EndEvent를 넣으면 안 된다 — 그쪽이 미등록 스레드를
	// 만나면 RegisterThread로 같은 뮤텍스를 다시 잡아 교착한다.
	{
	std::scoped_lock lock(m_ThreadDataLock);

	for (auto& threadData : m_ThreadData)
	{
		TLS* pTLS = const_cast<TLS*>(threadData.pTLS);

		// 은퇴한 스레드. 슬롯은 이름 표시를 위해 남지만 저장소는 이미 없다.
		if (!pTLS)
			continue;

		const uint32 numEvents = pTLS->NumEvents.load();
		for (uint32 i = 0; i < numEvents; ++i)
		{
			EventData::Event& event = pTLS->EventBuffer[i];
			if (event.TicksEnd > 0)
			{
				uint32 newIndex = frame.NumEvents.fetch_add(1);
				if (newIndex >= eventCapacity)
				{
					// 상한을 넘겼다. 예전에는 assert만 하고 그대로 기록해
					// NDEBUG 빌드에서 벡터 밖에 썼다. 이제는 세고 버린다.
					frame.NumEvents.store(eventCapacity);
					frame.DroppedEvents.fetch_add(1);
					continue;
				}

				EventData::Event& newEvent = frame.Events[newIndex];
				newEvent = event;
				if (const char* pStored = frame.Allocator.String(event.pName))
				{
					newEvent.pName = pStored;
				}
				else
				{
					newEvent.pName = kProfilerNameBudgetExhausted;
					frame.DroppedNames.fetch_add(1);
				}
			}
		}
		pTLS->NumEvents = 0;
	}

	// Sort the events by thread and group by thread
	std::vector<EventData::Event>& events = frame.Events;
	std::sort(events.begin(), events.begin() + frame.NumEvents, [](const EventData::Event& a, const EventData::Event& b)
		{
			return a.ThreadIndex < b.ThreadIndex;
		});

	URange eventRange(0, 0);
	for (uint32 threadIndex = 0; threadIndex < (uint32)m_ThreadData.size(); ++threadIndex)
	{
		// 앞선 스레드의 이벤트만 지나친다. 예전에는 부등호가 반대라
		// (threadIndex < events[Begin].ThreadIndex) 이번 프레임에 이벤트가 하나도
		// 없는 스레드를 만나면 남은 이벤트 전부가 조건을 만족해 커서가 끝까지
		// 밀렸고, 그 뒤 스레드의 스팬이 전부 비었다 — 한 스레드가 쉬면 그보다
		// 인덱스가 큰 스레드가 통째로 타임라인에서 사라졌다.
		while (eventRange.Begin < frame.NumEvents && events[eventRange.Begin].ThreadIndex < threadIndex)
			eventRange.Begin++;
		eventRange.End = eventRange.Begin;
		while (eventRange.End < frame.NumEvents && events[eventRange.End].ThreadIndex == threadIndex)
			++eventRange.End;

		// data() + offset으로 잡는다. 프레임이 상한까지 찼을 때 Begin이 size()가
		// 되는데, 그때 &events[size()]는 정의되지 않은 접근이다(디버그 이터레이터가
		// 잡는다). 끝 다음 포인터는 data() 산술로만 만든다.
		frame.EventsPerThread[threadIndex] = Span<const EventData::Event>(events.data() + eventRange.Begin, eventRange.End - eventRange.Begin);
		eventRange.Begin = eventRange.End;
	}

	// 끊긴 슬롯 수는 파생값이라 여기서 센다. 은퇴는 워커가 하지만 통계의
	// 기록자는 게임 스레드 하나로 유지한다 — 그래야 찢긴 읽기가 생기지 않는다.
	uint32 retired = 0;
	for (const ThreadData& threadData : m_ThreadData)
	{
		if (nullptr == threadData.pTLS)
			++retired;
	}
	m_Stats.RetiredThreads = retired;

	} // m_ThreadDataLock

	// 방금 닫은 프레임의 소비량을 기록한다. 다음 프레임 슬롯을 리셋하기 전에.
	const uint32 collectedEvents = frame.NumEvents.load();
	const uint32 collectedNameBytes = frame.Allocator.GetUsedBytes();

	++m_FrameIndex;

	EventData& newData = GetData();
	newData.Allocator.Reset();
	newData.NumEvents = 0;
	newData.DroppedEvents = 0;
	newData.DroppedNames = 0;

	uint64 tickEndTicks = 0;
	QueryPerformanceCounter((LARGE_INTEGER*)(&tickEndTicks));
	const uint64 elapsed = (tickEndTicks > tickBeginTicks) ? (tickEndTicks - tickBeginTicks) : 0;

	m_Stats.LastTickTicks = elapsed;
	m_Stats.TotalTickTicks += elapsed;
	++m_Stats.TickCount;
	if (elapsed > m_Stats.PeakTickTicks)
		m_Stats.PeakTickTicks = elapsed;

	m_Stats.LastFrameEvents = collectedEvents;
	if (collectedEvents > m_Stats.PeakFrameEvents)
		m_Stats.PeakFrameEvents = collectedEvents;

	m_Stats.LastFrameNameBytes = collectedNameBytes;
	if (collectedNameBytes > m_Stats.PeakFrameNameBytes)
		m_Stats.PeakFrameNameBytes = collectedNameBytes;

	m_Stats.EventCapacity = eventCapacity;
	m_Stats.NameCapacity = frame.Allocator.GetCapacityBytes();
	m_Stats.TotalDroppedEvents += frame.DroppedEvents.load();
	m_Stats.TotalDroppedNames += frame.DroppedNames.load();
	m_Stats.MalformedScopes = m_MalformedScopes.load();

	// 아래 BeginEvent는 다음 프레임의 최상위 스코프라 이 측정에 넣지 않는다.
	BeginEvent("CPU Frame");
}


void CPUProfiler::UnregisterThread()
{
	TLS& tls = GetTLSUnsafe();
	if (!tls.IsInitialized)
		return;

	// 스코프가 열린 채 은퇴한다는 것은 Begin/End 짝이 맞지 않았다는 뜻이다.
	// (일시정지 경계에서 EndEvent가 통째로 건너뛰어지면 이렇게 된다.)
	// 깊이를 되돌리지 않으면 이 TLS가 재등록될 때 어긋남이 그대로 이어져
	// 누적되고, 32를 넘는 순간 FixedStack이 TLS의 다음 멤버를 덮어쓴다.
	const uint32 openScopes = tls.EventStack.GetSize();
	if (openScopes > 0)
	{
		m_MalformedScopes.fetch_add(openScopes);
	}
	tls.EventStack.Reset();

	std::scoped_lock lock(m_ThreadDataLock);

	// 아직 수집되지 않은 이벤트는 여기서 버려진다. 수집은 Tick()이 하고
	// Tick()은 게임 스레드가 부르므로, 워커가 스스로 넘길 방법이 없다.
	// 남기고 싶으면 은퇴 전에 게임 스레드가 한 번 Tick()을 돌아야 한다.
	if (tls.ThreadIndex < m_ThreadData.size() && m_ThreadData[tls.ThreadIndex].pTLS == &tls)
	{
		m_ThreadData[tls.ThreadIndex].pTLS = nullptr;
		m_ThreadData[tls.ThreadIndex].RetiredAtFrame = m_FrameIndex.load();
	}

	tls.IsInitialized = false;
	tls.NumEvents = 0;
}


void CPUProfiler::RegisterThread(const char* pName)
{
	TLS& tls = GetTLSUnsafe();
	PROFILER_CHECK(!tls.IsInitialized);
	tls.IsInitialized = true;
	std::scoped_lock lock(m_ThreadDataLock);

	// 은퇴한 슬롯이 있으면 되쓴다. 매번 새 슬롯을 붙이면 스레드를 자주 세웠다
	// 접는 프로세스에서 표가 끝없이 자라고, ThreadIndex(11비트)도 2047에서 넘친다.
	//
	// 단, 히스토리가 한 바퀴 돌기 전에는 되쓰지 않는다. 조회는 ThreadIndex로만
	// 하므로(GetEventsForThread), 곧바로 되쓰면 아직 보존 중인 프레임에 남은
	// 옛 주인의 이벤트가 새 주인의 이름으로 보인다 — 귀속이 목적인 도구에서
	// 그 오염은 크래시보다 나쁘다.
	uint32 slot = (uint32)m_ThreadData.size();
	for (uint32 i = 0; i < (uint32)m_ThreadData.size(); ++i)
	{
		if (nullptr != m_ThreadData[i].pTLS)
			continue;

		if (m_FrameIndex.load() - m_ThreadData[i].RetiredAtFrame < m_HistorySize)
			continue;

		slot = i;
		break;
	}

	const bool isNewSlot = (slot == (uint32)m_ThreadData.size());
	if (isNewSlot)
	{
		m_ThreadData.emplace_back();
	}

	tls.ThreadIndex = slot;
	ThreadData& data = m_ThreadData[slot];
	data = ThreadData{};

	// 재사용되는 TLS(스레드 풀·OS 스레드 재사용)가 어긋난 깊이를 물고 오지 않게 한다.
	tls.EventStack.Reset();
	tls.NumEvents = 0;

	// If the name is not provided, retrieve it using GetThreadDescription()
	if (pName)
	{
		strcpy_s(data.Name, ARRAYSIZE(data.Name), pName);
	}
	else
	{
		// 호출을 검사 매크로 안에 두면 NDEBUG 빌드에서 통째로 사라진다 —
		// 이름을 못 받아 스레드 이름이 빈 채로 남는다. 값을 먼저 받고 그 값을 검사한다.
		PWSTR pDescription = nullptr;
		const HRESULT hr = ::GetThreadDescription(GetCurrentThread(), &pDescription);
		PROFILER_VERIFY_HR(hr);

		if (SUCCEEDED(hr) && nullptr != pDescription)
		{
			size_t converted = 0;
			const errno_t rc = wcstombs_s(&converted, data.Name,
				ARRAYSIZE(data.Name), pDescription, ARRAYSIZE(data.Name) - 1);
			PROFILER_CHECK(rc == 0);

			// GetThreadDescription이 성공하면 호출자가 해제해야 한다(문서 규약).
			::LocalFree(pDescription);
		}
	}
	data.ThreadID = GetCurrentThreadId();
	data.pTLS = &tls;
	data.Index = slot;	// 재사용 슬롯이면 size()-1이 아니다

	for (uint32 i = 0; i < m_HistorySize; ++i)
		m_pEventData[i].EventsPerThread.resize(m_ThreadData.size());
}

#endif