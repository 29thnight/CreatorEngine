namespace CreatorEngine

#nowarn "9"
open System
open System.Text
open System.Collections.Generic
open System.Runtime.CompilerServices
open System.Runtime.InteropServices
open Microsoft.FSharp.NativeInterop
open CreatorEngine.Native

[<Struct; CustomEquality; NoComparison>]
type Entity =
    val internal Handle: ObjectHandle
    new (handle) = { Handle = handle }
    member this.IsAlive =
        this.Handle.IsValid && NativeBootstrap.IsEntityAlive(this.Handle.ToNative())

    member this.GetComponent<'T when 'T :> Component>() : 'T =
        if typeof<'T> = typeof<Transform> then
            let exists =
                match NativeBootstrap.TryGet() with
                | Some api when not (isNull api.Transform_Exists) -> api.Transform_Exists.Invoke(this.Handle.ToNative()) <> 0
                | _ -> true
            if exists then unbox<'T> (box (Transform(this.Handle))) else Unchecked.defaultof<'T>
        elif not (typeof<NativeComponent>.IsAssignableFrom(typeof<'T>)) then
            match Component.FindManaged(this.Handle, typeof<'T>) with
            | :? 'T as found -> found
            | _ -> Unchecked.defaultof<'T>
        else
            match Component.FindNative(this.Handle, typeof<'T>) with
            | :? 'T as found -> found
            | _ -> Unchecked.defaultof<'T>

    member this.Transform = this.GetComponent<Transform>()
    member this.GetComponents<'T when 'T :> Component>() =
        let result = List<'T>()
        if typeof<NativeComponent>.IsAssignableFrom(typeof<'T>) then
            let found = this.GetComponent<'T>()
            if not (obj.ReferenceEquals(found, null)) then result.Add(found)
        else
            for candidate in Component.FindAllManaged(this.Handle, typeof<'T>) do
                match candidate with
                | :? 'T as found -> result.Add(found)
                | _ -> ()
        result
    member this.TryGetComponent<'T when 'T :> Component>(output: byref<'T>) =
        output <- this.GetComponent<'T>()
        not (obj.ReferenceEquals(output, null))
    member this.HasComponent<'T when 'T :> Component>() = not (obj.ReferenceEquals(this.GetComponent<'T>(), null))
    member this.Name =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_GetName) ->
            let buffer = NativePtr.stackalloc<byte> 256
            let length = api.Entity_GetName.Invoke(this.Handle.ToNative(), NativePtr.toNativeInt buffer, 256)
            if length > 0 then Encoding.UTF8.GetString(ReadOnlySpan<byte>(NativePtr.toVoidPtr buffer, length)) else String.Empty
        | _ -> String.Empty
    member this.SetEnabled(enabled: bool) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_SetEnabled) -> api.Entity_SetEnabled.Invoke(this.Handle.ToNative(), if enabled then 1 else 0)
        | _ -> ()
    member this.ChildCount =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_GetChildCount) -> api.Entity_GetChildCount.Invoke(this.Handle.ToNative())
        | _ -> 0
    member this.GetChild(index: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_GetChild) -> Entity(ObjectHandle.FromNative(api.Entity_GetChild.Invoke(this.Handle.ToNative(), index)))
        | _ -> Entity(ObjectHandle.Invalid)
    member this.Children =
        let entity = this
        seq { for index = 0 to entity.ChildCount - 1 do yield entity.GetChild(index) }
    member this.GetComponentsInChildren<'T when 'T :> Component>() = this.GetComponentsInChildren<'T>(true)
    member this.GetComponentsInChildren<'T when 'T :> Component>(includeSelf: bool) =
        let result = List<'T>()
        let rec collect (node: Entity) includeNode =
            if includeNode then
                let found = node.GetComponent<'T>()
                if not (obj.ReferenceEquals(found, null)) then result.Add(found)
            for index = 0 to node.ChildCount - 1 do
                collect (node.GetChild(index)) true
        collect this includeSelf
        result
    member this.GetComponentInChildren<'T when 'T :> Component>() = this.GetComponentInChildren<'T>(true)
    member this.GetComponentInChildren<'T when 'T :> Component>(includeSelf: bool) =
        let rec find (node: Entity) includeNode =
            let self = if includeNode then node.GetComponent<'T>() else Unchecked.defaultof<'T>
            if not (obj.ReferenceEquals(self, null)) then self
            else
                let mutable result = Unchecked.defaultof<'T>
                let mutable index = 0
                while index < node.ChildCount && obj.ReferenceEquals(result, null) do
                    result <- find (node.GetChild(index)) true
                    index <- index + 1
                result
        find this includeSelf
    member this.GetComponentInParent<'T when 'T :> Component>() = this.GetComponentInParent<'T>(true)
    member this.GetComponentInParent<'T when 'T :> Component>(includeSelf: bool) =
        let mutable node = this
        let mutable result = Unchecked.defaultof<'T>
        let mutable depth = 0
        while depth < 64 && node.Handle.IsValid && obj.ReferenceEquals(result, null) do
            if includeSelf || depth > 0 then result <- node.GetComponent<'T>()
            node <- node.Parent
            depth <- depth + 1
        result
    member this.Parent =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_GetParent) -> Entity(ObjectHandle.FromNative(api.Entity_GetParent.Invoke(this.Handle.ToNative())))
        | _ -> Entity(ObjectHandle.Invalid)
    member this.Index =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_GetIndex) -> api.Entity_GetIndex.Invoke(this.Handle.ToNative())
        | _ -> -1
    static member FindByIndex(index: int) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_FindByIndex) -> Entity(ObjectHandle.FromNative(api.Entity_FindByIndex.Invoke(index)))
        | _ -> Entity(ObjectHandle.Invalid)
    static member Find(name: string) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_FindByName) ->
            let buffer = NativePtr.stackalloc<byte> 256
            let length = Encoding.UTF8.GetBytes(name.AsSpan(), Span<byte>(NativePtr.toVoidPtr buffer, 255))
            NativePtr.set buffer length 0uy
            Entity(ObjectHandle.FromNative(api.Entity_FindByName.Invoke(NativePtr.toNativeInt buffer)))
        | _ -> Entity(ObjectHandle.Invalid)
    member this.Destroy() =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Entity_Destroy) -> api.Entity_Destroy.Invoke(this.Handle.ToNative())
        | _ -> ()
    static member Destroy(target: Entity) = target.Destroy()
    override this.Equals(other) =
        match other with
        | :? Entity as entity -> this.Handle.Equals(entity.Handle)
        | _ -> false
    override this.GetHashCode() = this.Handle.GetHashCode()
    override this.ToString() = if this.IsAlive then $"Entity({this.Name})" else "Entity(<destroyed>)"
    interface IEquatable<Entity> with
        member this.Equals(other) = this.Handle.Equals(other.Handle)

and [<AllowNullLiteral; AbstractClass>] Component() =
    static let mutable managedLookup: Func<ObjectHandle, Type, Component> = null
    static let mutable managedLookupAll: Func<ObjectHandle, Type, IReadOnlyList<Component>> = null
    static let nativeFactories = Dictionary<Type, Func<ObjectHandle, Component>>()
    let mutable entity = Unchecked.defaultof<Entity>
    let mutable transform: Transform = null
    let mutable enabled = true
    let mutable destroyed = false
    let mutable initialized = false
    let mutable initializeSucceeded = false
    let mutable enterSucceeded = false
    let mutable beginSucceeded = false
    let mutable enterDelivered = false
    let mutable teardownDelivered = false
    let mutable applyEnabled: Action<Component, bool> = null
    let lifetime = ComponentLifetime()
    static member internal SetManagedLookup(callback: Func<ObjectHandle, Type, Component>) = managedLookup <- callback
    static member internal SetManagedLookupAll(callback: Func<ObjectHandle, Type, IReadOnlyList<Component>>) = managedLookupAll <- callback
    static member internal ClearManagedLookup(callback: Func<ObjectHandle, Type, Component>) =
        if obj.ReferenceEquals(managedLookup, callback) then managedLookup <- null
    static member internal ClearManagedLookupAll(callback: Func<ObjectHandle, Type, IReadOnlyList<Component>>) =
        if obj.ReferenceEquals(managedLookupAll, callback) then managedLookupAll <- null
    static member internal FindManaged(owner: ObjectHandle, kind: Type) : Component =
        if isNull managedLookup then null else managedLookup.Invoke(owner, kind)
    static member internal FindAllManaged(owner: ObjectHandle, kind: Type) : IReadOnlyList<Component> =
        if isNull managedLookupAll then Array.empty<Component> else managedLookupAll.Invoke(owner, kind)
    static member internal RegisterNative(kind: Type, factory: Func<ObjectHandle, Component>) = nativeFactories.[kind] <- factory
    static member internal FindNative(owner: ObjectHandle, kind: Type) : Component =
        match nativeFactories.TryGetValue(kind) with
        | true, factory -> factory.Invoke(owner)
        | _ -> null
    static member FrameCount =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Engine_GetFrameCount) -> api.Engine_GetFrameCount.Invoke()
        | _ -> 0UL
    static member Log(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 1 message file line memberName
    static member LogWarning(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 2 message file line memberName
    static member LogError(message: string, [<CallerFilePath; Optional; DefaultParameterValue("")>] file: string, [<CallerLineNumber; Optional; DefaultParameterValue(0)>] line: int, [<CallerMemberName; Optional; DefaultParameterValue("")>] memberName: string) = NativeLog.writeAt 3 message file line memberName
    member _.Entity with get () = entity and internal set value = entity <- value
    member _.HasTransform = not (isNull transform)
    member _.Transform =
        if isNull transform then invalidOp "This entity has no Transform."
        transform
    member internal _.BindEntity(owner: Entity) =
        entity <- owner
        transform <- owner.GetComponent<Transform>()
    member internal _.BindTransform(value: Transform) = transform <- value
    member this.GetComponent<'T when 'T :> Component>() = this.Entity.GetComponent<'T>()
    member this.GetComponents<'T when 'T :> Component>() = this.Entity.GetComponents<'T>()
    member this.TryGetComponent<'T when 'T :> Component>(output: byref<'T>) = this.Entity.TryGetComponent(&output)
    member this.HasComponent<'T when 'T :> Component>() = this.Entity.HasComponent<'T>()
    member this.GetComponentsInChildren<'T when 'T :> Component>() = this.Entity.GetComponentsInChildren<'T>()
    member this.GetComponentsInChildren<'T when 'T :> Component>(includeSelf: bool) = this.Entity.GetComponentsInChildren<'T>(includeSelf)
    member this.GetComponentInChildren<'T when 'T :> Component>() = this.Entity.GetComponentInChildren<'T>()
    member this.GetComponentInChildren<'T when 'T :> Component>(includeSelf: bool) = this.Entity.GetComponentInChildren<'T>(includeSelf)
    member this.GetComponentInParent<'T when 'T :> Component>() = this.Entity.GetComponentInParent<'T>()
    member this.GetComponentInParent<'T when 'T :> Component>(includeSelf: bool) = this.Entity.GetComponentInParent<'T>(includeSelf)
    member val internal InstanceId = 0 with get, set
    member val internal Lifecycle = ScriptLifecycle() with get, set
    member internal _.Lifetime = lifetime
    member internal _.SetApplyEnabled(callback: Action<Component, bool>) = applyEnabled <- callback
    abstract member Enabled: bool with get, set
    default this.Enabled
        with get () = enabled
        and set value =
            if enabled <> value then
                if not (NativeApi.ScriptSetEnabled(this.Entity.Handle, this.InstanceId, value)) then
                    if not NativeApi.IsGameThread then
                        NativeApi.Log(3, $"[{this.GetType().Name}] Enabled change requires the game thread.")
                    else
                        NativeApi.Log(2, $"[{this.GetType().Name}] Enabled change was not delivered to native; applying managed state.")
                        if not (isNull applyEnabled) then applyEnabled.Invoke(this, value)
    member internal _.SetEnabledState(value) =
        if enabled = value then
            false
        else
            enabled <- value
            true
    member this.IsAlive = not destroyed && this.Entity.IsAlive
    member internal _.MarkDestroyed() = destroyed <- true
    member internal _.IsMarkedDestroyed = destroyed
    member internal _.IsInitialized = initialized
    member internal _.MarkInitialized() = initialized <- true
    member internal _.InitializeSucceeded = initializeSucceeded
    member internal _.MarkInitializeSucceeded() = initializeSucceeded <- true
    member internal _.EnterSucceeded = enterSucceeded
    member internal _.MarkEnterSucceeded() = enterSucceeded <- true
    member internal _.BeginSucceeded = beginSucceeded
    member internal _.MarkBeginSucceeded() = beginSucceeded <- true
    member internal _.EnterDelivered = enterDelivered
    member internal _.MarkEnterDelivered() = enterDelivered <- true
    member internal _.TeardownDelivered = teardownDelivered
    member internal _.MarkTeardownDelivered() = teardownDelivered <- true
    abstract member FieldCount: int
    default _.FieldCount = 0
    abstract member GetFieldName: int -> string
    default _.GetFieldName(_) = String.Empty
    abstract member GetFieldType: int -> FieldType
    default _.GetFieldType(_) = FieldType.Unknown
    abstract member GetFloat: int -> single
    default _.GetFloat(_) = 0.0f
    abstract member SetFloat: int * single -> unit
    default _.SetFloat(_, _) = ()
    abstract member GetInt32: int -> int
    default _.GetInt32(_) = 0
    abstract member SetInt32: int * int -> unit
    default _.SetInt32(_, _) = ()
    abstract member GetBool: int -> bool
    default _.GetBool(_) = false
    abstract member SetBool: int * bool -> unit
    default _.SetBool(_, _) = ()
    abstract member GetFloat2: int -> Float2
    default _.GetFloat2(_) = Float2.Zero
    abstract member SetFloat2: int * Float2 -> unit
    default _.SetFloat2(_, _) = ()
    abstract member GetFloat3: int -> Float3
    default _.GetFloat3(_) = Float3.Zero
    abstract member SetFloat3: int * Float3 -> unit
    default _.SetFloat3(_, _) = ()
    abstract member GetString: int -> string
    default _.GetString(_) = String.Empty
    abstract member SetString: int * string -> unit
    default _.SetString(_, _) = ()
    abstract member GetObject: int -> Entity
    default _.GetObject(_) = Unchecked.defaultof<Entity>
    abstract member SetObject: int * Entity -> unit
    default _.SetObject(_, _) = ()
    abstract member InvokeMessage: string -> bool
    default _.InvokeMessage(_) = false

and [<AllowNullLiteral; AbstractClass>] NativeComponent internal (owner: ObjectHandle) as this =
    inherit Component()
    do this.Entity <- Entity(owner)
    member internal _.OwnerHandle = owner

and [<AllowNullLiteral; Sealed>] Transform internal (owner: ObjectHandle) =
    inherit NativeComponent(owner)
    member this.LocalPosition
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_GetLocalPosition) ->
                Float3.FromNative(api.Transform_GetLocalPosition.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float3.Zero
        and set (position: Float3) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_SetLocalPosition) ->
                api.Transform_SetLocalPosition.Invoke(this.OwnerHandle.ToNative(), position.ToNative())
            | _ -> ()
    member this.Translate(delta: Float3) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Transform_AddLocalPosition) ->
            api.Transform_AddLocalPosition.Invoke(this.OwnerHandle.ToNative(), delta.ToNative())
        | _ -> ()
    member this.LocalRotation
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_GetLocalRotation) -> Quaternion.FromNative(api.Transform_GetLocalRotation.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Quaternion.Identity
        and set (rotation: Quaternion) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_SetLocalRotation) -> api.Transform_SetLocalRotation.Invoke(this.OwnerHandle.ToNative(), rotation.ToNative())
            | _ -> ()
    member this.LocalScale
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_GetLocalScale) -> Float3.FromNative(api.Transform_GetLocalScale.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float3.One
        and set (scale: Float3) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_SetLocalScale) -> api.Transform_SetLocalScale.Invoke(this.OwnerHandle.ToNative(), scale.ToNative())
            | _ -> ()
    member this.WorldPosition
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_GetWorldPosition) -> Float3.FromNative(api.Transform_GetWorldPosition.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float3.Zero
        and set (position: Float3) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_SetWorldPosition) -> api.Transform_SetWorldPosition.Invoke(this.OwnerHandle.ToNative(), position.ToNative())
            | _ -> ()
    member this.WorldRotation
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_GetWorldRotation) -> Quaternion.FromNative(api.Transform_GetWorldRotation.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Quaternion.Identity
        and set (rotation: Quaternion) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_SetWorldRotation) -> api.Transform_SetWorldRotation.Invoke(this.OwnerHandle.ToNative(), rotation.ToNative())
            | _ -> ()
    member this.WorldScale
        with get () =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_GetWorldScale) -> Float3.FromNative(api.Transform_GetWorldScale.Invoke(this.OwnerHandle.ToNative()))
            | _ -> Float3.One
        and set (scale: Float3) =
            match NativeBootstrap.TryGet() with
            | Some api when not (isNull api.Transform_SetWorldScale) -> api.Transform_SetWorldScale.Invoke(this.OwnerHandle.ToNative(), scale.ToNative())
            | _ -> ()
    member this.Forward =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Transform_GetForward) -> Float3.FromNative(api.Transform_GetForward.Invoke(this.OwnerHandle.ToNative()))
        | _ -> Float3.Forward
    member this.Right =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Transform_GetRight) -> Float3.FromNative(api.Transform_GetRight.Invoke(this.OwnerHandle.ToNative()))
        | _ -> Float3.Right
    member this.Up =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Transform_GetUp) -> Float3.FromNative(api.Transform_GetUp.Invoke(this.OwnerHandle.ToNative()))
        | _ -> Float3.Up
    member this.Rotate(delta: Quaternion) =
        match NativeBootstrap.TryGet() with
        | Some api when not (isNull api.Transform_AddLocalRotation) -> api.Transform_AddLocalRotation.Invoke(this.OwnerHandle.ToNative(), delta.ToNative())
        | _ -> ()
    member this.SetLocalPosition(value: Float3) = this.LocalPosition <- value
    member this.SetLocalRotation(value: Quaternion) = this.LocalRotation <- value
    member this.SetLocalScale(value: Float3) = this.LocalScale <- value
    member this.SetWorldPosition(value: Float3) = this.WorldPosition <- value
    member this.SetWorldRotation(value: Quaternion) = this.WorldRotation <- value
    member this.SetWorldScale(value: Float3) = this.WorldScale <- value
    member this.LookAt(target: Float3) =
        let direction = target - this.WorldPosition
        if direction.LengthSquared >= 1e-12f then this.WorldRotation <- Quaternion.LookRotation(direction)

and [<AllowNullLiteral>] ScriptLifecycle() =
    member val OnInitialized: Action<Component> = null with get, set
    member val OnAddedToScene: Action<Component> = null with get, set
    member val OnBeginSimulation: Action<Component> = null with get, set
    member val OnEndSimulation: Action<Component> = null with get, set
    member val OnRemovingFromScene: Action<Component> = null with get, set
    member val OnUninitializing: Action<Component> = null with get, set
    member val OnEnable: Action<Component> = null with get, set
    member val OnDisable: Action<Component> = null with get, set
    member val PrePhysics: Action<Component, single> = null with get, set
    member val PostPhysics: Action<Component, single> = null with get, set
    member val OnTickSimulate: Action<Component, single> = null with get, set
