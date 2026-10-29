namespace CreatorEngine.Scripts

open CreatorEngine

type CurveIndicatorProbe() as this =
    inherit Component()
    let mutable indicator = Unchecked.defaultof<CurveIndicator>
    let mutable frame = 0
    let mutable driven = false
    let mutable checkedResult = false
    let mutable passed = 0
    let mutable failed = 0
    do this._checkAfterFrames <- 10
    [<DefaultValue; SerializeField>]
    val mutable public _checkAfterFrames: int

    member private _.Assert(name: string, ok: bool, detail: string) =
        if ok then passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[CurveIndicatorProbe] 실패: {name} — {detail}")

    member private this.Verify() =
        let renderers = ResizeArray<MeshRenderer>()
        for child in this.Entity.Children do
            let renderer = child.GetComponent<MeshRenderer>()
            if not (isNull renderer) then renderers.Add(renderer)
        this.Assert("MeshRenderer를 가진 직계 자식이 있음", renderers.Count > 0, $"{renderers.Count}개")
        if renderers.Count > 0 then
            for renderer in renderers do
                let owner = renderer.Entity
                this.Assert($"'{owner.Name}' 재질이 사본으로 교체됨", renderer.MaterialName.Contains("Indicator"), renderer.MaterialName)
                this.Assert($"'{owner.Name}' 알파가 0~1 범위", renderer.BaseColor.A >= 0.0f && renderer.BaseColor.A <= 1.0f, string renderer.BaseColor.A)
            this.Assert("없는 상수 이름은 false 반환", not (renderers[0].SetMaterialFloat("__없는값__", 1.0f)), "true가 나왔습니다")

    member this.PostPhysics(_tick: single) =
        if not checkedResult then
            if not driven then
                driven <- true
                indicator <- this.GetComponent<CurveIndicator>()
                if obj.ReferenceEquals(indicator, null) then
                    Component.LogError("[CurveIndicatorProbe] CurveIndicator를 찾지 못했습니다.")
                    checkedResult <- true
                else
                    indicator.SetIndicator(Float3(0.0f, 0.0f, 0.0f), Float3(0.0f, 0.0f, 30.0f), 5.0f)
                    indicator.EnableIndicator(true)
            else
                frame <- frame + 1
                if frame >= this._checkAfterFrames then
                    checkedResult <- true
                    this.Verify()
                    if failed = 0 then Component.Log($"[CurveIndicatorProbe] 전체 통과 ({passed}건)")
                    else Component.LogError($"[CurveIndicatorProbe] {failed}건 실패 / {passed}건 통과")
