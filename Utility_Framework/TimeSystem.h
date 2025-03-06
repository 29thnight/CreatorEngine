#pragma once
#include <wrl.h>
#include <exception>
#include "TypeDefinition.h"

namespace DirectX11
{
	class TimeSystem
	{
	public:
		TimeSystem() :
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

			// Initialize max delta to 1/10 of a second.
			m_qpcMaxDelta = m_qpcFrequency.QuadPart / 10;
		}

		// Get elapsed time since the previous Update call.
		uint64_t GetElapsedTicks() const { return m_elapsedTicks; }
		double GetElapsedSeconds() const { return TicksToSeconds(m_elapsedTicks); }

		// Get total time since the start of the program.
		uint64_t GetTotalTicks() const { return m_totalTicks; }
		double GetTotalSeconds() const { return TicksToSeconds(m_totalTicks); }

		// Get total number of updates since start of the program.
		uint32_t GetFrameCount() const { return m_frameCount; }

		// Get the current framerate.
		uint32_t GetFramesPerSecond() const { return m_framesPerSecond; }

		// Set whether to use fixed or variable timestep mode.
		void SetFixedTimeStep(bool isFixedTimestep) { m_isFixedTimeStep = isFixedTimestep; }

		// Set how often to call Update when in fixed timestep mode.
		void SetTargetElapsedTicks(uint64_t targetElapsed) { m_targetElapsedTicks = targetElapsed; }
		void SetTargetElapsedSeconds(double targetElapsed) { m_targetElapsedTicks = SecondsToTicks(targetElapsed); }

		double GetTargetElapsedTicks() { return TicksToSeconds(m_targetElapsedTicks); }

		float GetFixedInterpolatedSeconds() { return m_fixedInterpolatedLerp; }

		// Integer format represents time using 10,000,000 ticks per second.
		static const uint64_t TicksPerSecond = 10000000;

		static double TicksToSeconds(uint64_t ticks) { return static_cast<double>(ticks) / TicksPerSecond; }
		static uint64_t SecondsToTicks(double seconds) { return static_cast<uint64_t>(seconds * TicksPerSecond); }

		// After an intentional timing discontinuity (for instance a blocking IO operation)
		// call this to avoid having the fixed timestep logic attempt a set of catch-up
		// Update calls.

		void ResetElapsedTime()
		{
			if (!QueryPerformanceCounter(&m_qpcLastTime))
			{
				throw std::exception("Failed_QueryPerformanceCounter");
			}
			m_leftOverTicks = 0;
			m_framesPerSecond = 0;
			m_framesThisSecond = 0;
			m_qpcSecondCounter = 0;
		}

		// Update timer state, calling the specified Update function the appropriate number of times.
		template<typename TUpdate>
		void Tick(const TUpdate& update)
		{
			// 현재 시간을 쿼리합니다.
			LARGE_INTEGER currentTime;

			if (!QueryPerformanceCounter(&currentTime))
			{
				throw std::exception("Failed_QueryPerformanceCounter");
			}

			uint64 timeDelta = currentTime.QuadPart - m_qpcLastTime.QuadPart;

			m_qpcLastTime = currentTime;
			m_qpcSecondCounter += timeDelta;

			// 너무 큰 시간 델타를 제한합니다(예: 디버거에서 일시 중지된 후).
			if (timeDelta > m_qpcMaxDelta)
			{
				timeDelta = m_qpcMaxDelta;
			}

			// QPC 단위를 정규 눈금 형식으로 변환합니다. 이전의 제한으로 인해 오버플로할 수 없습니다.
			timeDelta *= TicksPerSecond;
			timeDelta /= m_qpcFrequency.QuadPart;

			uint32 lastFrameCount = m_frameCount;

			{
				// 가변 timestep 업데이트 논리입니다.
				m_elapsedTicks = timeDelta;
				m_totalTicks += timeDelta;
				m_leftOverTicks = 0;
				m_frameCount++;
				//std::cout << "Tick" << TicksToSeconds(m_elapsedTicks) << std::endl;
				update();
			}

			// 현재 framerate를 추적합니다.
			if (m_frameCount != lastFrameCount)
			{
				m_framesThisSecond++;
			}

			if (m_qpcSecondCounter >= static_cast<uint64>(m_qpcFrequency.QuadPart))
			{
				m_framesPerSecond = m_framesThisSecond;
				m_framesThisSecond = 0;
				m_qpcSecondCounter %= m_qpcFrequency.QuadPart;
			}
		}

		template<typename TUpdate>
		void FixedTick(const TUpdate& fixedUpdate)
		{
			// 현재 시간을 쿼리합니다.
			LARGE_INTEGER currentTime;

			if (!QueryPerformanceCounter(&currentTime))
			{
				throw std::exception("Failed_QueryPerformanceCounter");
			}

			uint64 timeDelta = currentTime.QuadPart - m_qpcLastTime.QuadPart;

			// 너무 큰 시간 델타를 제한합니다(예: 디버거에서 일시 중지된 후).
			if (timeDelta > m_qpcMaxDelta)
			{
				timeDelta = m_qpcMaxDelta;
			}

			// QPC 단위를 정규 눈금 형식으로 변환합니다. 이전의 제한으로 인해 오버플로할 수 없습니다.
			timeDelta *= TicksPerSecond;
			timeDelta /= m_qpcFrequency.QuadPart;

			// 앱이 목표 경과 시간(1/4밀리초 이내)에 근접하게 실행하는 경우
			// 대상 값에 정확히 일치하도록 클록을 제한합니다. 이렇게 하면 사소하고 무관한 오류가 방지됩니다.
			// 방지할 수 있습니다. 시간을 제한하지 않으면 59.94 NTSC 디스플레이에서 사용하도록 설정된
			// vsync와 함께 실행되며 60fps 고정 업데이트가 요구되는 게임에
			// 사소한 오류가 누적되어 결국 프레임이 삭제될 수 있습니다. 반올림하는 것이 좋습니다.
			// 작은 편차를 0으로 줄여 원활하게 실행될 수 있도록 하세요.

			if (abs(static_cast<int64>(timeDelta - m_targetElapsedTicks)) < TicksPerSecond / 4000)
			{
				timeDelta = m_targetElapsedTicks;
			}

			m_fixedLeftOverTicks = m_leftOverTicks + timeDelta;

			while (m_fixedLeftOverTicks >= m_targetElapsedTicks)
			{
				m_elapsedTicks = m_targetElapsedTicks;
				m_totalTicks += m_targetElapsedTicks;
				m_fixedLeftOverTicks -= m_targetElapsedTicks;
				//std::cout << "FixedTick" << TicksToSeconds(m_elapsedTicks) << std::endl;
				m_frameCount++;

				fixedUpdate();
			}
		}

		template<typename TUpdate>
		void PostFixedTick(const TUpdate& postFixedUpdate) {
			m_fixedInterpolatedLerp = (float)m_fixedLeftOverTicks / m_targetElapsedTicks;

			postFixedUpdate();
		}

	private:
		// 원본 타이밍 데이터에는 QPC 단위가 사용됩니다.
		LARGE_INTEGER m_qpcFrequency;
		LARGE_INTEGER m_qpcLastTime;
		uint64 m_qpcMaxDelta;

		// 파생된 타이밍 데이터에는 정식 눈금 형식이 사용됩니다.
		uint64 m_elapsedTicks;
		uint64 m_totalTicks;
		uint64 m_leftOverTicks;
		uint32 m_fixedLeftOverTicks;

		// framerate 추적용 멤버입니다.
		uint32 m_frameCount;
		uint32 m_framesPerSecond;
		uint32 m_framesThisSecond;
		uint64 m_qpcSecondCounter;

		// 고정 timestep 모드 구성용 멤버입니다.
		bool m_isFixedTimeStep;
		uint64 m_targetElapsedTicks;

		float m_fixedInterpolatedLerp;
	};
}