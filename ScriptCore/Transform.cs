namespace CreatorEngine;

/// <summary>
/// 오브젝트의 위치·회전·스케일.
///
/// 네이티브 Transform과 같은 컴포넌트다 — 그쪽에서 S1-b로 Component에 승격되며
/// m_components로 들어갔고(ComponentTypeUUID.h), 이쪽도 같은 층에 선다. 그래서
/// GetComponent&lt;Transform&gt;()이 정식 경로이고, MeshRenderer를 얻는 것과 문법이 같다.
///
/// ── 없을 수 있다 ──
///
/// UI/Canvas는 Transform을 갖지 않고 RectTransformComponent만 갖는다(S3). 그래서 조회는
/// null을 돌려줄 수 있다. 예전에는 이것이 값 뷰(struct)라 "없음"을 표현할 방법이 아예
/// 없었고, 없는 Transform에 값을 써도 성공한 것처럼 보였다 — 네이티브가 공유 더미로
/// 받아 로그 한 줄만 남기고 값을 버렸기 때문이다(Entity.cpp MissingTransformFallback).
/// </summary>
public sealed class Transform : NativeComponent
{

    // ── 로컬 ──

    public Float3 LocalPosition
    {
        get => Native.GetLocalPosition(OwnerHandle);
        set => Native.SetLocalPosition(OwnerHandle, value);
    }

    public Quaternion LocalRotation
    {
        get => Native.GetLocalRotation(OwnerHandle);
        set => Native.SetLocalRotation(OwnerHandle, value);
    }

    public Float3 LocalScale
    {
        get => Native.GetLocalScale(OwnerHandle);
        set => Native.SetLocalScale(OwnerHandle, value);
    }

    // ── 월드 ──
    //
    // 월드 위치·회전·스케일은 읽기는 캐시된 값이고 쓰기는 부모 역행렬을 거쳐
    // 로컬로 환산된다(엔진 SetWorldPosition/SetWorldRotation과 같은 경로).

    public Float3 WorldPosition
    {
        get => Native.GetWorldPosition(OwnerHandle);
        set => Native.SetWorldPosition(OwnerHandle, value);
    }

    public Quaternion WorldRotation
    {
        get => Native.GetWorldRotation(OwnerHandle);
        set => Native.SetWorldRotation(OwnerHandle, value);
    }

    public Float3 WorldScale
    {
        get => Native.GetWorldScale(OwnerHandle);
        set => Native.SetWorldScale(OwnerHandle, value);
    }

    // ── 방향축 ──
    // 월드 기준이고 정규화되어 있다. 조준·이동에서 가장 많이 쓰인다(GetForward 실측 41회).

    public Float3 Forward => Native.GetForward(OwnerHandle);
    public Float3 Right   => Native.GetRight(OwnerHandle);
    public Float3 Up      => Native.GetUp(OwnerHandle);

    // ── 누적 ──
    // 읽고-고쳐-쓰기로 하면 경계를 두 번 넘는다. 네이티브가 한 번에 처리한다.

    public void Translate(Float3 delta) => Native.AddLocalPosition(OwnerHandle, delta);

    /// <summary>기존 회전에 이어 붙인다(엔진 AddRotation과 같은 순서).</summary>
    public void Rotate(Quaternion delta) => Native.AddLocalRotation(OwnerHandle, delta);

    // ── 남의 오브젝트를 고칠 때 ──
    //
    // Transform은 핸들만 담은 struct라 Entity.Transform 프로퍼티가 임시 복사본을
    // 돌려준다. 그래서 obj.Transform.LocalPosition = ... 은 컴파일이 막힌다.
    // 자기 것을 고칠 때는 Component.Transform이 필드라 위의 프로퍼티를 그냥 쓰면 된다.

    public void SetLocalPosition(Float3 p) => Native.SetLocalPosition(OwnerHandle, p);
    public void SetLocalRotation(Quaternion q) => Native.SetLocalRotation(OwnerHandle, q);
    public void SetLocalScale(Float3 s) => Native.SetLocalScale(OwnerHandle, s);

    public void SetWorldPosition(Float3 p) => Native.SetWorldPosition(OwnerHandle, p);
    public void SetWorldRotation(Quaternion q) => Native.SetWorldRotation(OwnerHandle, q);
    public void SetWorldScale(Float3 s) => Native.SetWorldScale(OwnerHandle, s);

    /// <summary>주어진 지점을 바라보게 한다. 위쪽은 +Y로 잡는다.</summary>
    public void LookAt(Float3 target)
    {
        Float3 direction = target - WorldPosition;
        if (direction.LengthSquared < 1e-12f) return;

        Native.SetWorldRotation(OwnerHandle, Quaternion.LookRotation(direction));
    }
}
