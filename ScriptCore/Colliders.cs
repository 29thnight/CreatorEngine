namespace CreatorEngine;

/// <summary>콜라이더 종류. 네이티브 디스패치 인자와 값이 같아야 한다.</summary>
internal enum ColliderKind
{
    Sphere = 0,
    Box = 1,
    Capsule = 2,
}

/// <summary>
/// 콜라이더 세 종류의 공통 표면.
///
/// 엔진의 <c>ICollider</c>에는 크기·마찰이 올라와 있지 않아 타입마다 같은 멤버가 따로
/// 선언돼 있다. 경계에 함수를 세 벌 만드는 대신 종류를 인자로 넘겨 네이티브에서
/// 디스패치한다 — 파생 클래스는 자기 <see cref="Kind"/>만 알려 주면 된다.
///
/// 트리거 여부와 콜라이더 켜고 끄기는 여기가 아니라
/// <see cref="RigidBodyComponent"/>에 있다(엔진 구조가 그렇다).
/// </summary>
public abstract class ColliderComponent : NativeComponent
{
    internal abstract ColliderKind Kind { get; }

    /// <summary>오브젝트 원점에서 형상 중심까지의 오프셋.</summary>
    public Float3 PositionOffset
    {
        get => Native.ColliderGetPositionOffset(OwnerHandle, (int)Kind);
        set => Native.ColliderSetPositionOffset(OwnerHandle, (int)Kind, value);
    }

    /// <summary>반발 계수. 클수록 잘 튄다.</summary>
    public float Restitution
    {
        get => Native.ColliderGetRestitution(OwnerHandle, (int)Kind);
        set => Native.ColliderSetRestitution(OwnerHandle, (int)Kind, value);
    }

    /// <summary>정지 마찰. 멈춰 있는 물체가 미끄러지기 시작하는 저항이다.</summary>
    public float StaticFriction
    {
        get => Native.ColliderGetStaticFriction(OwnerHandle, (int)Kind);
        set => Native.ColliderSetStaticFriction(OwnerHandle, (int)Kind, value);
    }

    /// <summary>운동 마찰. 미끄러지는 동안의 저항이다.</summary>
    public float DynamicFriction
    {
        get => Native.ColliderGetDynamicFriction(OwnerHandle, (int)Kind);
        set => Native.ColliderSetDynamicFriction(OwnerHandle, (int)Kind, value);
    }
}

/// <summary>구 콜라이더.</summary>
public sealed class SphereColliderComponent : ColliderComponent
{
    internal override ColliderKind Kind => ColliderKind.Sphere;

    public float Radius
    {
        get => Native.ColliderGetRadius(OwnerHandle, (int)Kind);
        set => Native.ColliderSetRadius(OwnerHandle, (int)Kind, value);
    }
}

/// <summary>상자 콜라이더.</summary>
public sealed class BoxColliderComponent : ColliderComponent
{
    internal override ColliderKind Kind => ColliderKind.Box;

    /// <summary>각 축 방향의 크기.</summary>
    public Float3 Extents
    {
        get => Native.ColliderGetExtents(OwnerHandle, (int)Kind);
        set => Native.ColliderSetExtents(OwnerHandle, (int)Kind, value);
    }
}

/// <summary>캡슐 콜라이더. 캐릭터 형상에 주로 쓴다.</summary>
public sealed class CapsuleColliderComponent : ColliderComponent
{
    internal override ColliderKind Kind => ColliderKind.Capsule;

    public float Radius
    {
        get => Native.ColliderGetRadius(OwnerHandle, (int)Kind);
        set => Native.ColliderSetRadius(OwnerHandle, (int)Kind, value);
    }

    public float Height
    {
        get => Native.ColliderGetHeight(OwnerHandle, (int)Kind);
        set => Native.ColliderSetHeight(OwnerHandle, (int)Kind, value);
    }
}
