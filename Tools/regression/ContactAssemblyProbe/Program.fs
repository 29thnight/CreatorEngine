open System
open System.IO
open System.Reflection
open System.Runtime.CompilerServices
open System.Runtime.Loader
open CreatorEngine

type ProbeContext() =
    inherit AssemblyLoadContext(isCollectible = true)
    override _.Load(name: AssemblyName) =
        if name.Name = typeof<Component>.Assembly.GetName().Name then
            typeof<Component>.Assembly
        else
            null

type Probe =
    [<MethodImpl(MethodImplOptions.NoInlining)>]
    static member Exercise(path: string, typeName: string) =
        let context = new ProbeContext()
        use input = File.OpenRead(path)
        let assembly = context.LoadFromStream(input)
        let method = assembly.GetType("CreatorEngine.Scripts." + typeName, true).GetMethod("State", BindingFlags.Public ||| BindingFlags.Static)
        let result = method.Invoke(null, null) :?> string
        if not (result.StartsWith('{')) then
            invalidOp "Missing diagnostic JSON."
        let weak = WeakReference(context)
        context.Unload()
        weak

[<EntryPoint>]
let main args =
    if args.Length <> 1 then
        invalidArg "args" "Pass the current GameScripts.dll path."
    let json = Probe.Exercise(args[0], "PhysicsContactStreamProbe")
    let plain = Probe.Exercise(args[0], "PhysicsSceneContactProbe")
    for _ in 1 .. 10 do
        GC.Collect()
        GC.WaitForPendingFinalizers()
        GC.Collect()
    if not json.IsAlive || plain.IsAlive then
        invalidOp $"Unexpected collectible diagnostics: JSON={json.IsAlive}, plain={plain.IsAlive}"
    Console.WriteLine("CONTACT_ASSEMBLY_DIAGNOSTIC_OK checks=2 defaultJsonRetained=true plainCollected=true")
    0
