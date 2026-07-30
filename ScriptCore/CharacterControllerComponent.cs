namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>CharacterControllerComponent</c>(PhysX CCT 래핑)의 스크립트 쪽 얼굴.
///
/// 실측에서 획득 47회로 네이티브 컴포넌트 2위이고, 플레이어·몬스터 이동이 전부 여기를 지난다.
/// 엔진 멤버는 78개지만 게임 스크립트가 실제로 쓰는 것은 9개뿐이라 그것들만 열었다.
/// </summary>
public sealed class CharacterControllerComponent : NativeComponent
{
    // ── 이동 ──

    /// <summary>
    /// 이동 입력을 넣는다. 실제 이동은 물리 틱에서 일어나므로 이 호출은 값만 기록한다.
    /// 입력은 XZ 평면 기준의 2차원 값이다.
    /// </summary>
    public void Move(float inputX, float inputY) => Native.CctMove(OwnerHandle, inputX, inputY);

    /// <summary>속도 0을 넣어 이동을 멈춘다.</summary>
    public void StopMove() => Native.CctMove(OwnerHandle, 0f, 0f);

    /// <summary>이동 중인지. 엔진이 물리 틱에서 갱신한다.</summary>
    public bool IsOnMove
    {
        get => Native.CctIsOnMove(OwnerHandle);
        set => Native.CctSetOnMove(OwnerHandle, value);
    }

    public bool IsFalling => Native.CctIsFalling(OwnerHandle);

    /// <summary>기본 이동 속도. 버프·디버프는 별도 배수로 곱해진다.</summary>
    public float BaseSpeed
    {
        get => Native.CctGetBaseSpeed(OwnerHandle);
        set => Native.CctSetBaseSpeed(OwnerHandle, value);
    }

    // ── 강제 이동 ──
    // 넉백·대시처럼 입력과 무관하게 밀어내는 경우에 쓴다(실측 8회).

    /// <summary>
    /// 주어진 속도로 강제 이동을 요청한다.
    /// <paramref name="duration"/>이 0이면 엔진이 속도가 줄어들 때까지 이어간다.
    /// </summary>
    /// <remarks>
    /// 엔진에는 이징 곡선 인자가 하나 더 있지만 노출하지 않았다 —
    /// 기존 게임 스크립트 8곳 어디도 쓰지 않아 기본값(None)으로 둔다.
    /// </remarks>
    public void TriggerForcedMove(Float3 velocity, float duration = 0f)
        => Native.CctTriggerForcedMove(OwnerHandle, velocity, duration);

    public void StopForcedMove() => Native.CctStopForcedMove(OwnerHandle);

    public bool IsInForcedMove => Native.CctIsInForcedMove(OwnerHandle);

    // ── 회전·순간이동 ──

    /// <summary>
    /// 이동 방향으로 자동 회전할지. 끄면 <see cref="SetLookDirection"/>으로 직접 잡는다.
    /// </summary>
    public void SetAutomaticRotation(bool useAuto) => Native.CctSetAutomaticRotation(OwnerHandle, useAuto);

    /// <summary>바라볼 방향을 고정한다. 자동 회전을 끈 상태에서 쓴다.</summary>
    public void SetLookDirection(Float3 direction) => Native.CctSetLookDirection(OwnerHandle, direction);

    public void ClearLookDirection() => Native.CctClearLookDirection(OwnerHandle);

    /// <summary>
    /// 지정한 위치로 순간이동한다. Transform을 직접 고치면 PhysX 컨트롤러와 어긋나므로
    /// CCT가 붙은 오브젝트는 반드시 이 경로를 쓴다.
    /// </summary>
    public void ForcedSetPosition(Float3 position) => Native.CctForcedSetPosition(OwnerHandle, position);

    // ── 형상 정보 ──
    //
    // 엔진은 CharacterControllerInfo 구조체를 통째로 돌려주지만, 스크립트가 읽는 것은
    // radius(실측 3회)와 id(2회)뿐이라 필드만 꺼내 온다. 구조체를 경계에 노출하면
    // 필드가 하나 바뀔 때마다 양쪽 배치를 맞춰야 한다.

    public float Radius => Native.CctGetRadius(OwnerHandle);
    public float Height => Native.CctGetHeight(OwnerHandle);

    /// <summary>PhysX 컨트롤러 ID. 물리 질의 결과와 대조할 때 쓴다.</summary>
    public uint ControllerId => Native.CctGetId(OwnerHandle);
}
