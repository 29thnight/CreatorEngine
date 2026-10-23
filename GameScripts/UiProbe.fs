namespace CreatorEngine.Scripts

open CreatorEngine

type UiProbe() =
    inherit Component()

    let mutable passed = 0
    let mutable failed = 0

    static member private Near(actual: Float2, expected: Float2) = (actual - expected).Length < 1e-3f

    member private _.Assert(name, condition, detail) =
        if condition then
            passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[UiProbe] 실패: {name} — {detail}")

    member private this.CheckRect() =
        let rect = this.GetComponent<RectTransformComponent>()

        if obj.ReferenceEquals(rect, null) then
            Component.LogWarning("[UiProbe] RectTransformComponent 없음 — 건너뜀")
        else
            Component.Log($"[UiProbe] Rect — pos={rect.AnchoredPosition} size={rect.SizeDelta} pivot={rect.Pivot}")

            rect.AnchoredPosition <- Float2(120.0f, 340.0f)

            this.Assert(
                "AnchoredPosition 왕복",
                UiProbe.Near(rect.AnchoredPosition, Float2(120.0f, 340.0f)),
                string rect.AnchoredPosition
            )

            rect.ScreenPosition <- Float2(320.0f, 180.0f)

            this.Assert(
                "ScreenPosition 왕복",
                UiProbe.Near(rect.ScreenPosition, Float2(320.0f, 180.0f)),
                string rect.ScreenPosition
            )

            rect.SizeDelta <- Float2(64.0f, 32.0f)
            this.Assert("SizeDelta 왕복", UiProbe.Near(rect.SizeDelta, Float2(64.0f, 32.0f)), string rect.SizeDelta)

            rect.Pivot <- Float2(0.5f, 0.5f)
            this.Assert("Pivot 왕복", UiProbe.Near(rect.Pivot, Float2(0.5f, 0.5f)), string rect.Pivot)

    member private this.CheckImage() =
        let image = this.GetComponent<ImageComponent>()

        if obj.ReferenceEquals(image, null) then
            Component.LogWarning("[UiProbe] ImageComponent 없음 — 건너뜀")
        else
            Component.Log($"[UiProbe] Image — color={image.Color} clip={image.ClipPercent} 텍스처 {image.TextureCount}개")

            image.Color <- Color4(1.0f, 0.5f, 0.25f, 0.75f)
            let color = image.Color

            let colorMatches =
                abs (color.R - 1.0f) < 1e-4f
                && abs (color.G - 0.5f) < 1e-4f
                && abs (color.B - 0.25f) < 1e-4f
                && abs (color.A - 0.75f) < 1e-4f

            this.Assert("Color 왕복", colorMatches, string color)
            image.Color <- Color4.White

            image.ClipPercent <- 0.4f
            this.Assert("ClipPercent 왕복", abs (image.ClipPercent - 0.4f) < 1e-4f, string image.ClipPercent)
            image.ClipPercent <- 1.0f

            image.SetTexture(image.TextureCount + 5)
            this.Assert("범위 밖 SetTexture 안전", true, "")

    member private this.CheckCamera() =
        if not Camera.Exists then
            Component.LogWarning("[UiProbe] 카메라 없음 — 건너뜀")
        else
            let size = Camera.ScreenSize
            Component.Log($"[UiProbe] 화면 {size}")
            this.Assert("ScreenSize > 0", size.X > 0.0f && size.Y > 0.0f, string size)

            let projectedOwner = Camera.WorldToScreenPoint(this.Transform.WorldPosition)
            Component.Log($"[UiProbe] 자기 위치 투영 {projectedOwner} (Z>0이면 카메라 앞)")

            let behind = Camera.WorldToScreenPoint(Float3(0.0f, 0.0f, -100000.0f))
            let ahead = Camera.WorldToScreenPoint(Float3(0.0f, 0.0f, 100000.0f))
            this.Assert("앞뒤 중 한쪽은 Z <= 0", behind.Z <= 0.0f || ahead.Z <= 0.0f, $"뒤={behind.Z} 앞={ahead.Z}")

    member this.OnInitialized() : unit =
        this.CheckRect()
        this.CheckImage()
        this.CheckCamera()

        if failed = 0 then
            Component.Log($"[UiProbe] 전체 통과 ({passed}건)")
        else
            Component.LogError($"[UiProbe] {failed}건 실패 / {passed}건 통과")
