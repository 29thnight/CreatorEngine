namespace CreatorEngine

#nowarn "9"
open System
open System.Buffers.Binary
open System.Collections.Concurrent
open System.Runtime.InteropServices
open System.Threading
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native
open CreatorEngine.Native.reflgen_creator_script_native

[<Struct; StructLayout(LayoutKind.Sequential); CustomEquality; NoComparison>]
type AssetId =
    val internal First: uint64
    val internal Second: uint64
    new (value: Guid) =
        let bytes = value.ToByteArray(true)
        { First = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(0, 8)); Second = BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(8, 8)) }
    member this.IsEmpty = this.First = 0UL && this.Second = 0UL
    member this.IsValid =
        let version = (this.First >>> 52) &&& 15UL
        not this.IsEmpty && (version = 4UL || version = 8UL) && (this.Second &&& 0xc0UL) = 0x80UL
    override this.ToString() =
        let bytes = Array.zeroCreate<byte> 16
        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(0, 8), this.First)
        BinaryPrimitives.WriteUInt64LittleEndian(bytes.AsSpan(8, 8), this.Second)
        Guid(ReadOnlySpan<byte>(bytes), true).ToString("D")
    static member TryParse(text: string, result: byref<AssetId>) =
        result <- Unchecked.defaultof<_>
        let mutable guid = Guid.Empty
        if Guid.TryParseExact(text, "D", &guid) then
            let candidate = AssetId(guid)
            if (candidate.IsEmpty || candidate.IsValid) && candidate.ToString() = text then
                result <- candidate
                true
            else false
        else false
    static member Parse(text: string) =
        let mutable result = Unchecked.defaultof<AssetId>
        if not (AssetId.TryParse(text, &result)) then raise (FormatException("Expected a canonical UUIDv4 or UUIDv8 asset ID."))
        result
    override this.Equals(other) =
        match other with | :? AssetId as value -> this.First = value.First && this.Second = value.Second | _ -> false
    override this.GetHashCode() = HashCode.Combine(this.First, this.Second)
    interface IEquatable<AssetId> with member this.Equals(other) = this.First = other.First && this.Second = other.Second

type Texture private () = class end
type Model private () = class end
type Mesh private () = class end
type Skeleton private () = class end
type AnimationClip private () = class end
type ShaderMeta private () = class end
type MaterialProgram private () = class end
type Material private () = class end
type CodeMaterialProgram private () = class end
type AuthoredMaterial private () = class end

type AssetKind =
    | Model = 1u | Material = 2u | Texture = 3u | ShaderMeta = 4u | MaterialProgram = 8u
    | Mesh = 12u | Skeleton = 13u | AnimationClip = 14u

type internal AssetType<'T> private () =
    static member Registration =
        let t = typeof<'T>
        if t = typeof<Texture> then AssetKind.Texture, 3u
        elif t = typeof<Model> then AssetKind.Model, 0x00010001u
        elif t = typeof<Mesh> then AssetKind.Mesh, 0x00010002u
        elif t = typeof<Skeleton> then AssetKind.Skeleton, 0x00010003u
        elif t = typeof<AnimationClip> then AssetKind.AnimationClip, 0x00010004u
        elif t = typeof<ShaderMeta> then AssetKind.ShaderMeta, 0x00010005u
        elif t = typeof<MaterialProgram> then AssetKind.MaterialProgram, 0x00010006u
        elif t = typeof<Material> then AssetKind.Material, 0x00010007u
        elif t = typeof<CodeMaterialProgram> then AssetKind.MaterialProgram, 0x00010008u
        elif t = typeof<AuthoredMaterial> then AssetKind.Material, 0x00010009u
        else raise (NotSupportedException("This concrete type has no AssetDepot runtime binding."))
    static member Kind = fst AssetType<'T>.Registration
    static member TokenType = snd AssetType<'T>.Registration

[<Struct; StructLayout(LayoutKind.Sequential)>]
type internal AssetLinkABI =
    val mutable Asset: AssetId
    val mutable Subasset: AssetId
    val mutable Kind: uint32
    val mutable Reserved: uint32

[<Struct; CustomEquality; NoComparison>]
type AssetLink<'T> =
    val Asset: AssetId
    val Subasset: AssetId
    new(asset: AssetId, subasset: AssetId) =
        AssetType<'T>.Kind |> ignore
        if (not asset.IsEmpty && not asset.IsValid) || (not subasset.IsEmpty && not subasset.IsValid) || (asset.IsEmpty && not subasset.IsEmpty) then
            invalidArg "asset" "Invalid stable asset/subasset identity."
        { Asset = asset; Subasset = subasset }
    new(asset: AssetId) = AssetLink<'T>(asset, Unchecked.defaultof<AssetId>)
    member _.ExpectedKind = AssetType<'T>.Kind
    member this.IsValid = this.Asset.IsValid && (this.Subasset.IsEmpty || this.Subasset.IsValid)
    member internal this.ToABI() =
        let mutable value = Unchecked.defaultof<AssetLinkABI>
        value.Asset <- this.Asset
        value.Subasset <- this.Subasset
        value.Kind <- uint32 this.ExpectedKind
        value
    override this.ToString() = $"1:{uint32 this.ExpectedKind}:{this.Asset}:{this.Subasset}"
    static member TryParse(text: string, result: byref<AssetLink<'T>>) =
        result <- Unchecked.defaultof<_>
        let parts = if isNull text then Array.empty else text.Split(':')
        let mutable asset = Unchecked.defaultof<AssetId>
        let mutable subasset = Unchecked.defaultof<AssetId>
        if parts.Length = 4 && parts[0] = "1" && parts[1] = string (uint32 AssetType<'T>.Kind)
           && AssetId.TryParse(parts[2], &asset) && AssetId.TryParse(parts[3], &subasset)
           && not (asset.IsEmpty && not subasset.IsEmpty) then
            result <- AssetLink<'T>(asset, subasset)
            true
        else false
    static member Parse(text: string) =
        let mutable result = Unchecked.defaultof<AssetLink<'T>>
        if not (AssetLink<'T>.TryParse(text, &result)) then raise (FormatException("Invalid AssetLink version, identity or expected kind."))
        result
    override this.Equals(other) =
        match other with | :? AssetLink<'T> as value -> this.Asset.Equals(value.Asset) && this.Subasset.Equals(value.Subasset) | _ -> false
    override this.GetHashCode() = HashCode.Combine(this.Asset, this.Subasset)
    interface IEquatable<AssetLink<'T>> with member this.Equals(other) = this.Asset.Equals(other.Asset) && this.Subasset.Equals(other.Subasset)

[<Struct>]
type AssetMountId =
    val Value: uint64
    new(value) = { Value = value }
    member this.IsValid = this.Value <> 0UL
type TextureAssetColorSpace = | Source = 0u | Linear = 1u | Srgb = 2u
[<Struct>]
type TextureAssetVariant =
    val mutable ColorSpace: TextureAssetColorSpace
    val mutable Compress: bool
    val mutable Role: uint32
    new(colorSpace, compress, role) = { ColorSpace = colorSpace; Compress = compress; Role = role }
type AssetRequestStatus = | Pending = 0 | Ready = 1 | Failed = 2 | Cancelled = 3 | Stale = 4
type AssetRequestError =
    | None = 0 | InvalidLink = 1 | NotMounted = 2 | TypeMismatch = 3 | HardDependencyCycle = 4
    | UnsupportedType = 5 | UnsupportedRepresentation = 6 | ReadFailed = 7 | IntegrityFailed = 8
    | DecodeFailed = 9 | DependencyFailed = 10 | SubmissionFailed = 11 | ShuttingDown = 12 | RevisionChanged = 13
[<Struct>]
type AssetRequestSnapshot =
    val Status: AssetRequestStatus
    val Error: AssetRequestError
    val Message: string
    val IsWorkComplete: bool
    new(status, error, message, complete) = { Status = status; Error = error; Message = message; IsWorkComplete = complete }
[<Struct; StructLayout(LayoutKind.Sequential)>]
type TextureDescriptor =
    val mutable Width: uint32
    val mutable Height: uint32
    val mutable MipLevels: uint32
    val mutable ArraySize: uint32
    val mutable private Cube: uint32
    member this.IsCube = this.Cube <> 0u

type internal AssetBindingResult =
    | Success = 0 | Unavailable = 1 | InvalidToken = 2 | InvalidLink = 3 | UnsupportedType = 4
    | WrongThread = 5 | InvalidArgument = 6 | NotResident = 7 | InternalError = 8

[<Struct; StructLayout(LayoutKind.Sequential)>]
type internal AssetSnapshotABI =
    val mutable Status: int
    val mutable Error: int
    val mutable WorkComplete: int
    val mutable MessageBytes: int
[<Struct; StructLayout(LayoutKind.Sequential)>]
type internal AssetVariantABI =
    val mutable ColorSpace: uint32
    val mutable Compress: uint32
    val mutable Role: uint32

type internal AssetReleaseQueue private () =
    static let pending = ConcurrentQueue<ScriptAssetToken>()
    static member Enqueue(token) = pending.Enqueue(token)
    static member ReleaseOrEnqueue(token) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Asset_Release) -> api.Asset_Release.Invoke(token) |> ignore
        | _ -> pending.Enqueue(token)
    static member Drain() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Asset_Release) ->
            let mutable token = Unchecked.defaultof<ScriptAssetToken>
            while pending.TryDequeue(&token) do api.Asset_Release.Invoke(token) |> ignore
        | _ -> ()

type internal AssetNative private () =
    static member Check(result: int) =
        match enum<AssetBindingResult>(result) with
        | AssetBindingResult.Success -> ()
        | AssetBindingResult.InvalidToken -> raise (ObjectDisposedException("Asset handle", "The native owner/request token is stale or released."))
        | AssetBindingResult.UnsupportedType -> raise (NotSupportedException("This concrete type has no AssetDepot runtime binding."))
        | other -> raise (InvalidOperationException($"AssetDepot binding failed: {other}."))
    static member Require() =
        match NativeBootstrap.TryGet() with
        | Some api -> api
        | _ -> invalidOp "AssetDepot requires an active engine on the game thread."
    static member ValidateToken<'T>(token: ScriptAssetToken, request: bool) =
        let expected = AssetType<'T>.TokenType ||| (if request then 0x80000000u else 0u)
        if token.generation = 0u || token.``type`` <> expected then invalidOp "Native asset token concrete type/role mismatch."
    static member Request<'T>(link: AssetLink<'T>, variant: TextureAssetVariant, residentOnly: bool) =
        if AssetType<'T>.Kind <> AssetKind.Texture && (variant.ColorSpace <> TextureAssetColorSpace.Source || variant.Compress || variant.Role <> 0u) then
            invalidArg "variant" "Texture variants apply only to Texture links."
        let api = AssetNative.Require()
        if isNull api.Asset_RequestTyped || isNull api.Asset_TryAcquireTyped then invalidOp "AssetDepot requires an active engine on the game thread."
        let mutable nativeLink = link.ToABI()
        let mutable options = Unchecked.defaultof<AssetVariantABI>
        options.ColorSpace <- uint32 variant.ColorSpace
        options.Compress <- if variant.Compress then 1u else 0u
        options.Role <- variant.Role
        let mutable token = Unchecked.defaultof<ScriptAssetToken>
        use linkPtr = fixed &nativeLink
        use optionPtr = fixed &options
        use tokenPtr = fixed &token
        let result =
            if residentOnly then api.Asset_TryAcquireTyped.Invoke(NativePtr.toNativeInt linkPtr, AssetType<'T>.TokenType, NativePtr.toNativeInt optionPtr, NativePtr.toNativeInt tokenPtr)
            else api.Asset_RequestTyped.Invoke(NativePtr.toNativeInt linkPtr, AssetType<'T>.TokenType, NativePtr.toNativeInt optionPtr, NativePtr.toNativeInt tokenPtr)
        if not residentOnly || result <> int AssetBindingResult.NotResident then AssetNative.Check(result)
        token
    static member Snapshot(token: ScriptAssetToken) =
        let api = AssetNative.Require()
        if isNull api.Asset_Snapshot then invalidOp "AssetDepot requires an active engine on the game thread."
        let mutable state = Unchecked.defaultof<AssetSnapshotABI>
        use statePtr = fixed &state
        AssetNative.Check(api.Asset_Snapshot.Invoke(token, NativePtr.toNativeInt statePtr, 0n, 0))
        if state.MessageBytes = 0 then AssetRequestSnapshot(enum<AssetRequestStatus> state.Status, enum<AssetRequestError> state.Error, String.Empty, state.WorkComplete <> 0)
        else
            let mutable buffer = Array.empty<byte>
            let mutable repeat = true
            while repeat do
                buffer <- Array.zeroCreate (Checked.(+) state.MessageBytes 1)
                use output = fixed buffer
                AssetNative.Check(api.Asset_Snapshot.Invoke(token, NativePtr.toNativeInt statePtr, NativePtr.toNativeInt output, buffer.Length))
                repeat <- state.MessageBytes >= buffer.Length
            AssetRequestSnapshot(enum<AssetRequestStatus> state.Status, enum<AssetRequestError> state.Error, Text.Encoding.UTF8.GetString(buffer, 0, state.MessageBytes), state.WorkComplete <> 0)
    static member Acquire(token: ScriptAssetToken, output: byref<ScriptAssetToken>) =
        output <- Unchecked.defaultof<_>
        let api = AssetNative.Require()
        if isNull api.Asset_AcquireResult then invalidOp "AssetDepot requires an active engine on the game thread."
        use pointer = fixed &output
        let result = api.Asset_AcquireResult.Invoke(token, NativePtr.toNativeInt pointer)
        if result = int AssetBindingResult.NotResident then false
        else
            AssetNative.Check(result)
            true
    static member Cancel(token: ScriptAssetToken) =
        let api = AssetNative.Require()
        if isNull api.Asset_Cancel then invalidOp "AssetDepot requires an active engine on the game thread."
        AssetNative.Check(api.Asset_Cancel.Invoke(token))

[<AllowNullLiteral; Sealed>]
type AssetHandle<'T> private (token: ScriptAssetToken) =
    let mutable disposed = 0
    static member internal FromToken(token: ScriptAssetToken) =
        try
            AssetNative.ValidateToken<'T>(token, false)
            AssetHandle<'T>(token)
        with _ ->
            AssetReleaseQueue.ReleaseOrEnqueue(token)
            reraise ()
    member _.IsDisposed = Volatile.Read(&disposed) <> 0
    member internal this.Token =
        if this.IsDisposed then raise (ObjectDisposedException("AssetHandle"))
        token
    member this.Dispose() =
        if Interlocked.CompareExchange(&disposed, 1, 0) = 0 then
            try AssetReleaseQueue.ReleaseOrEnqueue(token)
            with _ ->
                Volatile.Write(&disposed, 0)
                reraise ()
            GC.SuppressFinalize(this)
    override _.Finalize() =
        if Interlocked.Exchange(&disposed, 1) = 0 then
            try AssetReleaseQueue.Enqueue(token) with _ -> ()
    interface IDisposable with member this.Dispose() = this.Dispose()

[<AllowNullLiteral; Sealed>]
type AssetRequest<'T> private (token: ScriptAssetToken) =
    let mutable disposed = 0
    static member internal FromToken(token: ScriptAssetToken) =
        try
            AssetNative.ValidateToken<'T>(token, true)
            AssetRequest<'T>(token)
        with _ ->
            AssetReleaseQueue.ReleaseOrEnqueue(token)
            reraise ()
    member _.IsDisposed = Volatile.Read(&disposed) <> 0
    member private this.Token =
        if this.IsDisposed then raise (ObjectDisposedException("AssetRequest"))
        token
    member this.Snapshot() = AssetNative.Snapshot(this.Token)
    member this.Cancel() = AssetNative.Cancel(this.Token)
    member this.TryAcquireResult(asset: byref<AssetHandle<'T>>) =
        let mutable owner = Unchecked.defaultof<ScriptAssetToken>
        if AssetNative.Acquire(this.Token, &owner) then
            asset <- AssetHandle<'T>.FromToken(owner)
            true
        else
            asset <- null
            false
    member this.Dispose() =
        if Interlocked.CompareExchange(&disposed, 1, 0) = 0 then
            try AssetReleaseQueue.ReleaseOrEnqueue(token)
            with _ ->
                Volatile.Write(&disposed, 0)
                reraise ()
            GC.SuppressFinalize(this)
    override _.Finalize() =
        if Interlocked.Exchange(&disposed, 1) = 0 then
            try AssetReleaseQueue.Enqueue(token) with _ -> ()
    interface IDisposable with member this.Dispose() = this.Dispose()

[<AbstractClass; Sealed>]
type AssetDepot private () =
    static member RequestAsync<'T>(link: AssetLink<'T>, variant: TextureAssetVariant) =
        AssetRequest<'T>.FromToken(AssetNative.Request(link, variant, false))
    static member RequestAsync<'T>(link: AssetLink<'T>) = AssetDepot.RequestAsync(link, Unchecked.defaultof<TextureAssetVariant>)
    static member TryAcquire<'T>(link: AssetLink<'T>, asset: byref<AssetHandle<'T>>, variant: TextureAssetVariant) =
        let token = AssetNative.Request(link, variant, true)
        if token.generation = 0u then
            asset <- null
            false
        else
            asset <- AssetHandle<'T>.FromToken(token)
            true
    static member TryAcquire<'T>(link: AssetLink<'T>, asset: byref<AssetHandle<'T>>) = AssetDepot.TryAcquire(link, &asset, Unchecked.defaultof<TextureAssetVariant>)
    static member ListRootLinks<'T>(mount: AssetMountId) =
        let api = AssetNative.Require()
        if isNull api.Asset_ListRoots then invalidOp "AssetDepot requires an active engine on the game thread."
        let mutable count = 0
        use countPtr = fixed &count
        AssetNative.Check(api.Asset_ListRoots.Invoke(mount.Value, uint32 AssetType<'T>.Kind, 0n, 0, NativePtr.toNativeInt countPtr))
        let mutable roots = Array.empty<AssetLinkABI>
        let mutable repeat = true
        while repeat do
            roots <- Array.zeroCreate count
            use output = fixed roots
            AssetNative.Check(api.Asset_ListRoots.Invoke(mount.Value, uint32 AssetType<'T>.Kind, NativePtr.toNativeInt output, roots.Length, NativePtr.toNativeInt countPtr))
            repeat <- count > roots.Length
        Array.init count (fun index ->
            let value = roots[index]
            if value.Kind <> uint32 AssetType<'T>.Kind || value.Reserved <> 0u then invalidOp "Native asset root kind/layout mismatch."
            AssetLink<'T>(value.Asset, value.Subasset))
    static member Describe(texture: AssetHandle<Texture>) =
        if isNull texture then nullArg "texture"
        let api = AssetNative.Require()
        if isNull api.Asset_ReadTexture then invalidOp "AssetDepot requires an active engine on the game thread."
        let mutable descriptor = Unchecked.defaultof<TextureDescriptor>
        use output = fixed &descriptor
        AssetNative.Check(api.Asset_ReadTexture.Invoke(texture.Token, NativePtr.toNativeInt output))
        descriptor
