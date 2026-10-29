namespace CreatorEngine.Scripts

open System
open CreatorEngine

type UiNavProbe() as this =
    inherit Component()
    let mutable frame = 0
    let mutable checkedResult = false
    let mutable passed = 0
    let mutable failed = 0
    do this._checkAfterFrames <- 20
    [<DefaultValue; SerializeField>]
    val mutable public _checkAfterFrames: int

    member private _.Assert(name: string, ok: bool, detail: string) =
        if ok then passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[UiNavProbe] 실패: {name} — {detail}")

    member this.PostPhysics(_tick: single) =
        frame <- frame + 1
        if not checkedResult && frame >= this._checkAfterFrames then
            checkedResult <- true
            let image = this.GetComponent<ImageComponent>()
            if isNull image then
                Component.LogWarning("[UiNavProbe] ImageComponent 없음 — 선택 검사 건너뜀")
            else
                UINavigation.Selected <- this.Entity
                this.Assert("SetSelected 후 IsSelected", image.IsSelected, "false로 읽힘")
                this.Assert("Selected가 자기 자신", UINavigation.Selected = this.Entity, UINavigation.Selected.Name)
                let initial = image.NavLock
                image.NavLock <- true
                this.Assert("NavLock = true 왕복", image.NavLock, "false로 읽힘")
                image.NavLock <- false
                this.Assert("NavLock = false 왕복", not image.NavLock, "true로 읽힘")
                image.NavLock <- initial
                this.Assert("TextureIndex ≥ 0", image.TextureIndex >= 0, string image.TextureIndex)
                let rotation = image.Rotation
                image.Rotation <- 1.5f
                this.Assert("Rotation 왕복", MathF.Abs(image.Rotation - 1.5f) < 1e-4f, string image.Rotation)
                image.Rotation <- rotation
            let button = this.GetComponent<UIButton>()
            if isNull button then Component.LogWarning("[UiNavProbe] UIButton 없음 — 클릭 검사 건너뜀")
            else
                this.Assert("클릭 없음 → WasClicked=false", not (button.WasClicked()), "true가 나옴")
                this.Assert("반복 폴링 안전", not (button.WasClicked()), "true가 나옴")
            if failed = 0 then Component.Log($"[UiNavProbe] 전체 통과 ({passed}건)")
            else Component.LogError($"[UiNavProbe] {failed}건 실패 / {passed}건 통과")
