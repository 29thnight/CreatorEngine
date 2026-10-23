namespace CreatorEngine.Scripts

open System
open CreatorEngine

type FollowTarget() as this =
    inherit Component()

    let mutable warnedMissing = false

    do
        this._offset <- Float3(0.0f, 2.0f, 0.0f)
        this._smoothing <- 5.0f
        this._label <- "따라가기"

    [<DefaultValue; SerializeField>]
    val mutable public _target: Entity

    [<DefaultValue; SerializeField>]
    val mutable public _offset: Float3

    [<DefaultValue; SerializeField>]
    val mutable public _smoothing: float32

    [<DefaultValue; SerializeField(DisplayName = "표시 이름")>]
    val mutable public _label: string

    member this.OnBeginSimulation() : unit =
        let target = if this._target.IsAlive then this._target.Name else "없음"
        Component.Log($"[FollowTarget] '{this._label}' 시작 — 대상 {target}")

    member this.PostPhysics(dt: single) : unit =
        if not this._target.IsAlive then
            if not warnedMissing then
                warnedMissing <- true
                Component.LogWarning($"[FollowTarget] '{this._label}' 대상이 없어 대기합니다")
        else
            warnedMissing <- false
            let target = this._target.Transform

            if not (obj.ReferenceEquals(target, null)) then
                let goal = target.LocalPosition + this._offset
                let here = this.Transform.LocalPosition
                this.Transform.LocalPosition <- here + (goal - here) * (1.0f - MathF.Exp(-this._smoothing * dt))
