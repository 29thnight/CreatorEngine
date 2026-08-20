namespace CreatorEngine.Scripts;

/// <summary>
/// GetComponent 검증용 — 다른 스크립트가 찾아 쓰는 쪽.
/// </summary>
public sealed partial class HealthComponent : Behaviour
{
    [SerializeField] private int _maxHp = 100;

    public int CurrentHp { get; private set; }

    public override void OnInitialized()
    {
        CurrentHp = _maxHp;
    }

    public void TakeDamage(int amount)
    {
        CurrentHp = Math.Max(0, CurrentHp - amount);
        Log($"[Health] {Entity.Name} 피해 {amount} → 남은 체력 {CurrentHp}/{_maxHp}");
    }
}
