using Mono.Cecil;
using Mono.Cecil.Cil;

if (args.Length != 1 || !File.Exists(args[0]))
{
    Console.Error.WriteLine("Expected one compiled F# value assembly path.");
    return 1;
}
string path = Path.GetFullPath(args[0]);
using var assembly = AssemblyDefinition.ReadAssembly(path, new ReaderParameters { InMemory = true });
var type = assembly.MainModule.GetType("CreatorEngine.AudioPlaySettings")
    ?? throw new InvalidOperationException("AudioPlaySettings not found.");
var defaultGetter = type.Methods.Single(method => method.Name == "get_Default" && method.IsStatic && !method.HasParameters);
if (type.Methods.Any(method => method.IsConstructor && !method.IsStatic && !method.HasParameters))
{
    return 0;
}
var constructor = new MethodDefinition(".ctor", MethodAttributes.Public | MethodAttributes.HideBySig |
    MethodAttributes.SpecialName | MethodAttributes.RTSpecialName, assembly.MainModule.TypeSystem.Void);
var il = constructor.Body.GetILProcessor();
il.Append(il.Create(OpCodes.Ldarg_0));
il.Append(il.Create(OpCodes.Call, defaultGetter));
il.Append(il.Create(OpCodes.Stobj, type));
il.Append(il.Create(OpCodes.Ret));
type.Methods.Add(constructor);
string temporary = path + ".ctor.tmp";
try
{
    assembly.Write(temporary);
    File.Move(temporary, path, true);
}
finally
{
    if (File.Exists(temporary))
    {
        File.Delete(temporary);
    }
}
return 0;
