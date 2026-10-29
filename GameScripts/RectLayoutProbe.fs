namespace CreatorEngine.Scripts

open System
open CreatorEngine

type RectLayoutProbe() as this =
    inherit Component()
    let mutable frame = 0
    let mutable baselineTaken = false
    let mutable verified = false
    let mutable canvasRect: RectTransformComponent = null
    let mutable childRect: RectTransformComponent = null
    let mutable childName = ""
    let mutable screenBefore = Float2.Zero
    let mutable childBefore = struct (0.0f, 0.0f, 0.0f, 0.0f)
    let mutable passed = 0
    let mutable failed = 0
    do
        this._canvasName <- "Canvas"
        this._baselineFrame <- 30
        this._verifyFrame <- 150
    [<DefaultValue; SerializeField>]
    val mutable public _canvasName: string
    [<DefaultValue; SerializeField>]
    val mutable public _baselineFrame: int
    [<DefaultValue; SerializeField>]
    val mutable public _verifyFrame: int

    member private _.Assert(name: string, ok: bool, detail: string) =
        if ok then passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[RectLayoutProbe] 실패: {name} — {detail}")

    member private this.TakeBaseline() =
        let canvas = Entity.Find(this._canvasName)
        if not canvas.IsAlive then
            Component.LogError($"[RectLayoutProbe] 캔버스 '{this._canvasName}' 없음")
            verified <- true
        else
            canvasRect <- canvas.GetComponent<RectTransformComponent>()
            if isNull canvasRect then
                Component.LogError("[RectLayoutProbe] 캔버스에 RectTransform 없음")
                verified <- true
            else
                screenBefore <- Camera.ScreenSize
                let struct (x, y, width, height) = canvasRect.WorldRect
                this.Assert("캔버스 크기 = 화면 크기", MathF.Abs(width - screenBefore.X) < 1.0f && MathF.Abs(height - screenBefore.Y) < 1.0f, $"캔버스 {width}x{height} vs 화면 {screenBefore}")
                this.Assert("캔버스 원점 = -size/2", MathF.Abs(x + width * 0.5f) < 1.0f && MathF.Abs(y + height * 0.5f) < 1.0f, $"원점 ({x}, {y})")
                for child in canvas.Children do
                    if isNull childRect then
                        let candidate = child.GetComponent<RectTransformComponent>()
                        if not (isNull candidate) then
                            childRect <- candidate
                            childName <- child.Name
                            childBefore <- candidate.WorldRect

    member private this.Verify() =
        if not (isNull canvasRect) then
            let screenAfter = Camera.ScreenSize
            let struct (x, _, width, height) = canvasRect.WorldRect
            if MathF.Abs(screenAfter.X - screenBefore.X) <= 1.0f && MathF.Abs(screenAfter.Y - screenBefore.Y) <= 1.0f then
                Component.LogWarning("[RectLayoutProbe] 화면 크기가 바뀌지 않음 — 추종 검사 건너뜀")
            else
                this.Assert("캔버스가 새 화면 크기를 추종", MathF.Abs(width - screenAfter.X) < 1.0f && MathF.Abs(height - screenAfter.Y) < 1.0f, $"캔버스 {width}x{height} vs 화면 {screenAfter}")
                this.Assert("변경 후에도 원점 = -size/2", MathF.Abs(x + width * 0.5f) < 1.0f, $"원점 X {x}, 크기 {width}")
                if not (isNull childRect) then
                    let struct (childX, childY, _, _) = childRect.WorldRect
                    Component.Log($"[RectLayoutProbe] 자식 '{childName}' {childBefore} → ({childX}, {childY})")
                    this.Assert("자식 rect가 유효", not (Single.IsNaN(childX)) && not (Single.IsNaN(childY)), $"({childX}, {childY})")
            if failed = 0 then Component.Log($"[RectLayoutProbe] 전체 통과 ({passed}건)")
            else Component.LogError($"[RectLayoutProbe] {failed}건 실패 / {passed}건 통과")

    member this.PostPhysics(_tick: single) =
        frame <- frame + 1
        if not baselineTaken && frame >= this._baselineFrame then
            baselineTaken <- true
            this.TakeBaseline()
        elif baselineTaken && not verified && frame >= this._verifyFrame then
            verified <- true
            this.Verify()
