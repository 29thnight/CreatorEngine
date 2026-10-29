namespace CreatorEngine

open System
open System.Collections.Generic
open System.Runtime.InteropServices

[<Flags>]
type ContactPhases =
    | Begin = 1
    | Persist = 2
    | End = 4
    | All = 7

type ContactGrouping =
    | ShapePairs = 0
    | SensorTargets = 1

[<Struct>]
type ShapeRole =
    val Id: Guid
    new(id) = { Id = id }

[<AbstractClass; Sealed>]
type ShapeRoles private () =
    static member Attack = ShapeRole(Guid("67adfded-47c8-4ef9-9c38-d1c3497a7421"))
    static member Hurt = ShapeRole(Guid("7d352a65-f9bd-4bd5-83cf-97d1d276f740"))

[<Struct; StructLayout(LayoutKind.Sequential)>]
type internal ContactEndpoint =
    val mutable Owner: ObjectHandle
    val mutable Component: uint64
    val mutable Shape: uint32
    val mutable BodyGeneration: uint32
    val mutable BodySlot: uint32
    val mutable Reserved: uint32
    val mutable Role: Guid

[<Struct; StructLayout(LayoutKind.Sequential)>]
type internal NativeContact =
    val mutable First: ContactEndpoint
    val mutable Second: ContactEndpoint
    val mutable Tick: uint64
    val mutable Kind: int
    val mutable ContactCount: int
    val mutable RequiredContacts: int
    val mutable Point: Float3

[<Struct>]
type Contact =
    val private Self: ContactEndpoint
    val private Other: ContactEndpoint
    val Tick: uint64
    val Phase: ContactPhases
    val Sensor: bool
    val ContactCount: int
    val RequiredContacts: int
    val Point: Float3
    internal new (value: inref<NativeContact>, reverse: bool, phase: ContactPhases) =
        { Self = if reverse then value.Second else value.First
          Other = if reverse then value.First else value.Second
          Tick = value.Tick; Phase = phase; Sensor = value.Kind >= 3
          ContactCount = value.ContactCount; RequiredContacts = value.RequiredContacts; Point = value.Point }
    member this.SelfEntity = Entity(this.Self.Owner)
    member this.OtherEntity = Entity(this.Other.Owner)
    member this.SelfComponentId = this.Self.Component
    member this.OtherComponentId = this.Other.Component
    member this.SelfShapeId = this.Self.Shape
    member this.OtherShapeId = this.Other.Shape
    member this.SelfBodySlot = this.Self.BodySlot
    member this.OtherBodySlot = this.Other.BodySlot
    member this.SelfBodyGeneration = this.Self.BodyGeneration
    member this.OtherBodyGeneration = this.Other.BodyGeneration

[<AllowNullLiteral; Sealed>]
type ContactStream internal (subscriber: Component, owner: ObjectHandle, selfRole: ShapeRole, otherRole: ShapeRole,
                             phases: ContactPhases, capacity: int, grouping: ContactGrouping,
                             isUpdating: unit -> bool, remove: ContactStream -> unit) as this =
    let thread = Environment.CurrentManagedThreadId
    let buffer = Array.zeroCreate<Contact> capacity
    let tickBuffer = Array.zeroCreate<Contact> capacity
    let sensorPairs = HashSet<struct (struct (ObjectHandle * uint64 * uint32 * uint32 * uint32) * struct (ObjectHandle * uint64 * uint32 * uint32 * uint32))>()
    let targets = Dictionary<ObjectHandle, struct (int * uint64)>()
    let mutable count = 0
    let mutable required = 0
    let mutable tickCount = 0
    let mutable tickRequired = 0
    let mutable overflow = false
    let mutable tickOverflow = false
    let mutable disposed = false
    let mutable touched = false
    let requireThread () =
        if thread <> Environment.CurrentManagedThreadId then
            invalidOp "Contact stream requires its simulation owner thread."
    member internal _.Subscriber = subscriber
    member internal _.Owner = owner
    member internal _.SelfRole = selfRole
    member internal _.OtherRole = otherRole
    member internal _.Phases = phases
    member internal _.Disposed with get () = disposed and set value = disposed <- value
    member internal _.Touched with get () = touched and set value = touched <- value
    member _.Overflowed = if isUpdating() then tickOverflow else overflow
    member _.RequiredCapacity = if isUpdating() then tickRequired else required
    member _.Read() : ReadOnlySpan<Contact> =
        requireThread ()
        if disposed then raise (ObjectDisposedException(nameof ContactStream))
        if this.Overflowed then invalidOp "Contact stream capacity exceeded; partial gameplay results are unavailable."
        if isUpdating() then ReadOnlySpan<Contact>(tickBuffer, 0, tickCount) else ReadOnlySpan<Contact>(buffer, 0, count)
    member internal _.Append(contact: inref<Contact>) =
        required <- required + 1
        tickRequired <- tickRequired + 1
        if count = buffer.Length then
            overflow <- true
        else
            buffer[count] <- contact
            count <- count + 1
        if tickCount = tickBuffer.Length then
            tickOverflow <- true
        else
            tickBuffer[tickCount] <- contact
            tickCount <- tickCount + 1
    member internal _.Clear() =
        count <- 0; required <- 0; overflow <- false; touched <- false
    member internal _.ClearTick() =
        tickCount <- 0; tickRequired <- 0; tickOverflow <- false
    member internal _.ResetObservation() =
        sensorPairs.Clear(); targets.Clear(); this.ClearTick(); this.Clear()
    member internal _.ObservePhase(value: inref<NativeContact>, phase: ContactPhases, reverse: bool) =
        if value.Kind < 3 then
            if (phases &&& phase) <> enum<ContactPhases>(0) then phase else enum<ContactPhases>(0)
        else
            let pair = struct (struct (value.First.Owner, value.First.Component, value.First.Shape, value.First.BodyGeneration, value.First.BodySlot),
                               struct (value.Second.Owner, value.Second.Component, value.Second.Shape, value.Second.BodyGeneration, value.Second.BodySlot))
            let target = if reverse then value.First.Owner else value.Second.Owner
            let known = sensorPairs.Contains(pair)
            let grouped = grouping = ContactGrouping.SensorTargets
            let mutable state = match targets.TryGetValue(target) with | true, state -> state | _ -> struct (0, 0UL)
            let mutable result = phase
            let mutable skip = false
            if phase = ContactPhases.End then
                sensorPairs.Remove(pair) |> ignore
                if grouped then
                    let struct (pairs, tick) = state
                    if not known then skip <- true
                    elif pairs > 1 then
                        targets[target] <- struct (pairs - 1, tick)
                        skip <- true
                    else targets.Remove(target) |> ignore
            else
                if not known then
                    if sensorPairs.Count = buffer.Length then
                        overflow <- true; tickOverflow <- true; required <- required + 1; tickRequired <- tickRequired + 1
                        skip <- true
                    else
                        sensorPairs.Add(pair) |> ignore
                        if grouped then
                            let struct (pairs, tick) = state
                            state <- struct (pairs + 1, tick)
                            targets[target] <- state
                            result <- if pairs = 0 then ContactPhases.Begin else ContactPhases.Persist
                        elif phase = ContactPhases.Persist && (phases &&& ContactPhases.Begin) <> enum<ContactPhases>(0) then
                            result <- ContactPhases.Begin
                elif grouped then result <- ContactPhases.Persist
                if grouped && not skip then
                    let struct (pairs, lastTick) = state
                    if lastTick = value.Tick then skip <- true
                    else
                        targets[target] <- struct (pairs, value.Tick)
                        if result = ContactPhases.Begin && (phases &&& ContactPhases.Begin) = enum<ContactPhases>(0) then
                            result <- ContactPhases.Persist
            if not skip && (phases &&& result) <> enum<ContactPhases>(0) then result else enum<ContactPhases>(0)
    member _.Dispose() =
        requireThread ()
        if not disposed then
            remove this
            disposed <- true
            sensorPairs.Clear(); targets.Clear(); this.ClearTick(); this.Clear()
    interface IDisposable with member this.Dispose() = this.Dispose()
