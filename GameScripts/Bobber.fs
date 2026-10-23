namespace CreatorEngine.Scripts

open System
open CreatorEngine

type Bobber() as this =
    inherit Component()

    let mutable origin = Unchecked.defaultof<Float3>
    let mutable elapsed = 0.0f

    do
        this._amplitude <- 1.0f
        this._speed <- 2.0f

    [<DefaultValue; SerializeField>]
    val mutable public _amplitude: float32

    [<DefaultValue; SerializeField>]
    val mutable public _speed: float32

    [<DefaultValue; SerializeField(DisplayName = "가로 흔들기")>]
    val mutable public _horizontal: bool

    member this.OnInitialized() : unit =
        Component.Log($"[Bobber] Initialized — {this.Entity.Name}")

    member this.OnBeginSimulation() : unit =
        origin <- this.Transform.LocalPosition
        Component.Log($"[Bobber] BeginSimulation — 기준 위치 {origin}")

    member this.PostPhysics(dt: single) : unit =
        elapsed <- elapsed + dt
        let offset = MathF.Sin(elapsed * this._speed) * this._amplitude

        this.Transform.LocalPosition <-
            origin
            + (if this._horizontal then
                   Float3(offset, 0.0f, 0.0f)
               else
                   Float3(0.0f, offset, 0.0f))

    member this.OnUninitializing() : unit =
        Component.Log("[Bobber] Uninitializing")
