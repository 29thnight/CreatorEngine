namespace CreatorEngine.Scripts;

/// <summary>
/// CharacterControllerComponent 래퍼 검증.
///
/// 이동은 PhysX 틱을 거쳐야 결과가 나오므로 Awake 한 번으로 판정할 수 없다.
/// 그래서 즉시 판정 가능한 것(속성 왕복·강제 이동 상태·형상 정보)은 Awake에서 보고,
/// 실제 이동은 몇 프레임 뒤 위치 변화를 확인한다.
/// </summary>
public sealed partial class CctProbe : Behaviour
{
    /// <summary>이동 확인에 쓸 입력. 기본은 +X 방향.</summary>
    [SerializeField] private float _moveInputX = 1f;
    [SerializeField] private float _moveInputY;

    /// <summary>몇 프레임 뒤에 위치 변화를 확인할지.</summary>
    [SerializeField] private int _moveCheckFrames = 30;

    private CharacterControllerComponent? _cct;
    private Float3 _moveStart;
    private int _frame;
    private bool _moveChecked;

    private int _passed;
    private int _failed;

    public override void OnInitialized()
    {
        _cct = GetComponent<CharacterControllerComponent>();

        if (_cct is null)
        {
            LogWarning("[CctProbe] CharacterControllerComponent가 없습니다 — component.add로 먼저 붙여 주세요.");
            return;
        }

        Log($"[CctProbe] 래퍼 획득 — radius={_cct.Radius} height={_cct.Height} id={_cct.ControllerId}");

        CheckShape();
        CheckBaseSpeed();
        CheckOnMove();
        CheckRotationCalls();

        Log($"[CctProbe] 즉시 검사 {_passed}건 통과 / {_failed}건 실패 — 이동 검사는 {_moveCheckFrames}프레임 뒤");

        _moveStart = Transform.WorldPosition;
        _cct.Move(_moveInputX, _moveInputY);
    }

    public override void Update(float tick)
    {
        if (_cct is null || _moveChecked) return;
        if (++_frame < _moveCheckFrames) return;

        _moveChecked = true;
        _cct.StopMove();

        Float3 moved = Transform.WorldPosition - _moveStart;
        Log($"[CctProbe] {_moveCheckFrames}프레임 이동량 {moved} (거리 {moved.Length:0.###})");

        Assert("Move 입력이 실제 이동으로 이어짐", moved.Length > 1e-3f, "위치가 그대로입니다");

        // 강제 이동은 여기서 본다. PhysX 컨트롤러 생성이 한 틱 뒤로 미뤄져 있어
        // Awake 시점에는 아직 등록 전이고, 그때 걸면 조용히 무시된다.
        CheckForcedMove();

        if (_failed == 0) Log($"[CctProbe] 전체 통과 ({_passed}건)");
        else LogError($"[CctProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void CheckShape()
    {
        // 엔진 기본값은 radius 0.55 · height 2. 0이면 정보를 못 읽어 온 것이다.
        Assert("Radius > 0", _cct!.Radius > 0f, $"{_cct.Radius}");
        Assert("Height > 0", _cct.Height > 0f, $"{_cct.Height}");
    }

    private void CheckBaseSpeed()
    {
        float start = _cct!.BaseSpeed;

        _cct.BaseSpeed = 12.5f;
        Assert("BaseSpeed 왕복", MathF.Abs(_cct.BaseSpeed - 12.5f) < 1e-4f, $"{_cct.BaseSpeed}");

        _cct.BaseSpeed = start;
    }

    private void CheckOnMove()
    {
        bool start = _cct!.IsOnMove;

        _cct.IsOnMove = true;
        Assert("IsOnMove = true", _cct.IsOnMove, "false로 읽힘");

        _cct.IsOnMove = false;
        Assert("IsOnMove = false", !_cct.IsOnMove, "true로 읽힘");

        _cct.IsOnMove = start;
    }

    private void CheckForcedMove()
    {
        Assert("초기 IsInForcedMove = false", !_cct!.IsInForcedMove, "true로 시작했습니다");

        _cct.TriggerForcedMove(new Float3(0f, 0f, 5f), 1f);
        Assert("TriggerForcedMove 후 IsInForcedMove = true", _cct.IsInForcedMove, "false로 읽힘");

        _cct.StopForcedMove();
        Assert("StopForcedMove 후 IsInForcedMove = false", !_cct.IsInForcedMove, "true로 읽힘");
    }

    /// <summary>반환값이 없어 결과를 볼 수 없다 — 죽지 않고 지나가는지만 본다.</summary>
    private void CheckRotationCalls()
    {
        _cct!.SetAutomaticRotation(false);
        _cct.SetLookDirection(new Float3(0f, 0f, 1f));
        _cct.ClearLookDirection();
        _cct.SetAutomaticRotation(true);

        Assert("회전 관련 호출", true, "");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[CctProbe] 실패: {name} — {detail}");
    }
}
