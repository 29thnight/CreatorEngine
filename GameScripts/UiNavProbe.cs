namespace CreatorEngine.Scripts;

/// <summary>
/// UI 내비게이션·버튼·Image 잔여 바인딩 검증.
///
/// 선택 상태는 엔진 내비게이션이 스틱 입력으로 굴리는 것이라 무인 실행에서는
/// SetSelected로 직접 만들어 검증한다. 클릭도 마우스가 없으니 빈 폴링의 안전성과
/// 래치 규약(읽으면 내려감)을 본다.
/// </summary>
public sealed partial class UiNavProbe : Behaviour
{
    [SerializeField] private int _checkAfterFrames = 20;

    private int _frame;
    private bool _checked;
    private int _passed;
    private int _failed;

    public override void PostPhysics(float tick)
    {
        if (_checked || ++_frame < _checkAfterFrames) return;
        _checked = true;

        CheckSelection();
        CheckNavLock();
        CheckButton();
        CheckImageExtras();

        if (_failed == 0) Log($"[UiNavProbe] 전체 통과 ({_passed}건)");
        else LogError($"[UiNavProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void CheckSelection()
    {
        var image = GetComponent<ImageComponent>();
        if (image is null) { LogWarning("[UiNavProbe] ImageComponent 없음 — 선택 검사 건너뜀"); return; }

        // 자신을 선택 상태로 만들면 IsSelected가 참이어야 한다.
        UINavigation.Selected = GameObject;
        Assert("SetSelected 후 IsSelected", image.IsSelected, "false로 읽힘");
        Assert("Selected가 자기 자신", UINavigation.Selected == GameObject,
            $"{(UINavigation.Selected.IsAlive ? UINavigation.Selected.Name : "(없음)")}");

        Log($"[UiNavProbe] 선택 = '{UINavigation.Selected.Name}'");
    }

    private void CheckNavLock()
    {
        var image = GetComponent<ImageComponent>();
        if (image is null) return;

        bool start = image.NavLock;

        image.NavLock = true;
        Assert("NavLock = true 왕복", image.NavLock, "false로 읽힘");

        image.NavLock = false;
        Assert("NavLock = false 왕복", !image.NavLock, "true로 읽힘");

        image.NavLock = start;
    }

    private void CheckButton()
    {
        var button = GetComponent<UIButton>();
        if (button is null) { LogWarning("[UiNavProbe] UIButton 없음 — 클릭 검사 건너뜀"); return; }

        // 클릭이 없었으니 false여야 하고, 여러 번 읽어도 안전해야 한다.
        Assert("클릭 없음 → WasClicked=false", !button.WasClicked(), "true가 나옴");
        Assert("반복 폴링 안전", !button.WasClicked(), "true가 나옴");
    }

    private void CheckImageExtras()
    {
        var image = GetComponent<ImageComponent>();
        if (image is null) return;

        Assert("TextureIndex ≥ 0", image.TextureIndex >= 0, $"{image.TextureIndex}");

        float start = image.Rotation;
        image.Rotation = 1.5f;
        Assert("Rotation 왕복", MathF.Abs(image.Rotation - 1.5f) < 1e-4f, $"{image.Rotation}");
        image.Rotation = start;
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[UiNavProbe] 실패: {name} — {detail}");
    }
}
