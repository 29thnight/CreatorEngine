namespace CreatorEngine

#nowarn "9"
open System
open System.Runtime.InteropServices
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

type PhysicsError =
    | None = 0 | InvalidArgument = 1 | UnsupportedGeometry = 2 | StaleHandle = 3 | WrongScene = 4
    | WrongPhase = 5 | BackendInitialization = 6 | CookingFailed = 7 | CapacityExceeded = 8
    | OutOfMemory = 9 | LateCommand = 10 | DuplicateCommand = 11
type PhysicsBodyKind = | Static = 0 | Kinematic = 1 | Dynamic = 2
type PhysicsForceMode = | Force = 0 | Impulse = 1 | Acceleration = 2 | VelocityChange = 3
type PhysicsShapeKind = | Box = 0 | Sphere = 1 | Capsule = 2 | Convex = 3 | TriangleMesh = 4 | Heightfield = 5

[<Struct; StructLayout(LayoutKind.Sequential)>]
type PhysicsBodyState =
    val mutable Kind: PhysicsBodyKind
    val mutable Mass: single
    val mutable Position: Float3
    val mutable Rotation: Quaternion
    val mutable LinearVelocity: Float3
    val mutable AngularVelocity: Float3

[<Struct; StructLayout(LayoutKind.Sequential)>]
type PhysicsShapeState =
    val mutable ShapeId: uint32
    val mutable Kind: PhysicsShapeKind
    val mutable internal SensorValue: int
    val mutable internal QueryValue: int
    val mutable LayerOverride: uint64
    val mutable Radius: single
    val mutable HalfHeight: single
    val mutable HalfExtent: Float3
    val mutable LocalPosition: Float3
    val mutable LocalRotation: Quaternion
    val mutable internal Role: Guid
    member this.ContactRole = ShapeRole(this.Role)
    member this.Sensor = this.SensorValue <> 0
    member this.QueryEnabled = this.QueryValue <> 0

[<AllowNullLiteral; Sealed>]
type PhysicsBodyComponent internal (owner: ObjectHandle, instance: uint64) =
    inherit NativeComponent(owner, nativeInstance = instance)
    member _.ComponentId = instance
    override _.Enabled with get () = raise (NotSupportedException("Use Entity.SetEnabled to control native body activation.")) and set _ = raise (NotSupportedException("Use Entity.SetEnabled to control native body activation."))
    member this.ReadState(state: byref<PhysicsBodyState>) =
        state <- Unchecked.defaultof<_>
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Body_Read) ->
            use pointer = fixed &state
            enum<PhysicsError>(api.Body_Read.Invoke(this.OwnerHandle.ToNative(), instance, NativePtr.toNativeInt pointer))
        | _ -> PhysicsError.WrongPhase
    member this.Remove() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Body_Remove) -> enum<PhysicsError>(api.Body_Remove.Invoke(this.OwnerHandle.ToNative(), instance))
        | _ -> PhysicsError.WrongPhase
    member this.SetVelocity(linear: Float3) = this.SetVelocity(linear, Float3.Zero)
    member this.SetVelocity(linear: Float3, angular: Float3) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Body_Velocity) -> enum<PhysicsError>(api.Body_Velocity.Invoke(this.OwnerHandle.ToNative(), instance, linear.ToNative(), angular.ToNative()))
        | _ -> PhysicsError.WrongPhase
    member this.ApplyForce(linear: Float3) = this.ApplyForce(linear, Float3.Zero, PhysicsForceMode.Force)
    member this.ApplyForce(linear: Float3, angular: Float3) = this.ApplyForce(linear, angular, PhysicsForceMode.Force)
    member this.ApplyForce(linear: Float3, mode: PhysicsForceMode) = this.ApplyForce(linear, Float3.Zero, mode)
    member this.ApplyForce(linear: Float3, angular: Float3, mode: PhysicsForceMode) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Body_Force) -> enum<PhysicsError>(api.Body_Force.Invoke(this.OwnerHandle.ToNative(), instance, linear.ToNative(), angular.ToNative(), int mode))
        | _ -> PhysicsError.WrongPhase
    member this.GetShapeCount(count: byref<int>) =
        count <- 0
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Body_ShapeCount) ->
            use output = fixed &count
            enum<PhysicsError>(api.Body_ShapeCount.Invoke(this.OwnerHandle.ToNative(), instance, NativePtr.toNativeInt output))
        | _ -> PhysicsError.WrongPhase
    member this.GetShape(index: int, state: byref<PhysicsShapeState>) =
        state <- Unchecked.defaultof<_>
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Body_ShapeRead) ->
            use output = fixed &state
            enum<PhysicsError>(api.Body_ShapeRead.Invoke(this.OwnerHandle.ToNative(), instance, index, NativePtr.toNativeInt output))
        | _ -> PhysicsError.WrongPhase
    member this.SetShapeFlags(id: uint32, sensor: bool, queryEnabled: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Body_ShapeFlags) ->
            enum<PhysicsError>(api.Body_ShapeFlags.Invoke(this.OwnerHandle.ToNative(), instance, id, (if sensor then 1 else 0), (if queryEnabled then 1 else 0)))
        | _ -> PhysicsError.WrongPhase
    member this.SetShapeRole(id: uint32, role: ShapeRole) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Body_ShapeRole) ->
            let value = Text.Encoding.UTF8.GetBytes((if role.Id = Guid.Empty then "" else role.Id.ToString("D")) + "\000")
            use pointer = fixed value
            enum<PhysicsError>(api.Body_ShapeRole.Invoke(this.OwnerHandle.ToNative(), instance, id, NativePtr.toNativeInt pointer))
        | _ -> PhysicsError.WrongPhase
    static member Find(owner: Entity, componentId: uint64) =
        let candidate = PhysicsBodyComponent(owner.Handle, componentId)
        let mutable state = Unchecked.defaultof<PhysicsBodyState>
        if componentId = 0UL || candidate.ReadState(&state) <> PhysicsError.None then null else candidate

[<Flags>]
type CharacterCollisionFlags =
    | None = 0u | Sides = 1u | Above = 2u | Below = 4u | Simulating = 8u | Forced = 16u | JumpQueued = 32u

[<Struct; StructLayout(LayoutKind.Sequential)>]
type CharacterMovementState =
    val mutable Position: Float3
    val mutable FootPosition: Float3
    val mutable ActualDisplacement: Float3
    val mutable DesiredVelocity: Float3
    val mutable FallVelocity: single
    val mutable Flags: CharacterCollisionFlags
    val mutable Tick: uint64
    val mutable MovementVelocity: Float3
    val mutable ForcedRemaining: double
    member this.Sides = (this.Flags &&& CharacterCollisionFlags.Sides) <> CharacterCollisionFlags.None
    member this.Above = (this.Flags &&& CharacterCollisionFlags.Above) <> CharacterCollisionFlags.None
    member this.Below = (this.Flags &&& CharacterCollisionFlags.Below) <> CharacterCollisionFlags.None
    member this.Forced = (this.Flags &&& CharacterCollisionFlags.Forced) <> CharacterCollisionFlags.None
    member this.JumpQueued = (this.Flags &&& CharacterCollisionFlags.JumpQueued) <> CharacterCollisionFlags.None
    member this.Simulating = (this.Flags &&& CharacterCollisionFlags.Simulating) <> CharacterCollisionFlags.None

[<AllowNullLiteral; Sealed>]
type CharacterMovementComponent internal (owner: ObjectHandle, instance: uint64) =
    inherit NativeComponent(owner, nativeInstance = instance)
    member _.ComponentId = instance
    override _.Enabled with get () = raise (NotSupportedException("Use Entity.SetEnabled to control native character activation.")) and set _ = raise (NotSupportedException("Use Entity.SetEnabled to control native character activation."))
    member this.ReadState(state: byref<CharacterMovementState>) =
        state <- Unchecked.defaultof<_>
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Character_Read) ->
            use output = fixed &state
            enum<PhysicsError>(api.Character_Read.Invoke(this.OwnerHandle.ToNative(), instance, NativePtr.toNativeInt output))
        | _ -> PhysicsError.WrongPhase
    member this.SetDesiredVelocity(velocity: Float3) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Character_Velocity) -> enum<PhysicsError>(api.Character_Velocity.Invoke(this.OwnerHandle.ToNative(), instance, velocity.ToNative()))
        | _ -> PhysicsError.WrongPhase
    static member CreatePlanarVelocity(input: Float3, speed: single, velocity: byref<Float3>) =
        velocity <- Float3.Zero
        if not (Single.IsFinite(input.X) && Single.IsFinite(input.Y) && Single.IsFinite(input.Z) && Single.IsFinite(speed)) || speed < 0.0f then
            PhysicsError.InvalidArgument
        else
            let x, z = double input.X, double input.Z
            let scale = double speed / max 1.0 (sqrt (x*x + z*z))
            velocity <- Float3(single (x*scale), 0.0f, single (z*scale))
            PhysicsError.None
    member this.SetPlanarInput(input: Float3, speed: single) =
        let mutable velocity = Float3.Zero
        let result = CharacterMovementComponent.CreatePlanarVelocity(input, speed, &velocity)
        if result = PhysicsError.None then this.SetDesiredVelocity(velocity) else result
    member this.Jump() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Character_Jump) -> enum<PhysicsError>(api.Character_Jump.Invoke(this.OwnerHandle.ToNative(), instance))
        | _ -> PhysicsError.WrongPhase
    member this.ForceVelocity(velocity: Float3, seconds: double) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Character_Force) -> enum<PhysicsError>(api.Character_Force.Invoke(this.OwnerHandle.ToNative(), instance, velocity.ToNative(), seconds))
        | _ -> PhysicsError.WrongPhase
    member this.CancelForcedVelocity() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Character_CancelForce) -> enum<PhysicsError>(api.Character_CancelForce.Invoke(this.OwnerHandle.ToNative(), instance))
        | _ -> PhysicsError.WrongPhase
    member this.Teleport(position: Float3) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Character_Teleport) -> enum<PhysicsError>(api.Character_Teleport.Invoke(this.OwnerHandle.ToNative(), instance, position.ToNative()))
        | _ -> PhysicsError.WrongPhase
    static member Find(owner: Entity, componentId: uint64) =
        let candidate = CharacterMovementComponent(owner.Handle, componentId)
        let mutable state = Unchecked.defaultof<CharacterMovementState>
        if componentId = 0UL || candidate.ReadState(&state) <> PhysicsError.None then null else candidate

[<AbstractClass; Sealed>]
type internal PhysicsComponents private () =
    static member Register() =
        Component.RegisterNative(typeof<PhysicsBodyComponent>, Func<ObjectHandle, Component>(fun owner ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Body_Find) ->
                let instance = api.Body_Find.Invoke(owner.ToNative())
                if instance = 0UL then null else PhysicsBodyComponent(owner, instance) :> Component
            | _ -> null))
        Component.RegisterNative(typeof<CharacterMovementComponent>, Func<ObjectHandle, Component>(fun owner ->
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Character_Find) ->
                let instance = api.Character_Find.Invoke(owner.ToNative())
                if instance = 0UL then null else CharacterMovementComponent(owner, instance) :> Component
            | _ -> null))
