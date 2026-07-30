namespace CreatorEngine.Scripts;

/// <summary>
/// 기존 C++ 스크립트 <c>Dynamic_CPP/Assets/Script/CurveIndicator.cpp</c>의 C# 이관본.
///
/// 자식 오브젝트들을 시작점에서 끝점까지 이차 베지어 곡선 위에 늘어놓아
/// 포물선 궤적을 미리 보여 준다. 곡선 가운데로 갈수록 커지고 진해진다.
///
/// 원본과 달라진 점:
///  · 자식 목록을 <c>GameObject*</c>로 들고 있던 것이 값 타입 <c>GameObject</c>가 됐다.
///    세대 핸들이라 대상이 사라져도 안전하다(원본은 raw 포인터를 그대로 붙들고 있었다).
///  · 재질 사본을 만드는 이유를 <see cref="MeshRenderer.InstantiateMaterial"/>에 담았다.
///  · 원본은 자식에 MeshRenderer가 없으면 널 역참조로 죽었다 — 아래 주석 참고.
/// </summary>
public sealed partial class CurveIndicator : Behaviour
{
    /// <summary>표식 사이 간격. 이 값과 거리로 곡선 위 위치가 정해진다.</summary>
    [SerializeField] private float _interval = 1f;

    /// <summary>가장 작아질 때의 배율. 멀리 있는 표식이 너무 작아지지 않게 막는다.</summary>
    [SerializeField] private float _minScale = 0.7f;

    private readonly List<GameObject> _indicators = new();
    private readonly List<MeshRenderer> _renderers = new();
    private readonly List<Float3> _initialScales = new();

    private Float3 _start;
    private Float3 _end;
    private float _height;
    private bool _enabled;

    public override void Start()
    {
        foreach (GameObject child in GameObject.Children)
        {
            // 원본은 GetComponent 결과를 검사하지 않고 바로 m_Material을 만져서,
            // MeshRenderer가 없는 자식이 하나라도 섞이면 널 역참조로 죽었다.
            if (child.GetComponent<MeshRenderer>() is not { } renderer) continue;

            // 재질을 공유한 채로 색을 바꾸면 같은 재질을 쓰는 다른 오브젝트까지 함께 변한다.
            renderer.InstantiateMaterial("IndicatorMaterial");

            _indicators.Add(child);
            _renderers.Add(renderer);
            _initialScales.Add(child.Transform.LocalScale);
        }
    }

    public override void Update(float tick)
    {
        if (!_enabled) return;

        Float3 direction = _end - _start;
        float length = direction.Length;
        if (length < 1e-4f) return;

        // 이차 베지어의 제어점. 중간 지점을 height만큼 들어 올려 포물선을 만든다.
        Float3 control = _start + direction * 0.5f;
        control.Y += _height;

        for (int i = 0; i < _indicators.Count; ++i)
        {
            GameObject indicator = _indicators[i];
            if (!indicator.IsAlive) continue;

            indicator.SetEnabled(true);

            float t = (_interval * i) / length;

            Float3 a = Float3.Lerp(_start, control, t);
            Float3 b = Float3.Lerp(control, _end, t);
            indicator.Transform.SetLocalPosition(Float3.Lerp(a, b, t));

            // 곡선 가운데(t≈0.5)에서 가장 크고 진하게. 원본의 sin(t) 곡선을 그대로 둔다.
            float alpha = MathF.Sin(t);
            float scale = Math.Clamp(alpha * 3f, _minScale, 1f);

            indicator.Transform.SetLocalScale(_initialScales[i] * scale);
            _renderers[i].BaseColor = _renderers[i].BaseColor.WithAlpha(alpha);
        }
    }

    /// <summary>궤적의 시작·끝과 곡선 높이를 지정한다. 원본 <c>SetIndicator</c>.</summary>
    public void SetIndicator(Float3 start, Float3 end, float height)
    {
        // 바닥에 파묻히지 않도록 살짝 띄운다(원본과 같은 0.5).
        const float groundOffset = 0.5f;

        _start = start;
        _start.Y += groundOffset;

        _end = end;
        _end.Y += groundOffset;

        _height = height;
    }

    /// <summary>표식 전체를 켜고 끈다. 원본 <c>EnableIndicator</c>.</summary>
    public void EnableIndicator(bool enable)
    {
        _enabled = enable;

        foreach (GameObject indicator in _indicators)
        {
            if (indicator.IsAlive) indicator.SetEnabled(enable);
        }
    }
}
