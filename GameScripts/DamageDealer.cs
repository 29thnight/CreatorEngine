namespace CreatorEngine.Scripts;

/// <summary>
/// GetComponent 검증용 — 찾아 쓰는 쪽.
///
/// 같은 오브젝트의 HealthComponent와, 지정한 대상 오브젝트의 HealthComponent를
/// 각각 찾아 본다. 둘 다 경계를 넘지 않고 관리 영역에서 해결된다.
/// </summary>
public sealed partial class DamageDealer : Component
{
    [SerializeField] private Entity _target;
    [SerializeField] private int _damage = 7;
    [SerializeField] private float _interval = 0.5f;

    private HealthComponent? _ownHealth;
    private float _timer;
    private bool _reported;

    public override void OnBeginSimulation()
    {
        // 자기 오브젝트에서 찾기 — 가장 흔한 형태.
        _ownHealth = GetComponent<HealthComponent>();

        Log($"[Damage] 자기 HealthComponent: {(_ownHealth is null ? "없음" : $"있음(체력 {_ownHealth.CurrentHp})")}");
    }

    public override void PostPhysics(float tick)
    {
        _timer += tick;
        if (_timer < _interval) return;
        _timer = 0f;

        if (!_target.IsAlive)
        {
            if (!_reported)
            {
                _reported = true;
                LogWarning("[Damage] 대상이 지정되지 않았습니다");
            }
            return;
        }

        // 남의 오브젝트에서 찾기 — 스크립트 간 참조. 역시 경계를 넘지 않는다.
        HealthComponent? health = _target.GetComponent<HealthComponent>();
        if (health is null)
        {
            if (!_reported)
            {
                _reported = true;
                LogWarning($"[Damage] {_target.Name}에 HealthComponent가 없습니다");
            }
            return;
        }

        _reported = false;
        health.TakeDamage(_damage);
    }
}
