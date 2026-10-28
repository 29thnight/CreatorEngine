using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Text.Json;

static Dictionary<string, string[]> ReadSurface(string path)
{
    using var stream = File.OpenRead(path);
    using var pe = new PEReader(stream);
    var metadata = pe.GetMetadataReader();
    var result = new Dictionary<string, string[]>(StringComparer.Ordinal);
    foreach (var handle in metadata.TypeDefinitions)
    {
        var type = metadata.GetTypeDefinition(handle);
        var name = metadata.GetString(type.Name);
        var ns = metadata.GetString(type.Namespace);
        if ((type.Attributes & TypeAttributes.VisibilityMask) != TypeAttributes.Public || !ns.StartsWith("CreatorEngine", StringComparison.Ordinal))
        {
            continue;
        }
        var members = new SortedSet<string>(StringComparer.Ordinal);
        foreach (var methodHandle in type.GetMethods())
        {
            var method = metadata.GetMethodDefinition(methodHandle);
            if ((method.Attributes & MethodAttributes.MemberAccessMask) == MethodAttributes.Public)
            {
                var methodName = metadata.GetString(method.Name);
                members.Add(methodName);
                if (!methodName.StartsWith("get_", StringComparison.Ordinal) && !methodName.StartsWith("set_", StringComparison.Ordinal))
                {
                    var signature = metadata.GetBlobReader(method.Signature);
                    var header = signature.ReadSignatureHeader();
                    if (header.IsGeneric)
                    {
                        _ = signature.ReadCompressedInteger();
                    }
                    var parameterCount = signature.ReadCompressedInteger();
                    members.Add($"{methodName}/{parameterCount}");
                }
            }
        }
        foreach (var fieldHandle in type.GetFields())
        {
            var field = metadata.GetFieldDefinition(fieldHandle);
            if ((field.Attributes & FieldAttributes.FieldAccessMask) == FieldAttributes.Public)
            {
                members.Add(metadata.GetString(field.Name));
            }
        }
        result.Add(ns + "." + name, members.ToArray());
    }
    return result;
}

if (args.Length < 2)
{
    throw new ArgumentException("Expected legacy and F# binding assemblies.");
}
var oldSurface = ReadSurface(args[0]);
var newSurface = new Dictionary<string, string[]>(StringComparer.Ordinal);
foreach (var path in args.Skip(1))
{
    foreach (var entry in ReadSurface(path))
    {
        newSurface.Add(entry.Key, entry.Value);
    }
}
var differences = oldSurface.Select(entry => new
{
    type = entry.Key,
    missingType = !newSurface.ContainsKey(entry.Key),
    missingMembers = newSurface.TryGetValue(entry.Key, out var members) ? entry.Value.Except(members).ToArray() : Array.Empty<string>()
}).Where(entry => entry.missingType || entry.missingMembers.Length != 0).ToArray();
Console.WriteLine(JsonSerializer.Serialize(differences, new JsonSerializerOptions { WriteIndented = true }));
