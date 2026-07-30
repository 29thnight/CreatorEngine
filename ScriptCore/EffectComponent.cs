namespace CreatorEngine;

/// <summary>
/// 네이티브 <c>EffectComponent</c>의 스크립트 쪽 얼굴.
///
/// 획득 50회로 네이티브 컴포넌트 1위다. 기존 게임 코드가 쓰는 형태는 사실상 하나로,
/// 템플릿 이름을 갈아 끼우고 <see cref="Apply"/>로 다시 태우는 흐름이다.
/// <code>
/// var effect = GetComponent&lt;EffectComponent&gt;();
/// effect.TemplateName = "portal";
/// effect.Apply();
/// </code>
/// </summary>
public sealed class EffectComponent : NativeComponent
{
    /// <summary>재생할 이펙트 템플릿 이름. 바꾼 뒤 <see cref="Apply"/>를 불러야 반영된다.</summary>
    public string TemplateName
    {
        get => Native.EffectGetTemplateName(OwnerHandle);
        set => Native.EffectSetTemplateName(OwnerHandle, value);
    }

    /// <summary>현재 템플릿으로 이펙트를 다시 만들어 재생한다.</summary>
    public void Apply() => Native.EffectApply(OwnerHandle);

    /// <summary>
    /// 컴포넌트를 초기화한다. 엔진이 컴포넌트를 붙일 때 한 번 부르므로 보통은 필요 없고,
    /// 설정을 통째로 바꾼 뒤 처음 상태로 되돌릴 때 쓴다.
    /// </summary>
    public void Initialize() => Native.EffectInitialize(OwnerHandle);

    // ── 재생 제어 ──

    public void Stop() => Native.EffectStop(OwnerHandle);
    public void Pause() => Native.EffectPause(OwnerHandle);
    public void Resume() => Native.EffectResume(OwnerHandle);

    /// <summary>남은 재생 시간을 무시하고 즉시 끝낸 것으로 처리한다.</summary>
    public void ForceFinish() => Native.EffectForceFinish(OwnerHandle);

    /// <summary>템플릿을 바꿔 끼운다. <c>TemplateName</c> 대입 후 <c>Apply</c>와 달리 엔진이 교체를 직접 처리한다.</summary>
    public void ChangeEffect(string effectName) => Native.EffectChangeEffect(OwnerHandle, effectName);

    /// <summary>현재 템플릿을 건드리지 않고 지정한 이펙트만 한 번 재생한다.</summary>
    public void PlayEffectByName(string effectName) => Native.EffectPlayByName(OwnerHandle, effectName);

    // ── 상태 ──

    public bool IsPlaying => Native.EffectIsPlaying(OwnerHandle);
    public bool IsPaused => Native.EffectIsPaused(OwnerHandle);

    /// <summary>재생 시작 후 흐른 시간.</summary>
    public float CurrentTime => Native.EffectGetCurrentTime(OwnerHandle);

    public bool Loop
    {
        get => Native.EffectGetLoop(OwnerHandle);
        set => Native.EffectSetLoop(OwnerHandle, value);
    }

    /// <summary>재생 길이. 음수면 템플릿에 정의된 길이를 따른다.</summary>
    public float Duration
    {
        get => Native.EffectGetDuration(OwnerHandle);
        set => Native.EffectSetDuration(OwnerHandle, value);
    }

    /// <summary>재생 속도 배수. 슬로우 모션·히트 스톱에 쓴다.</summary>
    public float TimeScale
    {
        get => Native.EffectGetTimeScale(OwnerHandle);
        set => Native.EffectSetTimeScale(OwnerHandle, value);
    }
}
