namespace CreatorEngine;

/// <summary>블랙보드 값의 종류. 네이티브 BlackBoardType과 값이 같아야 한다.</summary>
public enum BlackBoardType
{
    None = 0,
    Bool = 1,
    Int = 2,
    Float = 3,
    String = 4,
    Vector2 = 5,
    Vector3 = 6,
    Vector4 = 7,
    GameObject = 8,
    Transform = 9,
}

/// <summary>
/// 행동 트리가 공유하는 값 저장소.
///
/// 트리와 함께 관리 영역에 산다 — 노드가 값을 읽고 쓸 때 경계를 넘지 않는 것이
/// C# 우선 재설계의 요점이다(BehaviorTreeManagedPlan §1). 네이티브가 값을 봐야 하는
/// 곳이 생기면 그때 개별 접근자를 추가한다.
///
/// 타입은 키마다 고정된다. 다른 타입으로 읽으면 예외 대신 기본값을 돌려주고 경고를
/// 남긴다 — AI 한 마리의 오타로 프레임 전체가 죽는 것을 막기 위해서다.
/// </summary>
public sealed class BlackBoard
{
    private readonly struct Value(BlackBoardType type, object boxed)
    {
        public readonly BlackBoardType Type = type;
        public readonly object Boxed = boxed;
    }

    private readonly Dictionary<string, Value> _values = new(StringComparer.Ordinal);

    public string Name { get; internal set; } = string.Empty;

    public bool HasKey(string key) => _values.ContainsKey(key);

    public BlackBoardType GetType(string key)
        => _values.TryGetValue(key, out Value value) ? value.Type : BlackBoardType.None;

    public void RemoveKey(string key) => _values.Remove(key);

    public void Clear() => _values.Clear();

    public IReadOnlyCollection<string> Keys => _values.Keys;

    // ── 쓰기 ──

    public void SetBool(string key, bool value) => _values[key] = new Value(BlackBoardType.Bool, value);
    public void SetInt(string key, int value) => _values[key] = new Value(BlackBoardType.Int, value);
    public void SetFloat(string key, float value) => _values[key] = new Value(BlackBoardType.Float, value);
    public void SetString(string key, string value) => _values[key] = new Value(BlackBoardType.String, value);
    public void SetVector2(string key, Float2 value) => _values[key] = new Value(BlackBoardType.Vector2, value);
    public void SetVector3(string key, Float3 value) => _values[key] = new Value(BlackBoardType.Vector3, value);
    public void SetGameObject(string key, GameObject value) => _values[key] = new Value(BlackBoardType.GameObject, value);

    // ── 읽기 ──

    public bool GetBool(string key) => Read(key, BlackBoardType.Bool, false);
    public int GetInt(string key) => Read(key, BlackBoardType.Int, 0);
    public float GetFloat(string key) => Read(key, BlackBoardType.Float, 0f);
    public string GetString(string key) => Read(key, BlackBoardType.String, string.Empty);
    public Float2 GetVector2(string key) => Read(key, BlackBoardType.Vector2, default(Float2));
    public Float3 GetVector3(string key) => Read(key, BlackBoardType.Vector3, default(Float3));
    // GameObject는 구조체라 "없음"을 null로 표현하지 않는다 — 기본 핸들을 돌려주고
    // 호출 쪽이 IsAlive로 판단한다(엔진의 다른 경로와 같은 규약).
    public GameObject GetGameObject(string key) => Read(key, BlackBoardType.GameObject, default(GameObject));

    private T Read<T>(string key, BlackBoardType expected, T fallback)
    {
        if (!_values.TryGetValue(key, out Value value))
        {
            Native.Log(2, $"[BlackBoard] '{key}' 키가 없습니다.");
            return fallback;
        }

        if (value.Type != expected)
        {
            Native.Log(2, $"[BlackBoard] '{key}'는 {value.Type}인데 {expected}로 읽었습니다.");
            return fallback;
        }

        return value.Boxed is T typed ? typed : fallback;
    }
}
