namespace CreatorEngine.Scripts

open System
open CreatorEngine

type UiTextProbe() as this =
    inherit Component()
    static let mutable completed = 0
    static let mutable totalPassed = 0
    static let mutable totalFailed = 0
    let mutable frame = 0
    let mutable checkedResult = false
    let mutable passed = 0
    let mutable failed = 0
    do this._checkAfterFrames <- 20
    [<DefaultValue; SerializeField>]
    val mutable public _checkAfterFrames: int

    [<EngineCallable>]
    static member ResetResults() =
        completed <- 0
        totalPassed <- 0
        totalFailed <- 0

    [<EngineCallable>]
    static member Results() = $"{{\"completed\":{completed},\"passed\":{totalPassed},\"failed\":{totalFailed}}}"

    member private _.Assert(name: string, ok: bool, detail: string) =
        if ok then passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[UiTextProbe] 실패: {name} — {detail}")

    member this.PostPhysics(_tick: single) =
        frame <- frame + 1
        if not checkedResult && frame >= this._checkAfterFrames then
            checkedResult <- true
            let text = this.GetComponent<TextComponent>()
            let image = this.GetComponent<ImageComponent>()
            let canvas = this.GetComponent<Canvas>()
            if isNull text then this.Assert("Text coverage", false, "TextComponent missing")
            else
                let sample = "체력 100 / 안녕하세요"
                text.Message <- sample
                this.Assert("Message 왕복", text.Message = sample, text.Message)
                text.Message <- ""
                this.Assert("빈 문자열 왕복", text.Message.Length = 0, text.Message)
                text.Color <- Color4(1.0f, 0.5f, 0.25f, 0.75f)
                let color = text.Color
                this.Assert("Color 왕복", MathF.Abs(color.R - 1.0f) < 1e-4f && MathF.Abs(color.G - 0.5f) < 1e-4f && MathF.Abs(color.B - 0.25f) < 1e-4f && MathF.Abs(color.A - 0.75f) < 1e-4f, string color)
                text.Alpha <- 0.3f
                this.Assert("Alpha 왕복", MathF.Abs(text.Alpha - 0.3f) < 1e-4f, string text.Alpha)
                this.Assert("Alpha와 Color.A", MathF.Abs(text.Color.A - 0.3f) < 1e-4f, string text.Color.A)
                text.Color <- Color4.White
                text.FontSize <- 24.0f
                this.Assert("FontSize 왕복", MathF.Abs(text.FontSize - 24.0f) < 1e-3f, string text.FontSize)
                text.RelativePosition <- Float2(5.0f, -3.0f)
                this.Assert("RelativePosition 왕복", (text.RelativePosition - Float2(5.0f, -3.0f)).Length < 1e-3f, string text.RelativePosition)
                text.Order <- 7
                this.Assert("Text.Order 왕복", text.Order = 7, string text.Order)
            if not (isNull image) then
                image.Order <- 3
                this.Assert("Image.Order 왕복", image.Order = 3, string image.Order)
                if not (isNull text) then this.Assert("같은 오브젝트의 Order", text.Order = image.Order, $"Text={text.Order} Image={image.Order}")
            if isNull canvas then this.Assert("Canvas coverage", false, "Canvas missing")
            else
                canvas.Order <- 12
                this.Assert("Canvas.Order 왕복", canvas.Order = 12, string canvas.Order)
                canvas.Name <- "검증캔버스"
                this.Assert("Canvas.Name 왕복", canvas.Name = "검증캔버스", canvas.Name)
            completed <- completed + 1
            totalPassed <- totalPassed + passed
            totalFailed <- totalFailed + failed
            if failed = 0 then Component.Log($"[UiTextProbe] 전체 통과 ({passed}건)")
            else Component.LogError($"[UiTextProbe] {failed}건 실패 / {passed}건 통과")
