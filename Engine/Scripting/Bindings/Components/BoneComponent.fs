namespace CreatorEngine

open CreatorEngine.Native

[<AllowNullLiteral; Sealed>]
type BoneComponent internal (owner) =
    inherit ExtraNativeComponent(owner, 1)

    member this.Pinned
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Bone_GetPinned) -> api.Bone_GetPinned.Invoke(this.OwnerHandle.ToNative()) <> 0
            | _ -> false
        and set value =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Bone_SetPinned) -> api.Bone_SetPinned.Invoke(this.OwnerHandle.ToNative(), if value then 1 else 0)
            | _ -> ()

    member this.ResolvedBoneIndex =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Bone_GetResolvedIndex) -> api.Bone_GetResolvedIndex.Invoke(this.OwnerHandle.ToNative())
        | _ -> -1
