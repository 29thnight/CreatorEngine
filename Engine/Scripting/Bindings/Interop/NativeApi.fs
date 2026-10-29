namespace CreatorEngine

open System
open CreatorEngine.Native

type internal NativeApi private () =
    static member IsGameThread = NativeBootstrap.IsGameThread
    static member Log(level: int, message: string) = NativeLog.write level message
    static member ResetOffThreadReports() = ()
    static member HasTransform(handle: ObjectHandle) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Transform_Exists) -> api.Transform_Exists.Invoke(handle.ToNative()) <> 0
        | _ -> true
    static member ScriptSetEnabled(handle: ObjectHandle, instanceId: int, enabled: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Script_SetEnabled) ->
            api.Script_SetEnabled.Invoke(handle.ToNative(), instanceId, if enabled then 1 else 0) <> 0
        | _ -> false
