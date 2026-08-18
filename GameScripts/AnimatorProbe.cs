namespace CreatorEngine.Scripts;

/// <summary>
/// Animator 래퍼 검증.
///
/// 엔진 파라미터는 컨트롤러에 미리 선언되어 있어야 한다 — 스크립트에서 새로 만들 수 없다.
/// 그래서 "선언된 파라미터가 있으면 왕복을 검사하고, 없으면 건너뛴다"로 짰다.
/// 파라미터가 하나도 없는 모델에서도 존재 확인·레이어·정지 경로는 그대로 검증된다.
/// </summary>
public sealed partial class AnimatorProbe : Behaviour
{
    /// <summary>왕복 검사에 쓸 bool 파라미터 이름. 컨트롤러에 있는 것으로 지정한다.</summary>
    [SerializeField] private string _boolParameter = "";

    /// <summary>왕복 검사에 쓸 float 파라미터 이름.</summary>
    [SerializeField] private string _floatParameter = "";

    /// <summary>왕복 검사에 쓸 trigger 파라미터 이름.</summary>
    [SerializeField] private string _triggerParameter = "";

    private Animator? _animator;
    private int _passed;
    private int _failed;

    public override void OnInitialized()
    {
        _animator = GetComponent<Animator>();

        if (_animator is null)
        {
            LogWarning("[AnimatorProbe] Animator가 없습니다 — component.add로 먼저 붙여 주세요.");
            return;
        }

        Log("[AnimatorProbe] 래퍼 획득");

        CheckMissingParameter();
        CheckBool();
        CheckFloat();
        CheckTrigger();
        CheckLayerAndStop();

        if (_failed == 0) Log($"[AnimatorProbe] 전체 통과 ({_passed}건)");
        else LogError($"[AnimatorProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    /// <summary>없는 이름은 조용히 기본값을 돌려주고 죽지 않아야 한다.</summary>
    private void CheckMissingParameter()
    {
        const string missing = "__없는파라미터__";

        Assert("없는 파라미터 HasParameter=false", !_animator!.HasParameter(missing), "true가 나왔습니다");

        _animator.SetBool(missing, true);
        _animator.SetFloat(missing, 1f);
        _animator.SetTrigger(missing);

        Assert("없는 파라미터 읽기 기본값", !_animator.GetBool(missing) && _animator.GetFloat(missing) == 0f,
            $"bool={_animator.GetBool(missing)} float={_animator.GetFloat(missing)}");
    }

    private void CheckBool()
    {
        if (_boolParameter.Length == 0) { Log("[AnimatorProbe] bool 파라미터 미지정 — 왕복 검사 건너뜀"); return; }

        if (!_animator!.HasParameter(_boolParameter))
        {
            LogWarning($"[AnimatorProbe] 컨트롤러에 '{_boolParameter}' 파라미터가 없습니다 — 건너뜀");
            return;
        }

        _animator.SetBool(_boolParameter, true);
        Assert($"bool '{_boolParameter}' = true", _animator.GetBool(_boolParameter), "false로 읽힘");

        _animator.SetBool(_boolParameter, false);
        Assert($"bool '{_boolParameter}' = false", !_animator.GetBool(_boolParameter), "true로 읽힘");
    }

    private void CheckFloat()
    {
        if (_floatParameter.Length == 0) { Log("[AnimatorProbe] float 파라미터 미지정 — 왕복 검사 건너뜀"); return; }

        if (!_animator!.HasParameter(_floatParameter))
        {
            LogWarning($"[AnimatorProbe] 컨트롤러에 '{_floatParameter}' 파라미터가 없습니다 — 건너뜀");
            return;
        }

        _animator.SetFloat(_floatParameter, 2.5f);
        Assert($"float '{_floatParameter}' 왕복", MathF.Abs(_animator.GetFloat(_floatParameter) - 2.5f) < 1e-4f,
            $"{_animator.GetFloat(_floatParameter)}");

        _animator.SetFloat(_floatParameter, 0f);
    }

    private void CheckTrigger()
    {
        if (_triggerParameter.Length == 0) { Log("[AnimatorProbe] trigger 파라미터 미지정 — 왕복 검사 건너뜀"); return; }

        if (!_animator!.HasParameter(_triggerParameter))
        {
            LogWarning($"[AnimatorProbe] 컨트롤러에 '{_triggerParameter}' 파라미터가 없습니다 — 건너뜀");
            return;
        }

        _animator.SetTrigger(_triggerParameter);
        Assert($"trigger '{_triggerParameter}' 세움", _animator.GetBool(_triggerParameter), "false로 읽힘");

        _animator.ResetTrigger(_triggerParameter);
        Assert($"trigger '{_triggerParameter}' 되돌림", !_animator.GetBool(_triggerParameter), "true로 읽힘");
    }

    /// <summary>반환값이 없어 결과를 볼 수 없다 — 죽지 않고 지나가는지만 본다.</summary>
    private void CheckLayerAndStop()
    {
        _animator!.SetUseLayer(1, false);
        _animator.SetUseLayer(1, true);
        _animator.StopAnimation(0.1f);

        Assert("SetUseLayer·StopAnimation 호출", true, "");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[AnimatorProbe] 실패: {name} — {detail}");
    }
}
