#ifndef BUILD_FLAG
#include "Profiler.h"

CPUProfiler gCPUProfiler;

//-----------------------------------------------------------------------------
// [SECTION] CPU Profiler
//-----------------------------------------------------------------------------


void CPUProfiler::Initialize(uint32 historySize, uint32 maxEvents)
{
	Shutdown();

	m_pEventData = new EventData[historySize];
	m_HistorySize = historySize;

	for (uint32 i = 0; i < historySize; ++i)
		m_pEventData[i].Events.resize(maxEvents);
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
	m_Paused = m_QueuedPaused;
	if (m_Paused)
		return;

	if (m_FrameIndex)
		EndEvent();

	EventData& frame = GetData();
	frame.NumEvents = 0;

	for (auto& threadData : m_ThreadData)
	{
		TLS* pTLS = const_cast<TLS*>(threadData.pTLS);
		for (uint32 i = 0; i < pTLS->NumEvents; ++i)
		{
			EventData::Event& event = pTLS->EventBuffer[i];
			if (event.TicksEnd > 0)
			{
				uint32 newIndex = frame.NumEvents.fetch_add(1);
				PROFILER_CHECK(newIndex < frame.Events.size());
				EventData::Event& newEvent = frame.Events[newIndex];
				newEvent = event;
				newEvent.pName = frame.Allocator.String(event.pName);
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
		while (eventRange.Begin < frame.NumEvents && threadIndex < events[eventRange.Begin].ThreadIndex)
			eventRange.Begin++;
		eventRange.End = eventRange.Begin;
		while (eventRange.End < frame.NumEvents && events[eventRange.End].ThreadIndex == threadIndex)
			++eventRange.End;

		frame.EventsPerThread[threadIndex] = Span<const EventData::Event>(&events[eventRange.Begin], eventRange.End - eventRange.Begin);
		eventRange.Begin = eventRange.End;
	}

	++m_FrameIndex;

	EventData& newData = GetData();
	newData.Allocator.Reset();
	newData.NumEvents = 0;

	BeginEvent("CPU Frame");
}


void CPUProfiler::RegisterThread(const char* pName)
{
	TLS& tls = GetTLSUnsafe();
	PROFILER_CHECK(!tls.IsInitialized);
	tls.IsInitialized = true;
	std::scoped_lock lock(m_ThreadDataLock);
	tls.ThreadIndex = (uint32)m_ThreadData.size();
	ThreadData& data = m_ThreadData.emplace_back();

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
	data.Index = (uint32)m_ThreadData.size() - 1;

	for (uint32 i = 0; i < m_HistorySize; ++i)
		m_pEventData[i].EventsPerThread.resize(m_ThreadData.size());
}

#endif