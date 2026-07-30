namespace CreatorEngine;

/// <summary>
/// 인스펙터에 노출하고 직렬화 대상으로 삼을 필드에 붙인다.
/// C++ 쪽 <c>[[Property]]</c>에 대응한다.
///
/// 런타임 리플렉션으로 필드를 훑지 않는다 — Native AOT가 사용처를 정적으로 볼 수 없는
/// 멤버를 트리밍하기 때문이다(설계 문서 04·06절). 대신 소스 제너레이터가 컴파일 타임에
/// 접근자를 생성한다.
/// </summary>
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false)]
public sealed class SerializeFieldAttribute : Attribute
{
    /// <summary>인스펙터에 표시할 이름. 비우면 필드 이름을 다듬어 쓴다.</summary>
    public string? DisplayName { get; init; }
}

/// <summary>
/// 노출 필드의 타입. 네이티브 인스펙터가 어떤 위젯을 그릴지 정하는 데 쓴다.
/// 값이 네이티브와 맞아야 하므로 번호를 고정한다.
/// </summary>
public enum FieldType
{
    Unknown = 0,
    Float   = 1,
    Int32   = 2,
    Bool    = 3,
    Float3  = 4,
    String  = 5,

    /// <summary>
    /// 씬 오브젝트 참조. 저장할 때는 핸들이 아니라 instanceID로 적는다 —
    /// 핸들의 슬롯 번호는 실행할 때마다 달라져 파일에 남길 수 없다.
    /// </summary>
    Object  = 6,

    /// <summary>UI 좌표·오프셋. Float3와 같은 방식으로 오간다.</summary>
    Float2  = 7,
}

