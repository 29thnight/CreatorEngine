namespace CreatorEngine

open System
open System.Collections.Generic
open System.IO
open System.Reflection
open System.Runtime.CompilerServices
open System.Runtime.Loader
open System.Text.Json
open CreatorEngine

/// Script dependencies, including FSharp.Core, belong to their collectible
/// generation. Only the engine binding assembly and framework are shared.
type internal ScriptLoadContext(name: string, directory: string) =
    inherit AssemblyLoadContext(name, isCollectible = true)

    let directory = Path.GetFullPath(directory)
    let core = typeof<Component>.Assembly
    let audioValues = typeof<AudioPlaySettings>.Assembly

    let framework =
        let names = HashSet<string>(StringComparer.OrdinalIgnoreCase)
        let runtimeDirectory = Path.GetDirectoryName(typeof<obj>.Assembly.Location)
        let paths = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") :?> string

        if not (isNull paths) then
            for path in paths.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries) do
                if String.Equals(Path.GetDirectoryName(path), runtimeDirectory, StringComparison.OrdinalIgnoreCase) then
                    names.Add(Path.GetFileNameWithoutExtension(path)) |> ignore

        names

    member this.LoadScript(path: string) =
        let fullPath = Path.GetFullPath(path)
        let identity = AssemblyName.GetAssemblyName(fullPath)

        let existing =
            this.Assemblies
            |> Seq.tryFind (fun assembly ->
                String.Equals(assembly.GetName().Name, identity.Name, StringComparison.OrdinalIgnoreCase))

        match existing with
        | Some assembly ->
            if assembly.GetName().FullName <> identity.FullName then
                raise (FileLoadException($"Conflicting script dependency identity: {identity.FullName}", fullPath))

            assembly
        | None ->
            // Stream loading permits the next build to replace both DLL and PDB.
            use input =
                new FileStream(fullPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite ||| FileShare.Delete)

            let pdbPath = Path.ChangeExtension(fullPath, ".pdb")

            if File.Exists(pdbPath) then
                use pdb =
                    new FileStream(pdbPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite ||| FileShare.Delete)

                this.LoadFromStream(input, pdb)
            else
                this.LoadFromStream(input)

    override this.Load(identity) =
        if identity.Name = core.GetName().Name then
            core
        elif identity.Name = audioValues.GetName().Name then
            audioValues
        elif framework.Contains(identity.Name) then
            null
        else
            let name = identity.Name

            if String.IsNullOrEmpty(name) || name.IndexOfAny([| '/'; '\\' |]) >= 0 then
                raise (FileLoadException("Invalid script dependency name."))

            let path = Path.Combine(directory, name + ".dll")

            if not (File.Exists(path)) then
                raise (FileNotFoundException($"Missing private script dependency: {identity.FullName}", path))

            this.LoadScript(path)

type internal Loader(registry: Registry, auxiliary: IAuxiliaryRegistry) =
    let mutable context: ScriptLoadContext option = None
    let mutable unloadProbe: WeakReference option = None
    let mutable assemblyPath: string option = None
    let mutable generation = 0

    let readAssemblySet (primaryPath: string) =
        let manifestPath = primaryPath + ".scripts.json"

        if not (File.Exists(manifestPath)) then
            [| primaryPath |]
        else
            use input =
                new FileStream(manifestPath, FileMode.Open, FileAccess.Read, FileShare.Read)

            if input.Length <= 0L || input.Length > 16384L then
                raise (InvalidDataException("Script assembly manifest must be between 1 and 16384 bytes."))

            let bytes = Array.zeroCreate<byte>(int input.Length)
            input.ReadExactly(bytes.AsSpan())

            let offset =
                if bytes.Length >= 3 && bytes[0] = 0xefuy && bytes[1] = 0xbbuy && bytes[2] = 0xbfuy then
                    3
                else
                    0

            use document =
                JsonDocument.Parse(ReadOnlyMemory<byte>(bytes, offset, bytes.Length - offset))

            let root = document.RootElement
            let mutable version = Unchecked.defaultof<JsonElement>
            let mutable entries = Unchecked.defaultof<JsonElement>
            let mutable schema = 0

            if
                root.ValueKind <> JsonValueKind.Object
                || not (root.TryGetProperty("schemaVersion", &version))
                || not (version.TryGetInt32(&schema))
                || schema <> 1
                || not (root.TryGetProperty("assemblies", &entries))
                || entries.ValueKind <> JsonValueKind.Array
                || entries.GetArrayLength() > 64
            then
                raise (InvalidDataException("Invalid script assembly manifest schema."))

            let paths = ResizeArray<string>()
            let names = HashSet<string>(StringComparer.OrdinalIgnoreCase)
            paths.Add(primaryPath)
            names.Add(Path.GetFileName(primaryPath)) |> ignore
            names.Add(typeof<Component>.Assembly.GetName().Name + ".dll") |> ignore
            names.Add(typeof<AudioPlaySettings>.Assembly.GetName().Name + ".dll") |> ignore
            names.Add("CreatorEngine.ManagedRuntime.dll") |> ignore

            for entry in entries.EnumerateArray() do
                let name =
                    if entry.ValueKind = JsonValueKind.String then
                        entry.GetString()
                    else
                        null

                if
                    String.IsNullOrWhiteSpace(name)
                    || name.IndexOfAny([| '/'; '\\'; ':' |]) >= 0
                    || not (name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
                    || name.Contains("..", StringComparison.Ordinal)
                    || not (names.Add(name))
                then
                    raise (InvalidDataException("Script manifest entries must be unique sibling script DLL filenames."))

                let path = Path.Combine(Path.GetDirectoryName(primaryPath), name)

                if not (File.Exists(path)) then
                    raise (FileNotFoundException("A declared script assembly is missing.", path))

                paths.Add(path)

            paths.ToArray()

    [<MethodImpl(MethodImplOptions.NoInlining)>]
    let registerAssemblySet (loadContext: ScriptLoadContext) (entries: string array) validationOnly =
        let components = HashSet<string>(StringComparer.Ordinal)
        let callbacks = HashSet<string>(StringComparer.Ordinal)
        let animations = HashSet<string>(StringComparer.Ordinal)
        let trees = HashSet<string>(StringComparer.Ordinal)

        let checkName family (names: HashSet<string>) name valid =
            if String.IsNullOrWhiteSpace(name) || not valid || not (names.Add(name)) then
                raise (InvalidDataException($"Duplicate or invalid {family} registration: {name}"))

        let registerComponent =
            Action<string, Func<Component>>(fun name factory ->
                checkName "component" components name (not (isNull factory))

                if not validationOnly then
                    registry.Register(name, factory))

        let registerLifecycle =
            Action<string, ScriptLifecycle>(fun name callback ->
                checkName "lifecycle" callbacks name (not (isNull callback))

                if not validationOnly then
                    registry.RegisterLifecycle(name, callback))

        let registerAnimation =
            Action<string, Func<AniBehavior>>(fun name factory ->
                checkName "animation" animations name (not (isNull factory))

                if not validationOnly then
                    auxiliary.RegisterAnimation(name, factory))

        let registerTree =
            Action<string, int, Func<BTNode>>(fun name kind factory ->
                checkName "behavior tree" trees name (not (isNull factory))

                if not validationOnly then
                    auxiliary.RegisterNode(name, kind, factory))

        for path in entries do
            let assembly = loadContext.LoadScript(path)

            let generated =
                assembly.GetType("CreatorEngine.Generated.ScriptRegistry", throwOnError = false)

            if isNull generated then
                raise (InvalidDataException($"ScriptRegistry is missing: {path}"))

            let invoke name argument required =
                let method = generated.GetMethod(name, BindingFlags.Public ||| BindingFlags.Static)

                if isNull method then
                    if required then
                        raise (
                            InvalidDataException(
                                $"{name} is missing; rebuild scripts for static lifecycle registration: {path}"
                            )
                        )
                else
                    method.Invoke(null, [| argument |]) |> ignore

            invoke "RegisterAll" registerComponent true
            invoke "RegisterAllLifecycle" registerLifecycle true
            invoke "RegisterAllAni" registerAnimation false
            invoke "RegisterAllBTNodes" registerTree false

        if not (components.SetEquals(callbacks)) then
            raise (InvalidDataException("Component factories and lifecycle tables must name exactly the same scripts."))

    member _.Generation = generation
    member _.IsLoaded = context.IsSome

    member _.LoadedAssemblies() =
        match context with
        | Some loaded -> loaded.Assemblies |> Seq.toArray
        | None -> Array.empty

    [<MethodImpl(MethodImplOptions.NoInlining)>]
    member _.Unload() =
        match context with
        | None -> ()
        | Some loaded ->
            // Native gates close and in-flight calls drain before script
            // cleanup can release delegates or the collectible context.
            NativeCallbackRuntime.EndGeneration()
            registry.Clear()
            registry.ClearRegistrations()
            auxiliary.ClearAnimations()
            auxiliary.ClearNodes()
            auxiliary.ClearTrees()
            unloadProbe <- Some(WeakReference(loaded, trackResurrection = false))
            loaded.Unload()
            context <- None
            GC.Collect()
            GC.WaitForPendingFinalizers()

    [<MethodImpl(MethodImplOptions.NoInlining)>]
    member this.Load(path: string) =
        if context.IsSome then
            false
        elif not (File.Exists(path)) then
            false
        else
            assemblyPath <- Some(Path.GetFullPath(path))

            try
                NativeCallbackRuntime.PrepareGeneration()
                let path = assemblyPath.Value
                let entries = readAssemblySet path

                let loaded =
                    ScriptLoadContext($"GameScripts#{generation}", Path.GetDirectoryName(path))

                context <- Some loaded
                registerAssemblySet loaded entries false
                generation <- generation + 1
                true
            with error ->
                NativeApi.Log(3, $"[ScriptRuntime] Script assembly load failed.\n{error}")
                this.Unload()
                false

    [<MethodImpl(MethodImplOptions.NoInlining)>]
    member private _.Validate(path: string) =
        // Candidate module initializers must not install rooted callbacks into
        // the live generation before validation accepts the replacement.
        use callbackRegistrationPause = NativeCallbackRuntime.SuspendRegistration()
        let mutable probe: ScriptLoadContext option = None

        try
            try
                if not (File.Exists(path)) then
                    false
                else
                    let entries = readAssemblySet path
                    let candidate = ScriptLoadContext("validate", Path.GetDirectoryName(path))
                    probe <- Some candidate
                    registerAssemblySet candidate entries true
                    true
            with error ->
                NativeApi.Log(3, $"[ScriptRuntime] Reload rejected; previous generation retained.\n{error}")
                false
        finally
            match probe with
            | Some candidate -> candidate.Unload()
            | None -> ()

    member this.Reload() =
        match assemblyPath with
        | None -> 1
        | Some path when not (this.Validate(path)) -> 1
        | Some path ->
            this.Unload()
            if this.Load(path) then 0 else 2

    [<MethodImpl(MethodImplOptions.NoInlining)>]
    member _.IsPreviousContextAlive() =
        match unloadProbe with
        | None -> false
        | Some probe ->
            GC.Collect()
            GC.WaitForPendingFinalizers()
            GC.Collect()

            if probe.IsAlive then
                true
            else
                unloadProbe <- None
                false
