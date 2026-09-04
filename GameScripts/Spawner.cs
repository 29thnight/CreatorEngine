namespace CreatorEngine.Scripts;

/// <summary>
/// S2 대표 케이스 — 프리팹 생성과 수명 관리.
///
/// 일정 간격으로 프리팹을 스폰하고, 정해진 수를 넘으면 가장 오래된 것을 지운다.
/// 생성 → 참조 보관 → 파괴가 한 스크립트 안에서 도는 흐름이라
/// 핸들 수명과 지연 파괴 규약을 함께 확인할 수 있다.
/// </summary>
public sealed partial class Spawner : Component
{
    [SerializeField] private string _prefabName = "";
    [SerializeField] private float _interval = 1.0f;
    [SerializeField] private int _maxAlive = 5;
    [SerializeField] private Float3 _spawnOffset = new(0f, 0f, 2f);

    private readonly Queue<Entity> _spawned = new();
    private Prefab _prefab;
    private string? _loadedName;
    private float _timer;
    private int _spawnCount;

    /// <summary>
    /// 이름이 바뀌면 다시 로드한다.
    /// Start에서 한 번만 하지 않는 이유는, 인스펙터에서 프리팹 이름을 나중에 채우거나
    /// 실행 중에 바꾸는 일이 흔하기 때문이다.
    /// </summary>
    private void EnsurePrefab()
    {
        if (_loadedName == _prefabName) return;

        _loadedName = _prefabName;
        _prefab = default;

        if (string.IsNullOrEmpty(_prefabName)) return;

        _prefab = Prefab.Load(_prefabName);
        if (_prefab.IsValid)
        {
            Log($"[Spawner] 준비됨 — '{_prefabName}', {_interval}초 간격, 최대 {_maxAlive}개");
        }
        else
        {
            LogError($"[Spawner] 프리팹을 찾을 수 없습니다: {_prefabName}");
        }
    }

    public override void PostPhysics(float tick)
    {
        EnsurePrefab();
        if (!_prefab.IsValid) return;

        _timer += tick;
        if (_timer < _interval) return;
        _timer = 0f;

        Spawn();
        TrimOldest();
    }

    private void Spawn()
    {
        Entity instance = _prefab.Instantiate($"{_prefabName}_{_spawnCount}");
        if (!instance.IsAlive)
        {
            LogWarning("[Spawner] 인스턴스 생성 실패 — 스폰을 멈춥니다");
            _prefab = default;
            _loadedName = _prefabName;   // 같은 이름으로 다시 시도하지 않는다
            return;
        }

        // 생성 위치는 스포너 기준 오프셋. 스폰마다 조금씩 어긋나게 둬서 겹치지 않게 한다.
        Float3 basePos = Transform.LocalPosition;
        instance.Transform?.SetLocalPosition(basePos + _spawnOffset * (1f + _spawnCount * 0.1f));

        _spawned.Enqueue(instance);
        ++_spawnCount;

        Log($"[Spawner] 스폰 {instance.Name} (총 {_spawnCount}, 생존 {_spawned.Count})");
    }

    private void TrimOldest()
    {
        while (_spawned.Count > _maxAlive)
        {
            Entity oldest = _spawned.Dequeue();

            // 이미 다른 경로로 사라졌을 수 있다. 세대 핸들이 그것을 알려준다.
            if (!oldest.IsAlive) continue;

            Log($"[Spawner] 정리 {oldest.Name}");
            oldest.Destroy();
        }
    }

    public override void OnUninitializing()
    {
        // 스포너가 사라지면 자기가 만든 것도 함께 치운다.
        while (_spawned.Count > 0)
        {
            Entity obj = _spawned.Dequeue();
            if (obj.IsAlive) obj.Destroy();
        }
    }
}
