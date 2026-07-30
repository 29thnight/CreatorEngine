namespace CreatorEngine;

/// <summary>강체 종류. 네이티브 <c>EBodyType</c>과 값이 같다.</summary>
public enum BodyType
{
    /// <summary>움직이지 않는 지형·구조물.</summary>
    Static = 0,

    /// <summary>물리에 따라 움직인다.</summary>
    Dynamic = 1,

    /// <summary>코드로만 움직이고 힘을 받지 않는다. 다른 물체는 밀어낸다.</summary>
    Kinematic = 2,

    /// <summary>시뮬레이션에서 제외.</summary>
    Disabled = 3,
}

/// <summary>힘을 주는 방식. 네이티브 <c>EForceMode</c>와 값이 같다.</summary>
public enum ForceMode
{
    /// <summary>질량을 고려한 지속적인 힘.</summary>
    Force = 0,

    /// <summary>질량을 고려한 순간 충격. 넉백에 쓴다.</summary>
    Impulse = 1,

    /// <summary>질량을 무시하고 속도를 즉시 바꾼다.</summary>
    VelocityChange = 2,

    /// <summary>질량을 무시한 지속 가속.</summary>
    Acceleration = 3,
}

/// <summary>
/// 네이티브 <c>RigidBodyComponent</c>의 스크립트 쪽 얼굴.
///
/// 이 엔진에서는 콜라이더의 트리거·활성 여부도 RigidBody가 쥐고 있다
/// (<see cref="IsTrigger"/> · <see cref="ColliderEnabled"/>). 콜라이더 컴포넌트가 아니라
/// 여기에 있는 것이 헷갈리기 쉬운 부분인데, 물리 갱신 루프가 RigidBody 기준으로 돌기 때문이다.
/// </summary>
public sealed class RigidBodyComponent : NativeComponent
{
    // ── 속도 ──

    public Float3 LinearVelocity
    {
        get => Native.RigidGetLinearVelocity(OwnerHandle);
        set => Native.RigidSetLinearVelocity(OwnerHandle, value);
    }

    /// <summary>기존 속도에 더한다. 읽고-더해-쓰기와 달리 경계를 한 번만 넘는다.</summary>
    public void AddLinearVelocity(Float3 delta) => Native.RigidAddLinearVelocity(OwnerHandle, delta);

    public Float3 AngularVelocity
    {
        get => Native.RigidGetAngularVelocity(OwnerHandle);
        set => Native.RigidSetAngularVelocity(OwnerHandle, value);
    }

    /// <summary>힘을 준다. 넉백처럼 한 번에 밀 때는 <see cref="ForceMode.Impulse"/>를 쓴다.</summary>
    public void AddForce(Float3 force, ForceMode mode = ForceMode.Force)
        => Native.RigidAddForce(OwnerHandle, force, (int)mode);

    // ── 종류·상태 ──

    /// <summary>강체 종류를 바꾼다. 읽기는 엔진이 노출하지 않아 쓰기 전용이다.</summary>
    public void SetBodyType(BodyType type) => Native.RigidSetBodyType(OwnerHandle, (int)type);

    public bool IsKinematic
    {
        get => Native.RigidIsKinematic(OwnerHandle);
        set => Native.RigidSetKinematic(OwnerHandle, value);
    }

    /// <summary>켜면 통과시키고 충돌 대신 트리거 콜백만 준다.</summary>
    public bool IsTrigger
    {
        get => Native.RigidIsTrigger(OwnerHandle);
        set => Native.RigidSetIsTrigger(OwnerHandle, value);
    }

    /// <summary>끄면 충돌 자체가 사라진다. 무적 구간이나 사망 연출에 쓴다.</summary>
    public bool ColliderEnabled
    {
        get => Native.RigidIsColliderEnabled(OwnerHandle);
        set => Native.RigidSetColliderEnabled(OwnerHandle, value);
    }

    public bool UseGravity
    {
        get => Native.RigidIsUsingGravity(OwnerHandle);
        set => Native.RigidUseGravity(OwnerHandle, value);
    }

    // ── 물성 ──

    public float Mass
    {
        get => Native.RigidGetMass(OwnerHandle);
        set => Native.RigidSetMass(OwnerHandle, value);
    }

    /// <summary>이동 감쇠. 클수록 빨리 멈춘다. 읽기는 노출되지 않는다.</summary>
    public void SetLinearDamping(float damping) => Native.RigidSetLinearDamping(OwnerHandle, damping);

    public void SetAngularDamping(float damping) => Native.RigidSetAngularDamping(OwnerHandle, damping);

    /// <summary>물리 형상의 크기 배율. Transform 스케일과 별개로 관리된다.</summary>
    public void SetScale(Float3 scale) => Native.RigidSetScale(OwnerHandle, scale);

    // ── 축 잠금 ──
    //
    // 세 축을 한 번에 넘긴다. 축마다 함수를 두면 흔한 "XZ만 잠그기"에 경계를 세 번 넘는다.

    /// <summary>이동 축을 잠근다. 평면 이동만 시키려면 Y만 풀어 둔다.</summary>
    public void SetLockLinear(bool x, bool y, bool z) => Native.RigidSetLockLinear(OwnerHandle, x, y, z);

    /// <summary>회전 축을 잠근다. 캐릭터가 넘어지지 않게 전부 잠그는 형태가 흔하다.</summary>
    public void SetLockAngular(bool x, bool y, bool z) => Native.RigidSetLockAngular(OwnerHandle, x, y, z);
}
