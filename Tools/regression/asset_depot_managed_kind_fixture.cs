// Unrun source fixture. Compile with ScriptCore and its normal ScriptGenerator,
// then invoke the checks from a dedicated CoreCLR/NativeAOT test host. This file
// is deliberately outside GameScripts/ScriptCore globs and is not a game script.
using System;
using CreatorEngine;
using MeshLink = CreatorEngine.AssetLink<CreatorEngine.Mesh>;

namespace AssetDepotRegression;

public partial class ManagedKindFixture : Component
{
    [SerializeField] private AssetLink<Texture> _texture;
    [SerializeField] private AssetLink<Model> _model;
    [SerializeField] private MeshLink _mesh;
    [SerializeField] private AssetLink<Skeleton> _skeleton;
    [SerializeField] private AssetLink<AnimationClip> _clip;
    [SerializeField] private AssetLink<ShaderMeta> _shader;
    [SerializeField] private AssetLink<MaterialProgram> _program;
    [SerializeField] private AssetLink<Material> _material;

    private const string Root = "11111111-1111-4111-8111-111111111111";
    private const string Child = "22222222-2222-8222-8222-222222222222";
    private const string Nil = "00000000-0000-0000-0000-000000000000";

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    // No host, mount, native request or resource residency is needed here.
    public static void VerifyStableSerialization()
    {
        RoundTrip<Texture>(AssetKind.Texture);
        RoundTrip<Model>(AssetKind.Model);
        RoundTrip<Mesh>(AssetKind.Mesh);
        RoundTrip<Skeleton>(AssetKind.Skeleton);
        RoundTrip<AnimationClip>(AssetKind.AnimationClip);
        RoundTrip<ShaderMeta>(AssetKind.ShaderMeta);
        RoundTrip<MaterialProgram>(AssetKind.MaterialProgram);
        RoundTrip<Material>(AssetKind.Material);

        var fixture = new ManagedKindFixture();
        AssetKind[] kinds = { AssetKind.Texture, AssetKind.Model, AssetKind.Mesh,
            AssetKind.Skeleton, AssetKind.AnimationClip, AssetKind.ShaderMeta,
            AssetKind.MaterialProgram, AssetKind.Material };
        Require(fixture.FieldCount == kinds.Length, "A lazy closed type was omitted by the source generator");
        for (int index = 0; index < kinds.Length; ++index)
        {
            Require(fixture.GetFieldType(index) == FieldType.AssetLink, "Field lost its typed-link classification");
            string expected = $"1:{(uint)kinds[index]}:{Root}:{Child}";
            fixture.SetString(index, expected);
            Require(fixture.GetString(index) == expected, "Generated setter parsed another closed generic type");
            uint otherKind = kinds[index] == AssetKind.Texture ? (uint)AssetKind.Material : (uint)AssetKind.Texture;
            fixture.SetString(index, $"1:{otherKind}:{Root}:{Child}");
            Require(fixture.GetString(index) == expected, "Wrong-kind text replaced the previous field");
            fixture.SetString(index, $"1:{(uint)kinds[index]}:{Nil}:{Child}");
            Require(fixture.GetString(index) == expected, "Orphan subasset text replaced the previous field");
            fixture.SetString(index, $"1:{(uint)kinds[index]}:{Nil}:{Nil}");
            Require(fixture.GetString(index) == $"1:{(uint)kinds[index]}:{Nil}:{Nil}", "Empty link did not roundtrip");
        }

        bool unsupported = false;
        try { _ = new AssetLink<Unregistered>(AssetId.Parse(Root)); }
        catch (NotSupportedException) { unsupported = true; }
        Require(unsupported, "An unregistered concrete managed type was accepted");

        bool variantRejected = false;
        try { _ = AssetDepot.RequestAsync(new AssetLink<Mesh>(AssetId.Parse(Root)), new TextureAssetVariant(Role: 1)); }
        catch (ArgumentException) { variantRejected = true; }
        Require(variantRejected, "A non-texture variant reached the native boundary");
    }

    private static void RoundTrip<T>(AssetKind kind)
    {
        var link = new AssetLink<T>(AssetId.Parse(Root), AssetId.Parse(Child));
        Require(link.ExpectedKind == kind && link.IsValid, "Wrong stable kind or identity");
        Require(link.ToString() == $"1:{(uint)kind}:{Root}:{Child}", "Residency/type token leaked into persisted identity");
        Require(AssetLink<T>.TryParse(link.ToString(), out var parsed) && parsed == link, "Typed link did not roundtrip");
        Require(AssetLink<T>.TryParse(default(AssetLink<T>).ToString(), out var empty) && !empty.IsValid,
            "Default nil link did not roundtrip");
        Require(!AssetLink<T>.TryParse($"2:{(uint)kind}:{Root}:{Child}", out _), "Unknown wire version accepted");
        Require(!AssetLink<T>.TryParse($"1:0{(uint)kind}:{Root}:{Child}", out _), "Noncanonical kind accepted");
    }

    // A separate dedicated host must mount and warm every fixture root through
    // the normal asynchronous scheduler before calling this game-thread check.
    // These direct closed calls additionally exercise NativeAOT request/handle code.
    public static void VerifyWarmHostBindings(AssetMountId mount)
    {
        VerifyWarmKind<Texture>(mount);
        VerifyWarmKind<Model>(mount);
        VerifyWarmKind<Mesh>(mount);
        VerifyWarmKind<Skeleton>(mount);
        VerifyWarmKind<AnimationClip>(mount);
        VerifyWarmKind<ShaderMeta>(mount);
        VerifyWarmKind<MaterialProgram>(mount);
        VerifyWarmKind<Material>(mount);
    }

    private static void VerifyWarmKind<T>(AssetMountId mount)
    {
        AssetLink<T>[] roots = AssetDepot.ListRootLinks<T>(mount);
        Require(roots.Length != 0, "Missing supported root in mounted fixture");
        Require(AssetDepot.TryAcquire(roots[0], out var resident), "Fixture root is not warm");
        using (resident)
        using (var first = AssetDepot.RequestAsync(roots[0]))
        using (var second = AssetDepot.RequestAsync(roots[0]))
        {
            first.Cancel();
            first.Dispose();
            first.Dispose();
            Require(second.Snapshot().Status == AssetRequestStatus.Ready, "Consumer cancellation leaked to a second warm request");
            Require(second.TryAcquireResult(out var ownerA), "Result acquisition failed");
            using (ownerA)
            {
                Require(second.TryAcquireResult(out var ownerB), "Second owner acquisition failed");
                using (ownerB)
                {
                    second.Dispose();
                    ownerA!.Dispose();
                    ownerA.Dispose();
                    Require(!ownerB!.IsDisposed, "Independent owner disposed with sibling or request");
                    Require(AssetDepot.TryAcquire(roots[0], out var retained), "Result owner no longer pins resident generation");
                    retained!.Dispose();
                }
            }
        }
    }

    private sealed class Unregistered { }
}
