namespace CreatorEngine.Scripts;

/// <summary>
/// 기존 C++ 스크립트 <c>Dynamic_CPP/Assets/Script/PlayEffectAll.cpp</c>의 C# 이관본.
///
/// 자식에 붙은 이펙트를 한꺼번에 재생하고, 전부 끝나면 자기 오브젝트를 지운다.
/// 일회성 연출(타격·폭발 등)을 오브젝트 하나로 묶어 던져 놓는 용도다.
///
/// 원본과 달라진 점:
///  · <c>weak_ptr</c>이 필요 없다 — 래퍼가 세대 핸들만 들고 있어서 대상이 사라지면
///    <c>IsAlive</c>가 false가 된다. 원본은 이걸 위해 weak_from_this를 썼다.
///  · <c>m_isCallStart</c> 검사가 없다 — 엔진의 ModuleBehavior는 Start가 불렸는지
///    스크립트가 직접 확인해야 했지만, 여기서는 Start가 첫 Update보다 먼저 도는 것이
///    <c>BehaviourRegistry</c>에서 보장된다.
///  · <c>Initialize()</c>는 옮기지 않았다 — Start와 하는 일이 같고, 외부에서 부르는
///    곳이 없다(원본에도 호출자가 없다).
/// </summary>
public sealed partial class PlayEffectAll : Behaviour
{
    /// <summary>
    /// 한 프레임 기다렸다 재생한다. 스폰 직후에는 위치가 아직 반영되지 않아,
    /// 그 프레임에 이펙트를 태우면 엉뚱한 자리에서 터진다(원본 주석의 이유 그대로).
    /// </summary>
    [SerializeField] private bool _delayOneFrame = true;

    /// <summary>동작을 로그로 남긴다. 이관 검증용이라 기본은 꺼져 있다.</summary>
    [SerializeField] private bool _verbose;

    private readonly List<EffectComponent> _effects = new();
    private bool _frameElapsed;
    private bool _started;

    public override void Start()
    {
        // 직계 자식만 본다 — 원본이 m_childrenIndices를 한 겹만 도는 것과 같다.
        foreach (GameObject child in GameObject.Children)
        {
            if (child.GetComponent<EffectComponent>() is { } effect) _effects.Add(effect);
        }

        if (_effects.Count == 0)
        {
            LogWarning($"[PlayEffectAll] {GameObject.Name}: 자식에 EffectComponent가 없습니다.");
        }
        else if (_verbose)
        {
            Log($"[PlayEffectAll] {GameObject.Name}: 자식 이펙트 {_effects.Count}개 수집");
        }
    }

    public override void Update(float tick)
    {
        if (!_started && (_frameElapsed || !_delayOneFrame))
        {
            _started = true;
            foreach (EffectComponent effect in _effects)
            {
                if (effect.IsAlive) effect.Apply();
            }

            if (_verbose) Log($"[PlayEffectAll] frame {FrameCount}: 이펙트 {_effects.Count}개 재생 시작");
        }

        _frameElapsed = true;

        // 아직 시작도 안 했는데 "전부 끝났다"고 판정해 자신을 지우면 안 된다.
        if (!_started) return;

        foreach (EffectComponent effect in _effects)
        {
            if (effect.IsAlive && effect.IsPlaying) return;
        }

        if (_verbose) Log($"[PlayEffectAll] frame {FrameCount}: 전부 끝나 {GameObject.Name} 파괴");
        GameObject.Destroy();
    }
}
