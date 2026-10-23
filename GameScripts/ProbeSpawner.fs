namespace CreatorEngine.Scripts

open System
open CreatorEngine

type ProbeSpawner() as this =
    inherit Component()

    let mutable frame = 0
    let mutable doneOnce = false

    do
        this._prefabName <- ""
        this._spawnAtFrame <- 30

    [<DefaultValue; SerializeField>]
    val mutable public _prefabName: string

    [<DefaultValue; SerializeField>]
    val mutable public _spawnAtFrame: int

    member this.PostPhysics(_: single) : unit =
        frame <- frame + 1

        if
            not doneOnce
            && frame >= this._spawnAtFrame
            && not (String.IsNullOrEmpty(this._prefabName))
        then
            doneOnce <- true
            let prefab = Prefab.Load(this._prefabName)

            if not prefab.IsValid then
                Component.LogError($"[ProbeSpawner] 프리팹 없음: {this._prefabName}")
            else
                Component.Log($"[ProbeSpawner] 엔진프레임 {Component.FrameCount} — Instantiate 호출 직전")
                let spawned = prefab.Instantiate("ProbeInstance")
                Component.Log($"[ProbeSpawner] 엔진프레임 {Component.FrameCount} — Instantiate 반환 (살아있음={spawned.IsAlive})")
