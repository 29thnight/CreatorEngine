namespace CreatorEngine.Diagnostics

#nowarn "9"
open System
open System.Text
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<Struct>]
type ProfilerScope =
    val private Started: bool
    internal new(started) = { Started = started }
    member this.Dispose() =
        if this.Started then
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Profiler_End) -> api.Profiler_End.Invoke()
            | _ -> ()
    interface IDisposable with member this.Dispose() = this.Dispose()

[<Sealed>]
type ProfilerMarker(name: string) =
    let mutable id = 0u
    do if String.IsNullOrWhiteSpace(name) then invalidArg "name" "A profiler marker name is required."
    member _.Auto() =
        match NativeBootstrap.TryGet() with
        | Some api ->
            if id = 0u && not (isNull api.Profiler_Register) then
                let count = Encoding.UTF8.GetByteCount(name)
                if count <= 255 then
                    let bytes = NativePtr.stackalloc<byte> (count + 1)
                    Encoding.UTF8.GetBytes(name.AsSpan(), Span<byte>(NativePtr.toVoidPtr bytes, count)) |> ignore
                    NativePtr.set bytes count 0uy
                    id <- api.Profiler_Register.Invoke(NativePtr.toNativeInt bytes)
            if id <> 0u && not (isNull api.Profiler_Begin) then
                api.Profiler_Begin.Invoke(id)
                ProfilerScope(true)
            else ProfilerScope(false)
        | _ -> ProfilerScope(false)
