namespace CreatorEngine.Scripts

open System
open CreatorEngine

type PhysicsLayerProbe() =
    inherit Component()

    [<EngineCallable>]
    static member BodyState() =
        let body = Entity.Find("LayerGateBody").GetComponent<PhysicsBodyComponent>()
        let mutable state = Unchecked.defaultof<PhysicsBodyState>

        if obj.ReferenceEquals(body, null) || body.ReadState(&state) <> PhysicsError.None then
            invalidOp "Layer gate body is unavailable"

        FormattableString.Invariant(
            $"{{\"componentId\":\"{body.ComponentId}\",\"y\":{state.Position.Y},\"velocityY\":{state.LinearVelocity.Y}}}"
        )

    [<EngineCallable>]
    static member ResetBody() =
        let owner = Entity.Find("LayerGateBody")
        let body = owner.GetComponent<PhysicsBodyComponent>()
        let transform = owner.Transform

        if obj.ReferenceEquals(body, null) || obj.ReferenceEquals(transform, null) then
            invalidOp "Layer gate body is unavailable"

        transform.WorldPosition <- Float3(4.0f, 4.0f, 0.0f)
        body.SetVelocity(Unchecked.defaultof<Float3>).ToString()
