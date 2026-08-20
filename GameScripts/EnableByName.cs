namespace CreatorEngine.Scripts;

/// <summary>
/// 이름으로 오브젝트를 찾아 켠다. CLI에 오브젝트 활성화 명령이 없어서 만든 검증 보조 —
/// 비활성 캔버스(설정 메뉴 등)를 게임 흐름대로 깨웠을 때 지연 연결이 따라오는지 본다.
/// </summary>
public sealed partial class EnableByName : Behaviour
{
    [SerializeField] private string _target = "";

    /// <summary>몇 프레임 뒤에 켤지.</summary>
    [SerializeField] private int _afterFrames = 30;

    private int _frame;
    private bool _done;

    public override void PostPhysics(float tick)
    {
        if (_done || ++_frame < _afterFrames) return;
        _done = true;

        if (_target.Length == 0) { LogWarning("[EnableByName] 대상 미지정"); return; }

        Entity target = Entity.Find(_target);
        if (!target.IsAlive)
        {
            LogError($"[EnableByName] '{_target}' 를 찾지 못했습니다.");
            return;
        }

        target.SetEnabled(true);
        Log($"[EnableByName] '{_target}' 활성화 (frame {FrameCount})");
    }
}
