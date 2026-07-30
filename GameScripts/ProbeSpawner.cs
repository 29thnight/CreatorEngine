namespace CreatorEngine.Scripts;

/// <summary>
/// 스폰 직후 대상 스크립트가 이미 깨어 있는지 확인하는 진단용 스포너.
///
/// Unity라면 Instantiate가 반환된 시점에 Awake가 끝나 있어야 한다.
/// 여기서는 그 가정이 성립하는지를 프레임 번호로 비교한다.
/// </summary>
public sealed partial class ProbeSpawner : Behaviour
{
    [SerializeField] private string _prefabName = "";
    [SerializeField] private int _spawnAtFrame = 30;

    private int _frame;
    private bool _done;

    public override void Update(float tick)
    {
        ++_frame;

        if (_done || _frame < _spawnAtFrame || string.IsNullOrEmpty(_prefabName)) return;
        _done = true;

        Prefab prefab = Prefab.Load(_prefabName);
        if (!prefab.IsValid)
        {
            LogError($"[ProbeSpawner] 프리팹 없음: {_prefabName}");
            return;
        }

        Log($"[ProbeSpawner] 엔진프레임 {FrameCount} — Instantiate 호출 직전");
        GameObject spawned = prefab.Instantiate("ProbeInstance");
        Log($"[ProbeSpawner] 엔진프레임 {FrameCount} — Instantiate 반환 (살아있음={spawned.IsAlive})");
    }
}

