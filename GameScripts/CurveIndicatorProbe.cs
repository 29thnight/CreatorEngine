namespace CreatorEngine.Scripts;

/// <summary>
/// 이관한 <see cref="CurveIndicator"/>를 실제로 구동해 검증한다.
///
/// SetIndicator·EnableIndicator는 다른 스크립트가 부르는 API라 CLI로는 건드릴 수 없다.
/// 그래서 호출부 역할을 대신하는데, 덤으로 스크립트끼리의 GetComponent도 함께 검증된다.
/// 구동은 Update 첫 프레임에 한다 — Start 순서에 기대지 않기 위해서다.
/// </summary>
public sealed partial class CurveIndicatorProbe : Component
{
    [SerializeField] private int _checkAfterFrames = 10;

    private CurveIndicator? _indicator;
    private int _frame;
    private bool _driven;
    private bool _checked;

    private int _passed;
    private int _failed;

    public override void PostPhysics(float tick)
    {
        if (_checked) return;

        if (!_driven)
        {
            _driven = true;
            _indicator = GetComponent<CurveIndicator>();

            if (_indicator is null)
            {
                LogError("[CurveIndicatorProbe] CurveIndicator를 찾지 못했습니다.");
                _checked = true;
                return;
            }

            _indicator.SetIndicator(new Float3(0f, 0f, 0f), new Float3(0f, 0f, 30f), 5f);
            _indicator.EnableIndicator(true);
            Log("[CurveIndicatorProbe] 궤적 지정 후 활성화 — (0,0,0) → (0,0,30), 높이 5");
            return;
        }

        if (++_frame < _checkAfterFrames) return;
        _checked = true;

        Verify();

        if (_failed == 0) Log($"[CurveIndicatorProbe] 전체 통과 ({_passed}건)");
        else LogError($"[CurveIndicatorProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void Verify()
    {
        // CurveIndicator는 직계 자식만 다룬다(원본과 같다). GetComponentsInChildren는
        // 손자까지 훑으므로 여기서 쓰면 대상이 아닌 것까지 검사하게 된다.
        var renderers = new List<MeshRenderer>();
        foreach (Entity child in Entity.Children)
        {
            if (child.GetComponent<MeshRenderer>() is { } renderer) renderers.Add(renderer);
        }

        Assert("MeshRenderer를 가진 직계 자식이 있음", renderers.Count > 0, $"{renderers.Count}개");
        if (renderers.Count == 0) return;

        foreach (MeshRenderer renderer in renderers)
        {
            Entity owner = renderer.Entity;
            Log($"[CurveIndicatorProbe] {owner.Name} — pos={owner.Transform?.LocalPosition} " +
                $"scale={owner.Transform?.LocalScale} 재질='{renderer.MaterialName}' 색={renderer.BaseColor}");

            // 재질 사본을 만들었으므로 이름에 지정한 문자열이 들어가야 한다.
            Assert($"'{owner.Name}' 재질이 사본으로 교체됨",
                renderer.MaterialName.Contains("Indicator"), $"'{renderer.MaterialName}'");

            // t=0이면 alpha=sin(0)=0이라 첫 표식은 완전 투명이 맞다.
            Assert($"'{owner.Name}' 알파가 0~1 범위",
                renderer.BaseColor.A >= 0f && renderer.BaseColor.A <= 1f, $"{renderer.BaseColor.A}");
        }

        // 셰이더 상수 넣기 경로도 확인한다. 없는 이름이면 false여야 한다.
        Assert("없는 상수 버퍼는 false 반환",
            !renderers[0].SetMaterialFloat("__없는버퍼__", "__없는값__", 1f), "true가 나왔습니다");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[CurveIndicatorProbe] 실패: {name} — {detail}");
    }
}
