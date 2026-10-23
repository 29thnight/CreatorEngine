namespace CreatorEngine.Scripts

open CreatorEngine

type EnableByName() as this =
    inherit Component()

    let mutable frame = 0
    let mutable doneOnce = false

    do
        this._target <- ""
        this._afterFrames <- 30

    [<DefaultValue; SerializeField>]
    val mutable public _target: string

    [<DefaultValue; SerializeField>]
    val mutable public _afterFrames: int

    member this.PostPhysics(_: single) : unit =
        // Preserve short-circuit behavior: completed probes no longer increment.
        if not doneOnce then
            frame <- frame + 1

            if frame >= this._afterFrames then
                doneOnce <- true

                if this._target.Length = 0 then
                    Component.LogWarning("[EnableByName] 대상 미지정")
                else
                    let target = Entity.Find(this._target)

                    if not target.IsAlive then
                        Component.LogError($"[EnableByName] '{this._target}' 를 찾지 못했습니다.")
                    else
                        target.SetEnabled(true)
                        Component.Log($"[EnableByName] '{this._target}' 활성화 (frame {Component.FrameCount})")
