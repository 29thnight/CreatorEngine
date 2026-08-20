namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>AniBehavior</c>에 대응하는 애니메이션 상태 스크립트.
///
/// <see cref="Behaviour"/>와 다른 축이다. Behaviour는 오브젝트에 붙어 매 프레임 도는 반면,
/// 이쪽은 애니메이션 컨트롤러의 <b>상태 하나</b>에 붙어 그 상태에 들어오고 나갈 때만 불린다.
/// 대시·공격처럼 "상태 진입 시 한 번" 처리하는 로직이 여기 들어간다.
///
/// 콜백은 발생 시점에 바로 오지 않고 틱 경계에서 일괄 전달된다 — 물리 콜백과 같은 이유로
/// 상태 전이마다 경계를 넘으면 "틱당 1회" 원칙이 무너진다(설계 문서 02절).
/// 순서는 발생 순서 그대로 유지된다.
/// </summary>
public abstract class AniBehaviour
{
    /// <summary>이 상태를 소유한 애니메이터가 붙어 있는 오브젝트.</summary>
    public Entity Entity { get; internal set; }

    /// <summary>소유 오브젝트의 Transform. <see cref="Behaviour.Transform"/>과 같은 이유로 필드다.</summary>
    public Transform Transform;

    /// <summary>대상 오브젝트가 살아 있는지.</summary>
    public bool IsAlive => Entity.IsAlive;

    /// <summary>이 상태에 진입할 때 한 번.</summary>
    public virtual void Enter() { }

    /// <summary>이 상태가 유지되는 동안 매 프레임.</summary>
    public virtual void Update(float tick) { }

    /// <summary>이 상태에서 빠져나갈 때 한 번.</summary>
    public virtual void Exit() { }

    // ── 편의 ──
    // 소유 오브젝트를 대상으로 하는 조회. Behaviour와 같은 표기를 쓸 수 있게 둔다.

    public T? GetComponent<T>() where T : Component => Entity.GetComponent<T>();
    public T? GetComponentInParent<T>(bool includeSelf = true) where T : Component
        => Entity.GetComponentInParent<T>(includeSelf);
    public T? GetComponentInChildren<T>(bool includeSelf = true) where T : Component
        => Entity.GetComponentInChildren<T>(includeSelf);

    /// <summary>엔진 프레임 번호. 콜백이 어느 프레임에 왔는지 볼 때 쓴다.</summary>
    public static ulong FrameCount => Native.FrameCount;

    protected static void Log(string message) => Native.Log(1, message);
    protected static void LogWarning(string message) => Native.Log(2, message);
    protected static void LogError(string message) => Native.Log(3, message);
}

/// <summary>전달할 콜백 종류. 네이티브 <c>ScriptAniEventKind</c>와 값이 같아야 한다.</summary>
internal enum AniEventKind
{
    Enter = 0,
    Update = 1,
    Exit = 2,
}

/// <summary>
/// 네이티브가 틱 경계에 한 번에 넘기는 애니메이션 콜백 하나.
/// 네이티브 <c>ScriptAniEvent</c>와 배치가 같아야 한다.
/// </summary>
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
internal struct AniEvent
{
    public int InstanceId;
    public int Kind;
    public float DeltaTime;
    public ObjectHandle Owner;
}
