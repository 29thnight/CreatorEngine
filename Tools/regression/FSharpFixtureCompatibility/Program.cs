using System.Globalization;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using CreatorEngine;

// This is a managed comparison harness. Product scripts are compiled from F#.
// Legacy C# inputs provide the old serialized-field contract for the migration.
if (args.Length != 3)
{
    throw new ArgumentException("Expected legacy DLL, F# DLL and result JSON paths.");
}

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;

Catalog legacy = ReadCatalog(args[0]);
Catalog migrated = ReadCatalog(args[1]);

// New F#-only acceptance fixture has no historical C# contract to compare.
migrated.Components.RemoveAll(component => component.Name == "FSharpNativePlayerProbe");

string legacyJson = JsonSerializer.Serialize(legacy);
string migratedJson = JsonSerializer.Serialize(migrated);

if (legacyJson != migratedJson)
{
    File.WriteAllText(args[2] + ".legacy.json", legacyJson);
    File.WriteAllText(args[2] + ".fsharp.json", migratedJson);
    throw new InvalidOperationException("F# migration changed factory names or serialized field defaults. Compare the two catalog files.");
}

var result = new
{
    result = "FSHARP_FIXTURE_COMPATIBILITY_OK",
    components = migrated.Components.Count,
    animations = migrated.Animations,
    behaviorTreeNodes = migrated.BehaviorTreeNodes,
    fields = migrated.Components.Sum(component => component.Fields.Count),
    scope = "Generated factory kinds, serialized labels, field kinds and constructor defaults; product hook behavior is verified separately."
};

File.WriteAllText(args[2], JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"FSHARP_FIXTURE_COMPATIBILITY_OK components={result.components} fields={result.fields}");

static Catalog ReadCatalog(string assemblyPath)
{
    var context = new FixtureContext(Path.GetDirectoryName(Path.GetFullPath(assemblyPath))!);

    try
    {
        Assembly assembly = context.LoadFromAssemblyPath(Path.GetFullPath(assemblyPath));
        Type registry = assembly.GetType("CreatorEngine.Generated.ScriptRegistry", throwOnError: true)!;

        var components = new SortedDictionary<string, ComponentContract>(StringComparer.Ordinal);
        var animations = new SortedSet<string>(StringComparer.Ordinal);
        var behaviorTreeNodes = new SortedDictionary<string, int>(StringComparer.Ordinal);

        var registerComponent = new Action<string, Func<Component>>((name, create) =>
        {
            Component instance = create();
            var fields = new List<FieldContract>();

            for (int index = 0; index < instance.FieldCount; ++index)
            {
                FieldType kind = instance.GetFieldType(index);
                fields.Add(new FieldContract(instance.GetFieldName(index), kind.ToString(), ReadDefault(instance, index, kind)));
            }

            components.Add(name, new ComponentContract(name, fields));
        });

        var registerAnimation = new Action<string, Func<AniBehavior>>((name, _) => animations.Add(name));
        var registerNode = new Action<string, int, Func<BTNode>>((name, kind, _) => behaviorTreeNodes.Add(name, kind));

        registry.GetMethod("RegisterAll")!.Invoke(null, [registerComponent]);
        registry.GetMethod("RegisterAllAni")!.Invoke(null, [registerAnimation]);
        registry.GetMethod("RegisterAllBTNodes")!.Invoke(null, [registerNode]);

        return new Catalog(components.Values.ToList(), animations.ToList(), behaviorTreeNodes);
    }
    finally
    {
        context.Unload();
    }
}

static string ReadDefault(Component instance, int index, FieldType kind)
{
    return kind switch
    {
        FieldType.Float => instance.GetFloat(index).ToString("R", CultureInfo.InvariantCulture),
        FieldType.Int32 => instance.GetInt32(index).ToString(CultureInfo.InvariantCulture),
        FieldType.Bool => instance.GetBool(index).ToString(),
        FieldType.Float2 => instance.GetFloat2(index).ToString(),
        FieldType.Float3 => instance.GetFloat3(index).ToString(),
        FieldType.String or FieldType.AssetLink => instance.GetString(index),
        FieldType.Object => instance.GetObject(index).Equals(default(Entity)).ToString(),
        _ => throw new InvalidOperationException($"Unexpected serialized field kind: {kind}")
    };
}

sealed class FixtureContext(string directory) : AssemblyLoadContext(isCollectible: true)
{
    protected override Assembly? Load(AssemblyName name)
    {
        if (name.Name == typeof(Component).Assembly.GetName().Name)
        {
            return typeof(Component).Assembly;
        }

        string candidate = Path.Combine(directory, name.Name + ".dll");
        return File.Exists(candidate) ? LoadFromAssemblyPath(candidate) : null;
    }
}

sealed record FieldContract(string Name, string Kind, string Default);
sealed record ComponentContract(string Name, List<FieldContract> Fields);
sealed record Catalog(
    List<ComponentContract> Components,
    List<string> Animations,
    SortedDictionary<string, int> BehaviorTreeNodes);
