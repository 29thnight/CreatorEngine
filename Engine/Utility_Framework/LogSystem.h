// LogSystem.h
#pragma once
#include "LogSink.h"
#include "HtmlFileSink.h"
#include "ClassProperty.h"
#include <spdlog/spdlog.h>
#include <atomic>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

class DebugClass : public Singleton<DebugClass>
{
private:
	friend class Singleton<DebugClass>;
    DebugClass() = default;
	~DebugClass() = default;

    // Stable for this DebugClass lifetime; sink shutdown does not invalidate
    // snapshots or make UI reads race with resetting the sink pointer.
    const std::shared_ptr<LogStore> m_logStore{ std::make_shared<LogStore>() };
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

	// 호출 지점(file/line/function)은 where가 기본 인자로 채운다 — 호출부가
	// __FILE__을 넘길 필요가 없다.
	//
	// 예전에는 앞에 논리 태그 source를 하나 더 받아 메시지 앞에 "[source] "로
	// 붙였다. 걷어냈다 — 호출처 596 중 592가 빈 {}를 넘기고 있었고, 태그를
	// 채우는 유일한 생산자는 C# 경계였는데 그쪽은 이제 자기 호출 지점을
	// [CallerFilePath] 계열로 직접 싣는다. 태그는 문자열 연결이라 구조 필드도
	// 아니었다 — 로그 창이 따로 낼 수도 걸러낼 수도 없었다.
	void PrintLog(spdlog::level::level_enum level, std::string_view message,
		std::source_location where = std::source_location::current())
	{
		PrintLog(level, message, spdlog::source_loc{ where.file_name(),
			static_cast<int>(where.line()), where.function_name() });
	}

	// 경계 밖에서 온 호출 지점을 그대로 싣는 저수준 진입점.
	//
	// std::source_location은 값으로 만들 수 없다(표준이 current()만 준다). 그래서
	// CLR 브리지처럼 file/line/function을 문자열로 건네받는 쪽은 이 오버로드로
	// 들어온다 — 위 오버로드를 거치면 기본 인자가 재평가돼 브리지 자신의 위치를
	// 싣게 된다.
	//
	// where가 든 포인터는 이 호출 동안만 살아 있으면 된다. 로거가 동기라
	// (async_logger가 아니다) 포매팅이 여기서 끝난다.
	void PrintLog(spdlog::level::level_enum level, std::string_view message,
		spdlog::source_loc where)
	{
		spdlog::log(where, level, "{}", message);
	}

	// fmt 문자열 리터럴과 호출 지점(source_location)을 함께 캡처하는 래퍼.
	// std::format_string<Args...>와 같은 방식으로 fmt는 컴파일 타임에 자리표시자
	// 개수/타입이 검증되고, where는 이 값이 넘어오는 실제 호출부에서 기본 인자로
	// 채워진다 — 포맷 오버로드가 내부적으로 다른 함수를 거쳐도 위치 정보가 유실되지
	// 않는다.
	template <typename... Args>
	struct FormatArgs
	{
		std::format_string<Args...> fmt;
		std::source_location where;

		template <typename T>
		consteval FormatArgs(const T& f, std::source_location loc = std::source_location::current())
			: fmt(f), where(loc)
		{
		}
	};

	// std::format 스타일 포맷 오버로드. fmt는 컴파일 타임에 검증되는 리터럴이어야 하고,
	// args는 런타임 값이면 된다 — 자리표시자 없이 완성 문자열만 넘기던 << 체인을
	// 대신한다.
	// FormatArgs<Args...> 자리에 std::type_identity_t를 씌워 비연역 컨텍스트로 만든다
	// (std::format_string 자신도 이 방식을 쓴다) — 그러지 않으면 컴파일러가 fmt
	// 리터럴 하나만으로 Args를 추론하려다 실패한다. 실제 Args는 뒤의 args... 팩에서만
	// 추론된다.
	template <typename... Args>
	void PrintLog(spdlog::level::level_enum level,
		FormatArgs<std::type_identity_t<Args>...> fmtArgs, Args&&... args)
	{
		PrintLog(level, std::format(fmtArgs.fmt, std::forward<Args>(args)...), fmtArgs.where);
	}

	void LogWarning(std::string_view message,
		std::source_location where = std::source_location::current())
	{
		PrintLog(spdlog::level::warn, message, where);
	}

	void Log(std::string_view message,
		std::source_location where = std::source_location::current())
	{
		PrintLog(spdlog::level::info, message, where);
	}

	void LogError(std::string_view message,
		std::source_location where = std::source_location::current())
	{
		PrintLog(spdlog::level::err, message, where);
	}

	void LogDebug(std::string_view message,
		std::source_location where = std::source_location::current())
	{
		PrintLog(spdlog::level::debug, message, where);
	}

	void LogTrace(std::string_view message,
		std::source_location where = std::source_location::current())
	{
		PrintLog(spdlog::level::trace, message, where);
	}

	void LogCritical(std::string_view message,
		std::source_location where = std::source_location::current())
	{
		PrintLog(spdlog::level::critical, message, where);
	}

	void Flush()
	{
		FlushNow();
	}

	void Clear()
	{
		m_logStore->Clear();
	}

	LogSnapshot GetLogSnapshot() const
	{
		return m_logStore->ReadSnapshot();
	}

	std::optional<LogSnapshot> GetLogSnapshotIfChanged(std::uint64_t revision) const
	{
		return m_logStore->ReadSnapshotIfChanged(revision);
	}

	// 화면이 프레임마다 전체를 받지 않도록 변경분만 준다. 커서는 돌려받은
	// 것을 그대로 다음 호출에 넘긴다.
	/// 목록을 들지 않는 소비자(상태 표시줄)가 수준별 누적만 읽는다.
	LogLevelTotals GetLogLevelTotals() const { return m_logStore->ReadLevelTotals(); }

	std::optional<LogDelta> GetLogDeltaSince(LogCursor cursor) const
	{
		return m_logStore->ReadDeltaSince(cursor);
	}
};

namespace Debug
{
	inline DebugClass& Instance() { return *DebugClass::GetInstance(); }

	// where는 이 오버로드가 직접 호출된 지점(예: PhysicsManager.cpp)에서 기본값으로
	// 채워지고, Instance().PrintLog로 그 값을 그대로 전달한다 — 여기서 생략하면
	// LogSystem.h 안에서 재평가돼 실제 호출 지점을 잃어버린다.
	inline void PrintLog(spdlog::level::level_enum level, std::string_view message,
		std::source_location where = std::source_location::current())
	{
		Instance().PrintLog(level, message, where);
	}

	// 경계 밖에서 온 호출 지점을 그대로 넘기는 진입점. CLR 브리지가 쓴다.
	inline void PrintLog(spdlog::level::level_enum level, std::string_view message,
		spdlog::source_loc where)
	{
		Instance().PrintLog(level, message, where);
	}

	template <typename... Args>
	inline void PrintLog(spdlog::level::level_enum level,
		DebugClass::FormatArgs<std::type_identity_t<Args>...> fmtArgs, Args&&... args)
	{
		Instance().PrintLog(level, std::move(fmtArgs), std::forward<Args>(args)...);
	}

	inline void Clear() { Instance().Clear(); }
	inline LogSnapshot GetLogSnapshot() { return Instance().GetLogSnapshot(); }
	inline std::optional<LogSnapshot> GetLogSnapshotIfChanged(std::uint64_t revision)
	{
		return Instance().GetLogSnapshotIfChanged(revision);
	}
	inline LogLevelTotals GetLogLevelTotals() { return Instance().GetLogLevelTotals(); }
	inline std::optional<LogDelta> GetLogDeltaSince(LogCursor cursor)
	{
		return Instance().GetLogDeltaSince(cursor);
	}
}

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
		DebugClass::GetInstance()->Initialize(sessionName);
	}

	inline void Finalize()
	{
		if (!IsAlive()) return;   // 중복 호출 및 파괴 후 접근 방지

		DebugClass::GetInstance()->Finalize();
		DebugClass::Destroy();
	}

	// 위험 구간 진입 전이나 크래시 핸들러에서 호출한다.
	inline void FlushNow()
	{
		if (IsAlive()) DebugClass::GetInstance()->FlushNow();
	}

	inline void NotifyCrash(std::string_view reason)
	{
		if (IsAlive()) DebugClass::GetInstance()->NotifyCrash(reason);
	}

	// SEH 예외 정보를 해석해 로그에 남기고 즉시 flush한다.
	// exceptionPointers는 EXCEPTION_POINTERS* (헤더에서 Windows.h 의존을 피하려 void*로 받는다).
	// 상위 예외 필터가 사용자 확인 대화상자 등으로 대기하기 "전에" 호출해야
	// 그 사이 프로세스가 강제 종료돼도 로그가 보존된다.
	void NotifySehCrash(void* exceptionPointers);
}
