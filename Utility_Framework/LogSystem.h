// LogSystem.h
#pragma once
#include "LogSink.h"
#include "HtmlFileSink.h"
#include "ClassProperty.h"
#include "ClassProperty.h"
#include <spdlog/spdlog.h>
#include <atomic>
#include <string>
#include <string_view>

class DebugClass : public Singleton<DebugClass>
{
private:
	friend class Singleton<DebugClass>;
    DebugClass() = default;
	~DebugClass() = default;

    std::shared_ptr<LogSink> logSink{};
	std::shared_ptr<HtmlFileSink> htmlSink{};
	std::string m_logFilePath{};
	std::atomic<bool> m_initialized{ false };

public:
	void Initialize(std::string_view sessionName = "Editor");
	// Initialize가 sink/default logger 구성 중 throw했을 때 쓰는 fail-closed 정리.
	// 생존 플래그를 먼저 내린 뒤 부분 구성된 spdlog/sink를 폐기한다.
	void AbortInitialization() noexcept;
	void Finalize();

	// spdlog를 종료하지 않고 디스크까지만 밀어낸다.
	// Finalize()와 달리 호출 후에도 로깅을 계속할 수 있어
	// 크래시 핸들러나 위험 구간 진입 직전에 안전하게 쓸 수 있다.
	void FlushNow();

	// 크래시 경로 전용. 사유를 로그 문서에 남기고 즉시 flush한다.
	// spdlog 로거 상태와 무관하게 싱크에 직접 기록하므로
	// 로거가 이미 파괴된 뒤에도 동작한다.
	void NotifyCrash(std::string_view reason);

	bool IsInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }
	const std::string& GetLogFilePath() const noexcept { return m_logFilePath; }

	void LogWarning(std::string_view message)
	{
		spdlog::warn(message);
	}

	void Log(std::string_view message)
	{
		spdlog::info(message);
	}

	void LogError(std::string_view message)
	{
		spdlog::error(message);
	}

	void LogDebug(std::string_view message)
	{
		spdlog::debug(message);
	}

	void LogTrace(std::string_view message)
	{
		spdlog::trace(message);
	}

	void LogCritical(std::string_view message)
	{
		spdlog::critical(message);
	}

	void Flush()
	{
		FlushNow();
	}

	void Clear()
	{
		if (logSink) logSink->ringBuffer_.clear();
	}

	bool IsClear() const
	{
		return logSink ? logSink->ringBuffer_.IsClear() : true;
	}

	void toggleClear()
	{
		if (logSink) logSink->ringBuffer_.toggleClear();
	}

	std::string GetBackLogMessage() const
	{
		return logSink ? logSink->m_backLogMessage : std::string{};
	}

	std::string& WriteBackLogMessage()
	{
		return logSink->m_backLogMessage;
	}

	std::vector<LogEntry> get_entries()
	{
		return logSink ? logSink->ringBuffer_.get_all() : std::vector<LogEntry>{};
	}
};

static auto Debug = DebugClass::GetInstance();

namespace Log
{
	// 프로세스가 어떤 경로로 죽든 로그가 디스크에 남도록 훅을 설치한다.
	// (std::terminate / abort / purecall / CRT 잘못된 파라미터 / atexit / 미처리 SEH)
	// Initialize()에서 자동 호출되므로 별도로 부를 필요는 없다.
	void InstallCrashGuards();

	/// 크래시 시 덤프를 남길 곳을 등록한다.
	///
	/// 크래시 후크(SEH·terminate·abort·purecall·잘못된 인자) 설치는 InstallCrashGuards
	/// 한 곳에서만 한다. 다른 곳에서 같은 후크를 또 걸면 서로 덮어써서, 설치 순서에 따라
	/// 덤프가 남기도 하고 안 남기도 한다(실제로 겪은 문제다).
	/// 덤프 기록만 이 콜백으로 위임한다 — 로그 시스템은 파일 포맷을 알 필요가 없다.
	///
	/// exceptionPointers는 SEH 경로에서만 유효하고 나머지 경로에서는 널이다.
	/// 호출은 크래시당 1회만 보장된다.
	using CrashDumpWriter = void (*)(void* exceptionPointers, const char* reason);
	void SetCrashDumpWriter(CrashDumpWriter writer);

	/// 덤프 기록자가 등록돼 있는지. 크래시가 나기 전에 물어볼 수 있어야
	/// '덤프가 없다'를 사후가 아니라 사전에 알 수 있다.
	bool HasCrashDumpWriter() noexcept;

	// 로그 시스템이 살아 있는지 확인한다.
	//
	// 위 전역 Debug는 각 번역 단위에 복사된 원시 포인터라, DebugClass::Destroy()
	// 이후에도 파괴된 인스턴스의 주소를 그대로 들고 있다. nullptr 검사로는 걸러지지
	// 않으므로, 종료 단계에서 Debug를 만지는 코드는 반드시 이 함수로 먼저 확인해야 한다.
	bool IsAlive() noexcept;

	inline void Initialize(std::string_view sessionName = "Editor")
    {
		DebugClass::GetInstance();
		Debug->Initialize(sessionName);
	}

	inline void Finalize()
	{
		if (!IsAlive()) return;   // 중복 호출 및 파괴 후 접근 방지

		Debug->Finalize();
		DebugClass::Destroy();
	}

	// 위험 구간 진입 전이나 크래시 핸들러에서 호출한다.
	inline void FlushNow()
	{
		if (IsAlive() && Debug) Debug->FlushNow();
	}

	inline void NotifyCrash(std::string_view reason)
	{
		if (IsAlive() && Debug) Debug->NotifyCrash(reason);
	}

	// SEH 예외 정보를 해석해 로그에 남기고 즉시 flush한다.
	// exceptionPointers는 EXCEPTION_POINTERS* (헤더에서 Windows.h 의존을 피하려 void*로 받는다).
	// 상위 예외 필터가 사용자 확인 대화상자 등으로 대기하기 "전에" 호출해야
	// 그 사이 프로세스가 강제 종료돼도 로그가 보존된다.
	void NotifySehCrash(void* exceptionPointers);
}
