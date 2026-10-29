namespace CreatorEngine

#nowarn "9"
open System
open System.Runtime.InteropServices
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<Struct; StructLayout(LayoutKind.Sequential)>]
type PhysicsHit =
    val mutable internal Object: ObjectHandle
    val mutable ComponentId: uint64
    val mutable ShapeId: uint32
    val mutable Face: uint32
    val mutable LayerId: uint64
    val mutable Point: Float3
    val mutable Normal: Float3
    val mutable Distance: single
    val mutable internal Location: int
    member this.Entity = Entity(this.Object)
    member this.HasLocation = this.Location <> 0

[<Struct; StructLayout(LayoutKind.Sequential)>]
type internal NativePhysicsQueryResult =
    val mutable Written: int
    val mutable RequiredCapacity: int
    val mutable Truncated: int

[<Struct>]
type PhysicsQueryResult =
    val Error: PhysicsError
    val Written: int
    val RequiredCapacity: int
    val Truncated: bool
    new (error, written, required, truncated) = { Error = error; Written = written; RequiredCapacity = required; Truncated = truncated }
    member this.Succeeded = this.Error = PhysicsError.None

[<Struct; StructLayout(LayoutKind.Sequential)>]
type PhysicsQueryRequest =
    val private Kind: int
    val Origin: Float3
    val Direction: Float3
    val DistanceOrRadius: single
    val Layers: uint32
    val private Sensors: int
    val Offset: int
    val Capacity: int
    private new(kind, origin, direction, distance, offset, capacity, layers, sensors) =
        { Kind = kind; Origin = origin; Direction = direction; DistanceOrRadius = distance; Layers = layers
          Sensors = (if sensors then 1 else 0); Offset = offset; Capacity = capacity }
    static member Raycast(origin, direction, distance, offset, capacity, layers: uint32, includeSensors: bool) =
        PhysicsQueryRequest(0, origin, direction, distance, offset, capacity, layers, includeSensors)
    static member Raycast(origin, direction, distance, offset, capacity) =
        PhysicsQueryRequest.Raycast(origin, direction, distance, offset, capacity, UInt32.MaxValue, false)
    static member Raycast(origin, direction, distance, offset, capacity, layers: uint32) =
        PhysicsQueryRequest.Raycast(origin, direction, distance, offset, capacity, layers, false)
    static member OverlapSphere(origin, radius, offset, capacity, layers: uint32, includeSensors: bool) =
        PhysicsQueryRequest(1, origin, Float3.Zero, radius, offset, capacity, layers, includeSensors)
    static member OverlapSphere(origin, radius, offset, capacity) =
        PhysicsQueryRequest.OverlapSphere(origin, radius, offset, capacity, UInt32.MaxValue, false)
    static member OverlapSphere(origin, radius, offset, capacity, layers: uint32) =
        PhysicsQueryRequest.OverlapSphere(origin, radius, offset, capacity, layers, false)

[<Struct; StructLayout(LayoutKind.Sequential)>]
type PhysicsBatchResult =
    val mutable Error: PhysicsError
    val mutable Written: int
    val mutable RequiredCapacity: int
    val mutable private TruncatedValue: int
    member this.Truncated = this.TruncatedValue <> 0
    member this.Succeeded = this.Error = PhysicsError.None

[<AbstractClass; Sealed>]
type Physics private () =
    static let mutable contacts: IContactDispatcher option = None
    static member internal SetContacts(value: IContactDispatcher option) = contacts <- value
    static member AllLayers = UInt32.MaxValue
    static member MaxQueryCapacity = 256
    static member MaxBatchRequests = 64
    static member MaxBatchHitCapacity = 4096
    static member ObserveContacts(owner: Entity, selfRole: ShapeRole, otherRole: ShapeRole, phases: ContactPhases, capacity: int, grouping: ContactGrouping) =
        match contacts with
        | Some router when not (isNull router.Current) -> router.Observe(router.Current, owner.Handle, selfRole, otherRole, phases, capacity, grouping)
        | _ -> invalidOp "ObserveContacts requires OnBeginSimulation."
    static member ObserveContacts(owner: Entity, selfRole: ShapeRole, otherRole: ShapeRole) =
        Physics.ObserveContacts(owner, selfRole, otherRole, ContactPhases.Begin, 256, ContactGrouping.ShapePairs)
    static member ObserveContacts(owner: Entity, selfRole: ShapeRole, otherRole: ShapeRole, phases: ContactPhases) =
        Physics.ObserveContacts(owner, selfRole, otherRole, phases, 256, ContactGrouping.ShapePairs)
    static member ObserveContacts(owner: Entity, selfRole: ShapeRole, otherRole: ShapeRole, phases: ContactPhases, capacity: int) =
        Physics.ObserveContacts(owner, selfRole, otherRole, phases, capacity, ContactGrouping.ShapePairs)
    static member BindContactRole(body: PhysicsBodyComponent, shapeId: uint32, role: ShapeRole) =
        match contacts with
        | Some router when not (isNull router.Current) ->
            let mutable count = 0
            if role.Id = Guid.Empty || body.GetShapeCount(&count) <> PhysicsError.None then invalidArg "body" "Invalid body or role."
            let mutable found = false
            let mutable index = 0
            while index < count && not found do
                let mutable shape = Unchecked.defaultof<PhysicsShapeState>
                if body.GetShape(index, &shape) = PhysicsError.None && shape.ShapeId = shapeId then
                    if shape.ContactRole.Id <> Guid.Empty then invalidOp "Authored contact role cannot be rebound by a script."
                    found <- true
                index <- index + 1
            if not found then invalidArg "shapeId" "Shape does not belong to the body."
            router.BindRole(body.OwnerHandle, body.ComponentId, shapeId, role, router.Current.Lifetime)
        | _ -> invalidOp "Role binding requires OnBeginSimulation."
    static member QueryBatch(anchor: Entity, requests: ReadOnlySpan<PhysicsQueryRequest>, hits: Span<PhysicsHit>, results: Span<PhysicsBatchResult>) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Physics_QueryBatch) ->
            if requests.Length <> results.Length || requests.Length > 64 || hits.Length > 4096 then PhysicsError.InvalidArgument
            else
                use input = fixed &MemoryMarshal.GetReference(requests)
                use output = fixed &MemoryMarshal.GetReference(hits)
                use summaries = fixed &MemoryMarshal.GetReference(results)
                enum<PhysicsError>(api.Physics_QueryBatch.Invoke(anchor.Handle.ToNative(), NativePtr.toNativeInt input, requests.Length, NativePtr.toNativeInt output, hits.Length, NativePtr.toNativeInt summaries))
        | _ -> PhysicsError.WrongPhase
    static member private Query(anchor: Entity, kind: int, origin: Float3, direction: Float3, distance: single, results: Span<PhysicsHit>, layers: uint32, sensors: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Physics_Query) ->
            if results.Length > 256 then PhysicsQueryResult(PhysicsError.InvalidArgument, 0, 0, false)
            else
                let mutable summary = Unchecked.defaultof<NativePhysicsQueryResult>
                use output = fixed &MemoryMarshal.GetReference(results)
                use summaryPointer = fixed &summary
                let error = enum<PhysicsError>(api.Physics_Query.Invoke(anchor.Handle.ToNative(), kind, origin.ToNative(), direction.ToNative(), distance, layers, (if sensors then 1 else 0), NativePtr.toNativeInt output, results.Length, NativePtr.toNativeInt summaryPointer))
                if error = PhysicsError.None then PhysicsQueryResult(error, summary.Written, summary.RequiredCapacity, summary.Truncated <> 0)
                else PhysicsQueryResult(error, 0, 0, false)
        | _ -> PhysicsQueryResult(PhysicsError.WrongPhase, 0, 0, false)
    static member Raycast(anchor, origin, direction, distance, results: Span<PhysicsHit>, layers, includeSensors) =
        Physics.Query(anchor, 0, origin, direction, distance, results, layers, includeSensors)
    static member Raycast(anchor, origin, direction, distance, results: Span<PhysicsHit>) =
        Physics.Raycast(anchor, origin, direction, distance, results, UInt32.MaxValue, false)
    static member Raycast(anchor, origin, direction, distance, results: Span<PhysicsHit>, layers: uint32) =
        Physics.Raycast(anchor, origin, direction, distance, results, layers, false)
    static member OverlapSphere(anchor, origin, radius, results: Span<PhysicsHit>, layers, includeSensors) =
        Physics.Query(anchor, 1, origin, Float3.Zero, radius, results, layers, includeSensors)
    static member OverlapSphere(anchor, origin, radius, results: Span<PhysicsHit>) =
        Physics.OverlapSphere(anchor, origin, radius, results, UInt32.MaxValue, false)
    static member OverlapSphere(anchor, origin, radius, results: Span<PhysicsHit>, layers: uint32) =
        Physics.OverlapSphere(anchor, origin, radius, results, layers, false)
