namespace CreatorEngine;

/// <summary>
/// 프리팹 에셋 참조.
///
/// 네이티브 <c>Prefab*</c>를 그대로 들고 있지 않고 이름만 보관한다.
/// 포인터를 쥐고 있으면 에셋이 재로드될 때 매달린 참조가 되는데, 프리팹 생성은
/// 스폰 시점에만 일어나 호출 빈도가 낮으므로 매번 이름으로 찾는 편이 안전하다.
/// </summary>
public readonly struct Prefab
{
    private readonly string? _name;

    private Prefab(string name) => _name = name;

    public bool IsValid => !string.IsNullOrEmpty(_name);

    public string Name => _name ?? string.Empty;

    /// <summary>
    /// 프리팹을 찾는다. 없으면 <see cref="IsValid"/>가 false인 값을 돌려준다 —
    /// 예외를 던지지 않는 이유는 스폰 코드가 매 프레임 도는 경우가 있어서다.
    /// </summary>
    public static Prefab Load(string name)
        => Native.PrefabExists(name) ? new Prefab(name) : default;

    /// <summary>
    /// 활성 씬에 인스턴스를 만든다. 실패하면 살아 있지 않은 Entity가 돌아온다.
    /// </summary>
    /// <param name="instanceName">비우면 프리팹 이름을 그대로 쓴다.</param>
    public Entity Instantiate(string? instanceName = null)
    {
        if (!IsValid)
        {
            Native.Log(2, "[ScriptCore] 유효하지 않은 프리팹으로 Instantiate를 호출했습니다");
            return default;
        }

        return new Entity(Native.InstantiatePrefab(_name!, instanceName ?? string.Empty));
    }
}
