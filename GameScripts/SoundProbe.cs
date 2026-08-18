namespace CreatorEngine.Scripts;

/// <summary>
/// 네이티브 컴포넌트 래퍼 첫 검증 — SoundComponent.
///
/// 스크립트가 자기 오브젝트에 붙은 네이티브 컴포넌트를 <c>GetComponent&lt;SoundComponent&gt;()</c>로
/// 잡아 속성을 읽고 쓰고 재생을 걸어 본다. 관리 스크립트 조회와 달리 여기서는
/// 존재 확인 한 번만 경계를 넘고, 이후 호출은 각각 함수 포인터 직행이다.
/// </summary>
public sealed partial class SoundProbe : Behaviour
{
    /// <summary>비워 두면 이미 인스펙터에 설정된 클립을 그대로 쓴다.</summary>
    [SerializeField] private string _clipKey = "";

    [SerializeField] private float _volume = 0.5f;

    // 기본값(1)과 다른 값을 써야 왕복이 실제로 일어났는지 구분된다.
    [SerializeField] private float _pitch = 1.25f;

    /// <summary>재생 상태를 몇 프레임마다 찍을지. 0이면 찍지 않는다.</summary>
    [SerializeField] private int _reportInterval = 60;

    private SoundComponent? _sound;
    private int _frame;

    public override void OnInitialized()
    {
        _sound = GetComponent<SoundComponent>();

        if (_sound is null)
        {
            LogWarning("[SoundProbe] SoundComponent가 없습니다 — component.add로 먼저 붙여 주세요.");
            return;
        }

        Log($"[SoundProbe] 래퍼 획득. 현재 clipKey='{_sound.ClipKey}' volume={_sound.Volume} pitch={_sound.Pitch}");

        // 왕복 검증 — 쓴 값이 네이티브 멤버에 그대로 남는지 본다.
        if (_clipKey.Length > 0) _sound.ClipKey = _clipKey;
        _sound.Volume = _volume;
        _sound.Pitch = _pitch;

        Log($"[SoundProbe] 설정 후 clipKey='{_sound.ClipKey}' volume={_sound.Volume} pitch={_sound.Pitch}");

        // 왕복이 맞았는지 스크립트가 스스로 판정한다 — 로그를 눈으로 대조할 필요가 없게.
        bool ok = (_clipKey.Length == 0 || _sound.ClipKey == _clipKey)
               && _sound.Volume == _volume
               && _sound.Pitch == _pitch;

        if (ok) Log("[SoundProbe] 왕복 검증 통과");
        else LogError("[SoundProbe] 왕복 검증 실패 — 쓴 값과 읽은 값이 다릅니다.");

        if (_sound.ClipKey.Length == 0)
        {
            LogWarning("[SoundProbe] clipKey가 비어 있어 재생은 건너뜁니다(네이티브가 조용히 무시함).");
            return;
        }

        // 등록되지 않은 키면 SoundManager가 오류만 남기고 조용히 빠진다 — 크래시하지 않는다.
        _sound.Play();
        Log($"[SoundProbe] Play 직후 IsPlaying={_sound.IsPlaying()}");
    }

    public override void Update(float tick)
    {
        if (_sound is null || _reportInterval <= 0) return;

        if (++_frame % _reportInterval == 0)
        {
            Log($"[SoundProbe] frame {FrameCount} — IsPlaying={_sound.IsPlaying()}");
        }
    }

    public override void OnUninitializing()
    {
        // 래퍼는 핸들만 들고 있으므로, 오브젝트가 살아 있을 때만 만진다.
        if (_sound is not null && GameObject.IsAlive) _sound.Stop();
    }
}

