namespace CreatorEngine.Scripts;

/// <summary>
/// EffectComponent 래퍼 검증.
///
/// 실제 파티클 재생 여부는 이펙트 템플릿이 프로젝트에 있어야 확인할 수 있다.
/// 템플릿 없이도 검증되는 것(속성 왕복·상태 플래그·호출 경로)을 먼저 보고,
/// 템플릿 이름이 주어지면 Apply 후 재생 상태까지 확인한다.
/// </summary>
public sealed partial class EffectProbe : Behaviour
{
    /// <summary>재생 확인에 쓸 이펙트 템플릿 이름. 비우면 재생 검사를 건너뛴다.</summary>
    [SerializeField] private string _templateName = "";

    private EffectComponent? _effect;
    private int _passed;
    private int _failed;

    public override void Awake()
    {
        _effect = GetComponent<EffectComponent>();

        if (_effect is null)
        {
            LogWarning("[EffectProbe] EffectComponent가 없습니다 — component.add로 먼저 붙여 주세요.");
            return;
        }

        Log($"[EffectProbe] 래퍼 획득 — template='{_effect.TemplateName}' loop={_effect.Loop} " +
            $"duration={_effect.Duration} timeScale={_effect.TimeScale}");

        CheckTemplateName();
        CheckScalarProperties();
        CheckLoop();
        CheckPlayback();

        if (_failed == 0) Log($"[EffectProbe] 전체 통과 ({_passed}건)");
        else LogError($"[EffectProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void CheckTemplateName()
    {
        string start = _effect!.TemplateName;

        _effect.TemplateName = "__검증용템플릿__";
        Assert("TemplateName 왕복", _effect.TemplateName == "__검증용템플릿__", $"'{_effect.TemplateName}'");

        _effect.TemplateName = start;
        Assert("TemplateName 복원", _effect.TemplateName == start, $"'{_effect.TemplateName}' vs '{start}'");
    }

    private void CheckScalarProperties()
    {
        float startDuration = _effect!.Duration;
        float startTimeScale = _effect.TimeScale;

        _effect.Duration = 3.5f;
        Assert("Duration 왕복", MathF.Abs(_effect.Duration - 3.5f) < 1e-4f, $"{_effect.Duration}");

        _effect.TimeScale = 0.25f;
        Assert("TimeScale 왕복", MathF.Abs(_effect.TimeScale - 0.25f) < 1e-4f, $"{_effect.TimeScale}");

        _effect.Duration = startDuration;
        _effect.TimeScale = startTimeScale;
    }

    private void CheckLoop()
    {
        bool start = _effect!.Loop;

        _effect.Loop = !start;
        Assert("Loop 왕복", _effect.Loop == !start, $"{_effect.Loop}");

        _effect.Loop = start;
    }

    /// <summary>템플릿이 없으면 재생은 확인할 수 없다 — 호출 경로가 죽지 않는지만 본다.</summary>
    private void CheckPlayback()
    {
        if (_templateName.Length == 0)
        {
            Log("[EffectProbe] 템플릿 미지정 — 재생 검사 대신 호출 경로만 확인합니다");

            _effect!.Stop();
            _effect.Pause();
            _effect.Resume();
            _effect.ForceFinish();

            Assert("재생 제어 호출", true, "");
            return;
        }

        _effect!.TemplateName = _templateName;
        _effect.Loop = true;
        _effect.Apply();

        Assert($"'{_templateName}' Apply 후 IsPlaying", _effect.IsPlaying, "false로 읽힘");

        _effect.Pause();
        Assert("Pause 후 IsPaused", _effect.IsPaused, "false로 읽힘");

        _effect.Resume();
        Assert("Resume 후 IsPaused = false", !_effect.IsPaused, "true로 읽힘");

        _effect.Stop();
        Assert("Stop 후 IsPlaying = false", !_effect.IsPlaying, "true로 읽힘");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[EffectProbe] 실패: {name} — {detail}");
    }
}
