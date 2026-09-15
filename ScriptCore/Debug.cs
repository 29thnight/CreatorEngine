namespace CreatorEngine;

/// <summary>
/// 엔진 Output Log로 전달하는 명시적 관리 코드 로깅 API.
/// Console.WriteLine은 명령 결과나 진단 출력과 의미가 다르므로 자동으로 가로채지 않는다.
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

    public static void PrintLog(string source, LogLevel level, string message)
    {
        Native.PrintLog(source, (int)level, message);
    }
}
