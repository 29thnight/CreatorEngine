using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace CreatorEngine.Generators;

/// <summary>
/// Behaviour 파생 클래스를 훑어 두 가지를 생성한다.
///
///  1) [SerializeField] 필드에 대한 인덱스 기반 접근자 — 인스펙터와 직렬화가 함께 쓴다.
///  2) 타입 이름 → 생성자 표 — ScriptFactory가 Activator 없이 인스턴스를 만든다.
///
/// 엔진이 C++에서 하고 있는 것(어노테이션 → 코드젠 → .generated.h)과 같은 철학이고,
/// 컴파일 타임에 확정되므로 Native AOT에서도 트리밍되지 않는다.
/// </summary>
[Generator]
public sealed class BehaviourGenerator : IIncrementalGenerator
{
    private const string BehaviourFullName = "CreatorEngine.Behaviour";
    private const string AniBehaviourFullName = "CreatorEngine.AniBehaviour";
    private const string AttributeFullName = "CreatorEngine.SerializeFieldAttribute";

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        IncrementalValuesProvider<BehaviourInfo?> candidates = context.SyntaxProvider
            .CreateSyntaxProvider(
                predicate: static (node, _) => node is ClassDeclarationSyntax { BaseList: not null },
                transform: static (ctx, _) => Analyze(ctx))
            .Where(static info => info is not null);

        context.RegisterSourceOutput(candidates.Collect(), Emit);
    }

    private static BehaviourInfo? Analyze(GeneratorSyntaxContext context)
    {
        var declaration = (ClassDeclarationSyntax)context.Node;
        if (context.SemanticModel.GetDeclaredSymbol(declaration) is not INamedTypeSymbol symbol)
            return null;

        if (symbol.IsAbstract) return null;

        // 두 계열을 한 번에 훑는다. Behaviour는 필드 접근자까지, AniBehaviour는 등록만 생성한다.
        bool isAni = InheritsFrom(symbol, AniBehaviourFullName);
        if (!isAni && !InheritsFrom(symbol, BehaviourFullName)) return null;

        var fields = new List<FieldInfo>();
        foreach (ISymbol member in symbol.GetMembers())
        {
            if (member is not IFieldSymbol field || field.IsStatic || field.IsConst)
                continue;

            if (!HasSerializeField(field))
                continue;

            FieldKind kind = Classify(field.Type);
            if (kind == FieldKind.Unsupported)
                continue;   // 지원하지 않는 타입은 조용히 건너뛴다(진단은 아래 Emit에서)

            fields.Add(new FieldInfo(field.Name, kind, GetDisplayName(field)));
        }

        bool isPartial = declaration.Modifiers.Any(m => m.ValueText == "partial");

        return new BehaviourInfo(
            Namespace: symbol.ContainingNamespace.IsGlobalNamespace ? null : symbol.ContainingNamespace.ToDisplayString(),
            TypeName: symbol.Name,
            FullName: symbol.ToDisplayString(),
            IsPartial: isPartial,
            IsAni: isAni,
            Fields: fields.ToImmutableArray());
    }

    private static bool InheritsFrom(INamedTypeSymbol symbol, string baseFullName)
    {
        for (INamedTypeSymbol? current = symbol.BaseType; current is not null; current = current.BaseType)
        {
            if (current.ToDisplayString() == baseFullName)
                return true;
        }
        return false;
    }

    private static bool HasSerializeField(IFieldSymbol field)
        => field.GetAttributes().Any(a => a.AttributeClass?.ToDisplayString() == AttributeFullName);

    private static string? GetDisplayName(IFieldSymbol field)
    {
        AttributeData? attribute = field.GetAttributes()
            .FirstOrDefault(a => a.AttributeClass?.ToDisplayString() == AttributeFullName);

        if (attribute is null) return null;

        foreach (KeyValuePair<string, TypedConstant> named in attribute.NamedArguments)
        {
            if (named.Key == "DisplayName" && named.Value.Value is string text && text.Length > 0)
                return text;
        }
        return null;
    }

    private static FieldKind Classify(ITypeSymbol type) => type.ToDisplayString() switch
    {
        "float"                    => FieldKind.Float,
        "int"                      => FieldKind.Int32,
        "bool"                     => FieldKind.Bool,
        "CreatorEngine.Float3"     => FieldKind.Float3,
        "CreatorEngine.Float2"     => FieldKind.Float2,
        "string"                   => FieldKind.String,
        "string?"                  => FieldKind.String,
        "CreatorEngine.GameObject" => FieldKind.Object,
        _                          => FieldKind.Unsupported,
    };

    private static void Emit(SourceProductionContext context, ImmutableArray<BehaviourInfo?> items)
    {
        var behaviours = items.Where(i => i is not null).Select(i => i!).ToList();
        if (behaviours.Count == 0)
            return;

        foreach (BehaviourInfo info in behaviours)
        {
            // partial이 아니면 부분 클래스를 덧붙일 수 없다. 필드가 없으면 알릴 필요도 없다.
            if (!info.IsPartial)
            {
                if (info.Fields.Length > 0)
                {
                    context.ReportDiagnostic(Diagnostic.Create(NotPartialRule, Location.None, info.TypeName));
                }
                continue;
            }

            if (info.Fields.Length > 0)
            {
                context.AddSource($"{info.TypeName}.Fields.g.cs", SourceText.From(EmitAccessors(info), Encoding.UTF8));
            }
        }

        context.AddSource("ScriptRegistry.g.cs",
            SourceText.From(EmitRegistrations(behaviours), Encoding.UTF8));
    }

    private static string EmitAccessors(BehaviourInfo info)
    {
        var sb = new StringBuilder();
        sb.AppendLine("// <auto-generated/> BehaviourGenerator가 만든 파일입니다. 직접 고치지 마세요.");
        sb.AppendLine("#nullable enable");
        sb.AppendLine();

        if (info.Namespace is not null)
        {
            sb.AppendLine($"namespace {info.Namespace};");
            sb.AppendLine();
        }

        sb.AppendLine($"partial class {info.TypeName}");
        sb.AppendLine("{");
        sb.AppendLine($"    public override int FieldCount => {info.Fields.Length};");
        sb.AppendLine();

        // 이름
        sb.AppendLine("    public override string GetFieldName(int index) => index switch");
        sb.AppendLine("    {");
        for (int i = 0; i < info.Fields.Length; ++i)
        {
            FieldInfo f = info.Fields[i];
            string label = f.DisplayName ?? Prettify(f.Name);
            sb.AppendLine($"        {i} => \"{label}\",");
        }
        sb.AppendLine("        _ => string.Empty,");
        sb.AppendLine("    };");
        sb.AppendLine();

        // 타입
        sb.AppendLine("    public override global::CreatorEngine.FieldType GetFieldType(int index) => index switch");
        sb.AppendLine("    {");
        for (int i = 0; i < info.Fields.Length; ++i)
        {
            sb.AppendLine($"        {i} => global::CreatorEngine.FieldType.{info.Fields[i].Kind},");
        }
        sb.AppendLine("        _ => global::CreatorEngine.FieldType.Unknown,");
        sb.AppendLine("    };");
        sb.AppendLine();

        EmitTypedAccessor(sb, info, FieldKind.Float,  "float",  "GetFloat",  "SetFloat",  "0f");
        EmitTypedAccessor(sb, info, FieldKind.Int32,  "int",    "GetInt32",  "SetInt32",  "0");
        EmitTypedAccessor(sb, info, FieldKind.Bool,   "bool",   "GetBool",   "SetBool",   "false");
        EmitTypedAccessor(sb, info, FieldKind.Float3, "global::CreatorEngine.Float3", "GetFloat3", "SetFloat3", "default");
        EmitTypedAccessor(sb, info, FieldKind.Float2, "global::CreatorEngine.Float2", "GetFloat2", "SetFloat2", "default");
        EmitTypedAccessor(sb, info, FieldKind.String, "string", "GetString", "SetString", "string.Empty");
        EmitTypedAccessor(sb, info, FieldKind.Object, "global::CreatorEngine.GameObject", "GetObject", "SetObject", "default");

        sb.AppendLine("}");
        return sb.ToString();
    }

    private static void EmitTypedAccessor(StringBuilder sb, BehaviourInfo info, FieldKind kind,
        string typeName, string getter, string setter, string fallback)
    {
        var matching = new List<(int Index, string Name)>();
        for (int i = 0; i < info.Fields.Length; ++i)
        {
            if (info.Fields[i].Kind == kind) matching.Add((i, info.Fields[i].Name));
        }

        if (matching.Count == 0) return;   // 해당 타입 필드가 없으면 기본 구현을 그대로 둔다

        sb.AppendLine($"    public override {typeName} {getter}(int index) => index switch");
        sb.AppendLine("    {");
        foreach ((int index, string name) in matching)
        {
            sb.AppendLine($"        {index} => {name},");
        }
        sb.AppendLine($"        _ => {fallback},");
        sb.AppendLine("    };");
        sb.AppendLine();

        sb.AppendLine($"    public override void {setter}(int index, {typeName} value)");
        sb.AppendLine("    {");
        sb.AppendLine("        switch (index)");
        sb.AppendLine("        {");
        foreach ((int index, string name) in matching)
        {
            sb.AppendLine($"            case {index}: {name} = value; break;");
        }
        sb.AppendLine("        }");
        sb.AppendLine("    }");
        sb.AppendLine();
    }

    private static string EmitRegistrations(List<BehaviourInfo> behaviours)
    {
        var sb = new StringBuilder();
        sb.AppendLine("// <auto-generated/> BehaviourGenerator가 만든 파일입니다. 직접 고치지 마세요.");
        sb.AppendLine("#nullable enable");
        sb.AppendLine();
        sb.AppendLine("namespace CreatorEngine.Generated;");
        sb.AppendLine();
        sb.AppendLine("/// <summary>");
        sb.AppendLine("/// 이 어셈블리에 있는 스크립트를 등록한다.");
        sb.AppendLine("///");
        sb.AppendLine("/// 스크립트 어셈블리는 언로드 가능한 컨텍스트에 올라가므로 ScriptCore의 표를");
        sb.AppendLine("/// 직접 건드리지 않는다. 대신 호스트가 이 진입점 하나만 찾아 호출하고,");
        sb.AppendLine("/// 등록은 넘겨받은 델리게이트로 수행한다.");
        sb.AppendLine("/// (리플렉션 사용을 이 메서드 하나로 묶어 두면 AOT 모드에서 직접 호출로 바꾸기 쉽다)");
        sb.AppendLine("/// </summary>");
        sb.AppendLine("public static class ScriptRegistry");
        sb.AppendLine("{");
        sb.AppendLine("    public static void RegisterAll(global::System.Action<string, global::System.Func<global::CreatorEngine.Behaviour>> register)");
        sb.AppendLine("    {");

        foreach (BehaviourInfo info in behaviours.Where(b => !b.IsAni).OrderBy(b => b.TypeName))
        {
            // 이름은 짧은 타입 이름으로 등록한다 — 네이티브가 붙일 때 쓰는 키다.
            sb.AppendLine($"        register(\"{info.TypeName}\", static () => new global::{info.FullName}());");
        }

        sb.AppendLine("    }");
        sb.AppendLine();

        // 애니메이션 상태 스크립트는 목록을 따로 둔다 — 수명을 상태 머신이 쥐고 있어
        // Behaviour와 섞으면 어느 팩토리로 만들지 구분할 수 없다.
        sb.AppendLine("    public static void RegisterAllAni(global::System.Action<string, global::System.Func<global::CreatorEngine.AniBehaviour>> register)");
        sb.AppendLine("    {");

        foreach (BehaviourInfo info in behaviours.Where(b => b.IsAni).OrderBy(b => b.TypeName))
        {
            sb.AppendLine($"        register(\"{info.TypeName}\", static () => new global::{info.FullName}());");
        }

        sb.AppendLine("    }");
        sb.AppendLine("}");
        return sb.ToString();
    }

    /// <summary>_moveSpeed → Move Speed 처럼 인스펙터에 보기 좋은 이름으로 다듬는다.</summary>
    private static string Prettify(string fieldName)
    {
        string name = fieldName.TrimStart('_');
        if (name.Length == 0) return fieldName;

        var sb = new StringBuilder();
        sb.Append(char.ToUpperInvariant(name[0]));

        for (int i = 1; i < name.Length; ++i)
        {
            if (char.IsUpper(name[i]) && !char.IsUpper(name[i - 1])) sb.Append(' ');
            sb.Append(name[i]);
        }
        return sb.ToString();
    }

    private static readonly DiagnosticDescriptor NotPartialRule = new(
        id: "CE001",
        title: "Behaviour에 partial이 필요합니다",
        messageFormat: "'{0}'에 [SerializeField] 필드가 있지만 partial이 아니라 접근자를 생성할 수 없습니다",
        category: "CreatorEngine",
        defaultSeverity: DiagnosticSeverity.Warning,
        isEnabledByDefault: true);

    // 값은 CreatorEngine.FieldType과 이름이 같아야 한다(생성 코드가 그대로 찍는다).
    private enum FieldKind { Unsupported, Float, Int32, Bool, Float3, String, Object, Float2 }

    private sealed record FieldInfo(string Name, FieldKind Kind, string? DisplayName);

    private sealed record BehaviourInfo(
        string? Namespace,
        string TypeName,
        string FullName,
        bool IsPartial,
        bool IsAni,
        ImmutableArray<FieldInfo> Fields);
}


