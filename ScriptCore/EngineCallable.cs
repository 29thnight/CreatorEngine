namespace CreatorEngine;

/// <summary>
/// 엔진이 <c>script.invoke</c> 로 부를 수 있는 static 메서드에 붙인다(L3-B · §10.2).
///
/// ★ 표식이 없으면 **이름을 알아도 호출되지 않는다.**
///
///   이 표식이 L3 등급 B 의 경계 전부다. 표식을 확인하지 않으면
///   `POST /command` 하나가 곧 "로드된 어셈블리의 아무 static 메서드나 부르는 API"
///   가 된다 — 게임 스크립트 어셈블리에는 파일을 지우는 메서드도 있을 수 있고,
///   그것이 의도된 표면인지 아닌지를 가를 것이 이름 말고는 없어진다.
///
///   그래서 거부는 조용하지 않다. 표식 없는 메서드를 부르면 "그런 메서드가 없다"가
///   아니라 "있지만 표식이 없다" 로 답한다(§10.2 의 "표식 없는 메서드 호출 0" 은
///   시도가 0 이라는 뜻이 아니라 **성공이 0** 이라는 뜻이고, 둘을 구분해 세려면
///   거부가 자기 이름을 갖고 있어야 한다).
///
/// ── 붙일 수 있는 자리 ───────────────────────────────────────────────────
///
/// static 메서드만이다. 인스턴스 메서드는 "어느 인스턴스인가"를 이름으로 정할 수
/// 없다 — 그 답은 `script.add` 가 낸 인스턴스 id 이고, 그것으로 부르는 통로는
/// 필드 접근자(`script.fields`·`script.set`)가 이미 갖고 있다.
///
/// 인자와 반환은 문자열로 왕복한다. 지원하는 파라미터 타입은
/// <c>string</c>·<c>bool</c>·<c>int</c>·<c>long</c>·<c>float</c>·<c>double</c> 이고,
/// 그 밖의 타입을 받는 메서드는 표식이 있어도 **인자 변환에서 거부**된다.
/// 넓히는 것은 필요가 실측될 때 한다 — 지금 넓히면 쓰지 않는 변환 규칙만 는다.
/// </summary>
/// <example>
/// <code>
/// [EngineCallable]
/// public static int SpawnWave(int count) => ...;
/// </code>
/// </example>
[AttributeUsage(AttributeTargets.Method, Inherited = false, AllowMultiple = false)]
public sealed class EngineCallableAttribute : Attribute
{
}
