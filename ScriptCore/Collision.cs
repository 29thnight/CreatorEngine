using System.Runtime.InteropServices;

namespace CreatorEngine;

/// <summary>
/// 충돌·트리거 정보.
///
/// 네이티브 <c>Collision</c>은 접점 배열을 참조로 들고 있어 그대로 넘길 수 없다.
/// 경계를 넘는 것은 이 평탄한 형태이고, 접점 전체가 필요하면 별도 조회를 두는 편이 낫다
/// (대부분의 코드는 상대 오브젝트와 대표 접점만 쓴다).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct Collision
{
    private readonly ObjectHandle _other;

    /// <summary>접점 개수. 0이면 트리거처럼 접점이 없는 경우다.</summary>
    public readonly int ContactCount;

    /// <summary>대표 접점(첫 번째). ContactCount가 0이면 의미 없다.</summary>
    public readonly Float3 Contact;

    /// <summary>부딪힌 상대. 이미 파괴되었을 수 있으니 IsAlive를 확인하고 쓴다.</summary>
    public GameObject Other => new(_other);
}

/// <summary>
/// 한 프레임에 모인 물리 이벤트 하나.
///
/// 충돌마다 경계를 넘으면 "틱당 1회" 원칙이 깨지므로, 네이티브가 큐에 모아 두었다가
/// 틱 경계에서 배열 하나로 넘긴다(설계 문서 02절 "부분" 방식).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal readonly struct PhysicsEvent
{
    public readonly int InstanceId;   // 받을 스크립트
    public readonly int Kind;         // PhysicsEventKind
    public readonly Collision Collision;
}

internal enum PhysicsEventKind
{
    TriggerEnter = 0,
    TriggerStay = 1,
    TriggerExit = 2,
    CollisionEnter = 3,
    CollisionStay = 4,
    CollisionExit = 5,
}
