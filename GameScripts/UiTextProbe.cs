namespace CreatorEngine.Scripts;

/// <summary>Text · Canvas · UI 그리기 순서 바인딩 검증.</summary>
public sealed partial class UiTextProbe : Component
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

        CheckText();
        CheckOrder();
        CheckCanvas();

        if (_failed == 0) Log($"[UiTextProbe] 전체 통과 ({_passed}건)");
        else LogError($"[UiTextProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void CheckText()
    {
        var text = GetComponent<TextComponent>();
        if (text is null) { LogWarning("[UiTextProbe] TextComponent 없음 — 건너뜀"); return; }

        Log($"[UiTextProbe] Text — '{text.Message}' 색={text.Color} 크기={text.FontSize} 위치={text.RelativePosition}");

        // 한글이 섞인 문자열로 UTF-8 왕복까지 본다.
        const string sample = "체력 100 / 안녕하세요";
        text.Message = sample;
        Assert("Message 왕복(한글 포함)", text.Message == sample, $"'{text.Message}'");

        text.Message = "";
        Assert("빈 문자열 왕복", text.Message.Length == 0, $"'{text.Message}'");

        text.Color = new Color4(1f, 0.5f, 0.25f, 0.75f);
        Color4 c = text.Color;
        Assert("Color 왕복",
            MathF.Abs(c.R - 1f) < 1e-4f && MathF.Abs(c.G - 0.5f) < 1e-4f &&
            MathF.Abs(c.B - 0.25f) < 1e-4f && MathF.Abs(c.A - 0.75f) < 1e-4f, $"{c}");

        // 알파 전용 경로가 색의 w와 같은 자리를 가리켜야 한다.
        text.Alpha = 0.3f;
        Assert("Alpha 왕복", MathF.Abs(text.Alpha - 0.3f) < 1e-4f, $"{text.Alpha}");
        Assert("Alpha가 Color.A와 같은 값", MathF.Abs(text.Color.A - 0.3f) < 1e-4f, $"{text.Color.A}");

        text.Color = Color4.White;

        text.FontSize = 24f;
        Assert("FontSize 왕복", MathF.Abs(text.FontSize - 24f) < 1e-3f, $"{text.FontSize}");

        text.RelativePosition = new Float2(5f, -3f);
        Assert("RelativePosition 왕복",
            (text.RelativePosition - new Float2(5f, -3f)).Length < 1e-3f, $"{text.RelativePosition}");
    }

    /// <summary>그리기 순서는 UIComponent 공통이라 Text와 Image 어느 쪽이든 같은 자리를 봐야 한다.</summary>
    private void CheckOrder()
    {
        var text = GetComponent<TextComponent>();
        var image = GetComponent<ImageComponent>();

        if (text is null && image is null) { LogWarning("[UiTextProbe] UI 컴포넌트 없음 — 순서 검사 건너뜀"); return; }

        if (text is not null)
        {
            text.Order = 7;
            Assert("Text.Order 왕복", text.Order == 7, $"{text.Order}");
        }

        if (image is not null)
        {
            image.Order = 3;
            Assert("Image.Order 왕복", image.Order == 3, $"{image.Order}");
        }

        // 둘 다 있으면 같은 오브젝트라 기반 타입 조회가 하나를 고른다.
        // 어느 쪽이 잡히든 값이 서로 일관돼야 한다.
        if (text is not null && image is not null)
        {
            Assert("같은 오브젝트의 Order는 하나", text.Order == image.Order,
                $"Text={text.Order} Image={image.Order}");
        }
    }

    private void CheckCanvas()
    {
        var canvas = GetComponent<Canvas>();
        if (canvas is null) { LogWarning("[UiTextProbe] Canvas 없음 — 건너뜀"); return; }

        Log($"[UiTextProbe] Canvas — '{canvas.Name}' 순서={canvas.Order}");

        canvas.Order = 12;
        Assert("Canvas.Order 왕복", canvas.Order == 12, $"{canvas.Order}");

        canvas.Name = "검증캔버스";
        Assert("Canvas.Name 왕복", canvas.Name == "검증캔버스", $"'{canvas.Name}'");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[UiTextProbe] 실패: {name} — {detail}");
    }
}
