namespace CreatorEngine.Scripts

open CreatorEngine

/// Cache an existing native wrapper once; no per-tick component query or F# scheduler.
type FSharpNativeMover() =
    inherit Component()

    let mutable transform = Unchecked.defaultof<Transform>

    [<DefaultValue; SerializeField>]
    val mutable Speed: single

    member this.OnBeginSimulation() : unit =
        transform <- this.GetComponent<Transform>()

    member this.OnTickSimulate(dt: single) : unit =
        if not (obj.ReferenceEquals(transform, null)) then
            transform.Translate(Float3(this.Speed * dt, 0.0f, 0.0f))
