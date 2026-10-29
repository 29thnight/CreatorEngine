namespace CreatorEngine

#nowarn "9"

open System
open System.Runtime.InteropServices
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

type SimulationResult =
    | Ok = 0 | Inactive = 1 | StaleEpoch = 2 | InvalidArgument = 3 | Late = 4
    | TooFar = 5 | Full = 6 | NoSnapshot = 7 | SnapshotGap = 8 | BufferTooSmall = 9
    | WrongThread = 10 | Busy = 11 | Failed = 12

[<Struct; StructLayout(LayoutKind.Sequential)>]
type SimulationStatus =
    val mutable Epoch: uint64
    val mutable Committed: uint64
    val mutable Executing: uint64
    val mutable Queued: uint32
    val mutable Active: uint32
    member this.NextTick = max this.Executing this.Committed + 1UL

[<Struct; StructLayout(LayoutKind.Sequential)>]
type SimulationSnapshot =
    val mutable Epoch: uint64
    val mutable Tick: uint64
    val mutable Applied: uint32
    val mutable Rejected: uint32
    val mutable Bytes: uint32
    val mutable Schema: uint32

[<AbstractClass; Sealed>]
type Simulation private () =
    static member GetStatus(status: byref<SimulationStatus>) =
        status <- Unchecked.defaultof<_>
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Simulation_Status) ->
            use pointer = fixed &status
            enum<SimulationResult>(api.Simulation_Status.Invoke(NativePtr.toNativeInt pointer))
        | _ -> SimulationResult.WrongThread

    static member Submit(epoch: uint64, tick: uint64, kind: uint32, payload: ReadOnlySpan<byte>, sequence: byref<uint64>) =
        sequence <- 0UL
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Simulation_Submit) ->
            use data = fixed &MemoryMarshal.GetReference(payload)
            use output = fixed &sequence
            enum<SimulationResult>(api.Simulation_Submit.Invoke(epoch, tick, kind, NativePtr.toNativeInt data, payload.Length, NativePtr.toNativeInt output))
        | _ -> SimulationResult.WrongThread

    static member ReadSnapshot(epoch: uint64, afterTick: uint64, payload: Span<byte>, snapshot: byref<SimulationSnapshot>) =
        snapshot <- Unchecked.defaultof<_>
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Simulation_Read) ->
            use data = fixed &MemoryMarshal.GetReference(payload)
            use output = fixed &snapshot
            enum<SimulationResult>(api.Simulation_Read.Invoke(epoch, afterTick, NativePtr.toNativeInt output, NativePtr.toNativeInt data, payload.Length))
        | _ -> SimulationResult.WrongThread

    static member SetWorldPosition(epoch: uint64, tick: uint64, owner: Entity, position: Float3, sequence: byref<uint64>) =
        Simulation.SetWorldPosition(epoch, tick, owner.Handle, position, &sequence)

    static member SetWorldPosition(epoch: uint64, tick: uint64, owner: ObjectHandle, position: Float3, sequence: byref<uint64>) =
        let memory = NativePtr.stackalloc<byte> 20
        let payload = Span<byte>(NativePtr.toVoidPtr memory, 20)
        let mutable handle = owner
        let mutable vector = position
        MemoryMarshal.Write(payload, &handle)
        MemoryMarshal.Write(payload.Slice(8), &vector)
        Simulation.Submit(epoch, tick, 1u, ReadOnlySpan<byte>(NativePtr.toVoidPtr memory, 20), &sequence)
