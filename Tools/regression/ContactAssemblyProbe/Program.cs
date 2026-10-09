using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using CreatorEngine;

if (args.Length != 1) throw new ArgumentException("Pass the current GameScripts.dll path.");

var json = Exercise(args[0], "PhysicsContactStreamProbe");
var plain = Exercise(args[0], "PhysicsSceneContactProbe");
for (int i = 0; i < 10; ++i)
{
    GC.Collect();
    GC.WaitForPendingFinalizers();
    GC.Collect();
}

if (!json.IsAlive || plain.IsAlive)
    throw new InvalidOperationException($"Unexpected collectible diagnostics: JSON={json.IsAlive}, plain={plain.IsAlive}");

Console.WriteLine("CONTACT_ASSEMBLY_DIAGNOSTIC_OK checks=2 defaultJsonRetained=true plainCollected=true");

[MethodImpl(MethodImplOptions.NoInlining)]
static WeakReference Exercise(string path, string type)
{
    var context = new ProbeContext();
    using var input = File.OpenRead(path);
    var assembly = context.LoadFromStream(input);
    var method = assembly.GetType("CreatorEngine.Scripts." + type, true)!.GetMethod("State", BindingFlags.Public | BindingFlags.Static)!;
    var result = (string)method.Invoke(null, null)!;
    if (!result.StartsWith('{')) throw new InvalidOperationException("Missing diagnostic JSON.");
    var weak = new WeakReference(context);
    context.Unload();
    return weak;
}

sealed class ProbeContext() : AssemblyLoadContext(isCollectible: true)
{
    protected override Assembly? Load(AssemblyName name)
        => name.Name == typeof(Component).Assembly.GetName().Name ? typeof(Component).Assembly : null;
}
