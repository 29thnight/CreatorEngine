namespace CreatorEngine.Scripts

open CreatorEngine

/// The ordinary Scene defers initialization while this component stays disabled.
type DisabledLifecycleProbe() =
    inherit Component()

    static let mutable initialized = 0
    static let mutable enabled = 0
    static let mutable updated = 0
    static let mutable retired = 0

    static member Initialized = initialized
    static member EnabledCalls = enabled
    static member Updates = updated
    static member Retired = retired

    member this.OnInitialized() : unit =
        if this.Enabled then invalidOp "Authored disabled state was applied after initialization"
        initialized <- initialized + 1

    member _.OnEnable() : unit =
        enabled <- enabled + 1
        invalidOp "Authored disabled script received OnEnable"

    member _.OnTickSimulate(_dt: single) : unit =
        updated <- updated + 1
        invalidOp "Authored disabled script received OnTickSimulate"

    member _.OnUninitializing() : unit =
        retired <- retired + 1
