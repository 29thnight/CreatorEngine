namespace CreatorEngine.Scripts;

/// <summary>
/// <c>script.invoke</c> 계약(L3-B · §10.2)의 고정 fixture.
///
/// ★ 표식 있는 것과 **없는 것을 나란히** 둔다.
///
///   게이트의 핵심 단정은 "표식 없는 메서드는 이름을 알아도 호출되지 않는다" 이고,
///   그것을 확인하려면 **실제로 존재하지만 표식이 없는** 메서드가 있어야 한다.
///   없는 이름으로 시험하면 거부가 아니라 오타를 확인하는 셈이라, 표식 검사를
///   통째로 지워도 그 시험은 초록으로 남는다.
///
/// 값은 전부 결정적이다. 게이트가 반환값을 문자로 맞대 본다.
/// </summary>
public static class EngineCallableProbe
{
    /// <summary>인자 없이 부르고 결정적 문자열을 받는다.</summary>
    [EngineCallable]
    public static string Ping() => "pong";

    /// <summary>인자 변환(int·string·bool·double)이 왕복하는지 본다.</summary>
    [EngineCallable]
    public static string Echo(string text, int count, bool upper, double scale)
    {
        string body = string.Concat(Enumerable.Repeat(text, count));
        if (upper) body = body.ToUpperInvariant();
        return $"{body}|{scale.ToString("R", System.Globalization.CultureInfo.InvariantCulture)}";
    }

    /// <summary>사용자 코드가 던져도 에디터가 죽지 않는지 본다.</summary>
    [EngineCallable]
    public static void Throw() => throw new InvalidOperationException("의도된 프로브 예외");

    /// <summary>void 반환이 성공으로 판정되는지 본다.</summary>
    [EngineCallable]
    public static void Touch() { }

    /// <summary>
    /// ★ **표식이 없다.** 지우지 말 것 — 이것이 없으면 표식 검사를 시험할 수 없다.
    /// </summary>
    public static string Unmarked() => "이 값은 밖으로 나가면 안 된다";

    /// <summary>지원하지 않는 파라미터 타입. 표식이 있어도 인자 변환에서 거부된다.</summary>
    [EngineCallable]
    public static string UnsupportedParameter(DateTime when) => when.ToString("O");
}
