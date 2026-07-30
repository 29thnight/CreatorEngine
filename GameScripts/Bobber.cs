namespace CreatorEngine.Scripts;

/// <summary>
/// S1 수직 슬라이스 대상 스크립트.
///
/// 일부러 최소한으로 만들었다 — 목적은 게임 로직이 아니라
/// 빌드 → 로드 → 라이프사이클 → 엔진 API 호출 전 구간이 실제로 관통되는지 보는 것이다.
/// 오브젝트를 제자리에서 위아래로 흔들어, 값이 네이티브까지 왕복하는지 눈으로 확인한다.
/// </summary>
public sealed partial class Bobber : Behaviour
{
    // 소스 제너레이터가 이 필드들을 보고 인덱스 기반 접근자를 만든다.
    // 인스펙터 표시와 직렬화가 그 접근자를 함께 쓴다(설계 문서 04절).
    [SerializeField] private float _amplitude = 1.0f;
    [SerializeField] private float _speed = 2.0f;
    [SerializeField(DisplayName = "가로 흔들기")] private bool _horizontal;

    private Float3 _origin;
    private float _elapsed;

    public override void Awake()
    {
        Log($"[Bobber] Awake — {GameObject.Name}");
    }

    public override void Start()
    {
        _origin = Transform.LocalPosition;
        Log($"[Bobber] Start — 기준 위치 {_origin}");
    }

    public override void Update(float tick)
    {
        _elapsed += tick;

        float offset = MathF.Sin(_elapsed * _speed) * _amplitude;

        Transform.LocalPosition = _horizontal
            ? _origin + new Float3(offset, 0f, 0f)
            : _origin + new Float3(0f, offset, 0f);
    }

    public override void OnDestroy()
    {
        Log("[Bobber] OnDestroy");
    }
}


