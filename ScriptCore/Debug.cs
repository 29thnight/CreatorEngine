namespace CreatorEngine;

using System.Runtime.CompilerServices;

/// <summary>
/// 엔진 Output Log로 전달하는 명시적 관리 코드 로깅 API.
/// Console.WriteLine은 명령 결과나 진단 출력과 의미가 다르므로 자동으로 가로채지 않는다.
///
/// 호출 지점(file/line/member)은 [Caller*] 특성이 컴파일러 시점에 채운다.
/// 네이티브의 std::source_location 기본 인자와 같은 성격이고, 호출자는 아무것도
/// 넘기지 않는다 — 넘기면 오히려 실제 호출 지점을 덮어쓴다.
///
/// 예전에는 앞에 논리 태그 source를 받아 메시지 앞에 "[source] "로 붙였다.
/// 걷어냈다. 태그는 문자열 연결이라 구조 필드가 아니었고, 실제 출처는 이제
/// 아래 세 값이 Output Log의 source 열에 그대로 뜬다.
/// </summary>
public static class Debug
{
    public enum LogLevel
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
        Critical = 4
    }

    public static void PrintLog(LogLevel level, string message,
        [CallerFilePath] string file = "",
        [CallerLineNumber] int line = 0,
        [CallerMemberName] string member = "")
    {
        Native.PrintLog((int)level, message, file, line, member);
    }

    public static void Log(string message,
        [CallerFilePath] string file = "",
        [CallerLineNumber] int line = 0,
        [CallerMemberName] string member = "")
        => Native.PrintLog((int)LogLevel.Info, message, file, line, member);

    public static void LogWarning(string message,
        [CallerFilePath] string file = "",
        [CallerLineNumber] int line = 0,
        [CallerMemberName] string member = "")
        => Native.PrintLog((int)LogLevel.Warning, message, file, line, member);

    public static void LogError(string message,
        [CallerFilePath] string file = "",
        [CallerLineNumber] int line = 0,
        [CallerMemberName] string member = "")
        => Native.PrintLog((int)LogLevel.Error, message, file, line, member);
}
