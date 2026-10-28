namespace CreatorEngine

open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type DecalComponent internal (owner) =
    inherit ExtraNativeComponent(owner, 2)

    member private this.GetInt(field) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Decal_GetInt) -> api.Decal_GetInt.Invoke(this.OwnerHandle.ToNative(), field)
        | _ -> 0
    member private this.SetInt(field, value) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Decal_SetInt) -> api.Decal_SetInt.Invoke(this.OwnerHandle.ToNative(), field, value)
        | _ -> ()
    member private this.GetFloat(field) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Decal_GetFloat) -> api.Decal_GetFloat.Invoke(this.OwnerHandle.ToNative(), field)
        | _ -> 0.0f
    member private this.SetFloat(field, value) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Decal_SetFloat) -> api.Decal_SetFloat.Invoke(this.OwnerHandle.ToNative(), field, value)
        | _ -> ()

    member this.SliceX with get () = uint32 (this.GetInt(0)) and set (value: uint32) = this.SetInt(0, int value)
    member this.SliceY with get () = uint32 (this.GetInt(1)) and set (value: uint32) = this.SetInt(1, int value)
    member this.SliceNumber with get () = this.GetInt(2) and set value = this.SetInt(2, value)
    member this.UseAnimation with get () = this.GetInt(3) <> 0 and set value = this.SetInt(3, if value then 1 else 0)
    member this.IsLoop with get () = this.GetInt(4) <> 0 and set value = this.SetInt(4, if value then 1 else 0)
    member this.SlicePerSeconds with get () = this.GetFloat(0) and set value = this.SetFloat(0, value)
    member this.Timer with get () = this.GetFloat(1) and set value = this.SetFloat(1, value)
