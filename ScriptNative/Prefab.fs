namespace CreatorEngine

open System
open System.Runtime.InteropServices
open CreatorEngine.Native

[<Struct>]
type Prefab =
    val private name: string
    private new(name) = { name = name }
    member this.IsValid = not (String.IsNullOrEmpty(this.name))
    member this.Name = if isNull this.name then String.Empty else this.name
    static member Load(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Prefab_Exists) && not (String.IsNullOrEmpty(name)) ->
            let pointer = Marshal.StringToCoTaskMemUTF8(name)
            try
                if api.Prefab_Exists.Invoke(pointer) <> 0 then Prefab(name) else Unchecked.defaultof<Prefab>
            finally
                Marshal.FreeCoTaskMem(pointer)
        | _ -> Unchecked.defaultof<Prefab>
    member this.Instantiate() = this.Instantiate(String.Empty)
    member this.Instantiate(instanceName: string) =
        if not this.IsValid then
            NativeApi.Log(2, "[ScriptRuntime] 유효하지 않은 프리팹으로 Instantiate를 호출했습니다")
            Unchecked.defaultof<Entity>
        else
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Prefab_Instantiate) ->
                let name = Marshal.StringToCoTaskMemUTF8(this.name)
                let target = Marshal.StringToCoTaskMemUTF8(if isNull instanceName then String.Empty else instanceName)
                try Entity(ObjectHandle.FromNative(api.Prefab_Instantiate.Invoke(name, target)))
                finally
                    Marshal.FreeCoTaskMem(target)
                    Marshal.FreeCoTaskMem(name)
            | _ -> Unchecked.defaultof<Entity>
