namespace CreatorEngine.Scripts;

/// <summary>RectTransform · Image · Camera 바인딩 검증.</summary>
public sealed partial class UiProbe : Component
{
    private int _passed;
    private int _failed;

    public override void OnInitialized()
    {
        CheckRect();
        CheckImage();
        CheckCamera();

        if (_failed == 0) Log($"[UiProbe] 전체 통과 ({_passed}건)");
        else LogError($"[UiProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void CheckRect()
    {
        var rect = GetComponent<RectTransformComponent>();
        if (rect is null) { LogWarning("[UiProbe] RectTransformComponent 없음 — 건너뜀"); return; }

        Log($"[UiProbe] Rect — pos={rect.AnchoredPosition} size={rect.SizeDelta} pivot={rect.Pivot}");

        rect.AnchoredPosition = new Float2(120f, 340f);
        Assert("AnchoredPosition 왕복", Near(rect.AnchoredPosition, new Float2(120f, 340f)),
            $"{rect.AnchoredPosition}");

        rect.ScreenPosition = new Float2(320f, 180f);
        Assert("ScreenPosition 왕복", Near(rect.ScreenPosition, new Float2(320f, 180f)),
            $"{rect.ScreenPosition}");

        rect.SizeDelta = new Float2(64f, 32f);
        Assert("SizeDelta 왕복", Near(rect.SizeDelta, new Float2(64f, 32f)), $"{rect.SizeDelta}");

        rect.Pivot = new Float2(0.5f, 0.5f);
        Assert("Pivot 왕복", Near(rect.Pivot, new Float2(0.5f, 0.5f)), $"{rect.Pivot}");
    }

    private void CheckImage()
    {
        var image = GetComponent<ImageComponent>();
        if (image is null) { LogWarning("[UiProbe] ImageComponent 없음 — 건너뜀"); return; }

        Log($"[UiProbe] Image — color={image.Color} clip={image.ClipPercent} 텍스처 {image.TextureCount}개");

        image.Color = new Color4(1f, 0.5f, 0.25f, 0.75f);
        Color4 c = image.Color;
        Assert("Color 왕복",
            MathF.Abs(c.R - 1f) < 1e-4f && MathF.Abs(c.G - 0.5f) < 1e-4f &&
            MathF.Abs(c.B - 0.25f) < 1e-4f && MathF.Abs(c.A - 0.75f) < 1e-4f, $"{c}");

        image.Color = Color4.White;

        image.ClipPercent = 0.4f;
        Assert("ClipPercent 왕복", MathF.Abs(image.ClipPercent - 0.4f) < 1e-4f, $"{image.ClipPercent}");
        image.ClipPercent = 1f;

        // 범위를 벗어난 인덱스로도 죽지 않아야 한다.
        image.SetTexture(image.TextureCount + 5);
        Assert("범위 밖 SetTexture 안전", true, "");
    }

    private void CheckCamera()
    {
        if (!Camera.Exists) { LogWarning("[UiProbe] 카메라 없음 — 건너뜀"); return; }

        Float2 size = Camera.ScreenSize;
        Log($"[UiProbe] 화면 {size}");
        Assert("ScreenSize > 0", size.X > 0f && size.Y > 0f, $"{size}");

        // 자기 위치를 투영해 본다. 화면 안이든 밖이든 Z 부호로 앞/뒤가 갈려야 한다.
        Float3 self = Camera.WorldToScreenPoint(Transform.WorldPosition);
        Log($"[UiProbe] 자기 위치 투영 {self} (Z>0이면 카메라 앞)");

        // 카메라 뒤쪽으로 아주 멀리 보낸 점은 Z <= 0이어야 한다.
        Float3 behind = Camera.WorldToScreenPoint(new Float3(0f, 0f, -100000f));
        Float3 ahead = Camera.WorldToScreenPoint(new Float3(0f, 0f, 100000f));
        Assert("앞뒤 중 한쪽은 Z <= 0", behind.Z <= 0f || ahead.Z <= 0f,
            $"뒤={behind.Z} 앞={ahead.Z}");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[UiProbe] 실패: {name} — {detail}");
    }

    private static bool Near(Float2 a, Float2 b) => (a - b).Length < 1e-3f;
}
