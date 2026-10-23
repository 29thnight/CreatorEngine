using System.Globalization;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace CreatorEngine;

/// <summary>결과 갈래. 네이티브 <c>ClrHost::InvokeOutcome</c> 과 값이 같아야 한다.</summary>
internal enum InvokeOutcome
{
    Ok = 0,

    /// <summary>게임 스크립트 어셈블리가 올라와 있지 않다.</summary>
    NoAssembly = -1,

    TypeNotFound = -2,
    TypeAmbiguous = -3,
    MethodNotFound = -4,
    MethodAmbiguous = -5,

    /// <summary>있지만 <see cref="EngineCallableAttribute"/> 가 없다. **이것이 경계다.**</summary>
    NotMarked = -6,

    ArgumentMismatch = -7,

    /// <summary>사용자 코드가 던졌다. 에디터는 죽지 않는다.</summary>
    Threw = -8,

    Internal = -9,
}

/// <summary>
/// 이름으로 지목된 <b>표식된</b> static 메서드를 호출한다(L3-B · §10.2).
///
/// ── 왜 캐시가 하나도 없는가 ─────────────────────────────────────────────
///
/// <see cref="Type"/> 이나 <see cref="MethodInfo"/> 를 정적 필드에 담으면 그것이
/// 게임 스크립트 어셈블리를 붙들고, 붙들린 <c>ScriptLoadContext</c> 는 언로드되지
/// 않는다. 그러면 다음 리로드부터 같은 타입이 두 벌 생기고 캐스팅이 조용히 깨진다
/// — 크래시가 아니라 "저 오브젝트만 스크립트가 안 돈다" 로 나타나 원인을 짚기
/// 어려운 종류다. 그래서 조회는 매번 새로 하고, 이 클래스는 상태를 갖지 않는다.
///
/// 호출 비용은 리플렉션 조회 한 번이다. 이 통로는 에이전트가 왕복하는 자리이지
/// 프레임 루프가 아니므로, 캐시로 벌 것보다 캐시가 만들 결함이 크다.
///
/// ── 무엇을 거부하는가 ───────────────────────────────────────────────────
///
/// 표식이 없으면 거부한다. 그 거부는 <see cref="InvokeOutcome.MethodNotFound"/> 가
/// 아니라 <see cref="InvokeOutcome.NotMarked"/> 다 — "없다" 와 "있지만 안 연다" 를
/// 같은 코드로 내면 오타와 권한 거부를 구분해 셀 수 없다.
/// </summary>
internal static class EngineCallableInvoker
{
    public static InvokeOutcome Invoke(string typeName, string methodName, string[] args, out string payload)
        => ManagedRuntime.Current.Invoke(typeName, methodName, args, out payload);
}
