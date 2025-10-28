#include "TimeSystem.h"


DirectX11::TimeSystem::TimeSystem() :
	m_elapsedTicks(0),
	m_totalTicks(0),
	m_leftOverTicks(0),
	m_frameCount(0),
	m_framesPerSecond(0),
	m_framesThisSecond(0),
	m_qpcSecondCounter(0),
	m_isFixedTimeStep(false),
	m_targetElapsedTicks(TicksPerSecond / 60),
	m_fixedLeftOverTicks(0)
{
	if (!QueryPerformanceFrequency(&m_qpcFrequency))
	{
		throw std::exception("Failed_QueryPerformanceFrequency");
	}

	if (!QueryPerformanceCounter(&m_qpcLastTime))
	{
		throw std::exception("Failed_QueryPerformanceCounter");
	}

	// NewCreateSceneInitialize max delta to 1/10 of a second.
	m_qpcMaxDelta = m_qpcFrequency.QuadPart / 10;
}

void DirectX11::TimeSystem::UpdateTimeScale(float _timeDelta)
{
	if (changeTimeScaleTime > 0.f)
	{
		changeTimeScaleTime -= _timeDelta;
		if (changeTimeScaleTime <= 0.f)
		{
			changeTimeScaleTime = 0.f;
			timeScale = orginTimeScale;
		}
	}
}

void DirectX11::TimeSystem::SetTimeScale(float _timeScale, float _changeTimeScaleTime)
{
	timeScale = _timeScale;
	changeTimeScaleTime = _changeTimeScaleTime;
}

void DirectX11::TimeSystem::SetTimeScale(float _timeScale)
{
	timeScale = _timeScale;
}
