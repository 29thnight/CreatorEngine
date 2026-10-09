// Unrun source fixture. Compile with ScriptCore and its normal ScriptGenerator,
// then invoke the checks from a dedicated CoreCLR/NativeAOT test host. This file
// is deliberately outside GameScripts/ScriptCore globs and is not a game script.
using System;
using CreatorEngine;
using MeshLink = CreatorEngine.AssetLink<CreatorEngine.Mesh>;
using AuthoredLink = CreatorEngine.AssetLink<CreatorEngine.AuthoredMaterial>;

namespace AssetDepotRegression
{
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
        [SerializeField] private AssetLink<CodeMaterialProgram> _codeProgram;
        [SerializeField] private AuthoredLink _authoredMaterial;

        private const string Root = "11111111-1111-4111-8111-111111111111";
        private const string Child = "22222222-2222-8222-8222-222222222222";
        private const string Nil = "00000000-0000-0000-0000-000000000000";

        private static void Require(bool condition, string message)
        {
            if (!condition)
            {
                throw new InvalidOperationException(message);
            }
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
            RoundTrip<CodeMaterialProgram>(AssetKind.MaterialProgram);
            RoundTrip<AuthoredMaterial>(AssetKind.Material);
            var identity = AssetId.Parse(Root);
            Require(new AssetLink<MaterialProgram>(identity).ToString()
                == new AssetLink<CodeMaterialProgram>(identity).ToString(), "Program concrete view changed serialized identity");
            Require(new AssetLink<Material>(identity).ToString()
                == new AssetLink<AuthoredMaterial>(identity).ToString(), "Material concrete view changed serialized identity");

            var fixture = new ManagedKindFixture();
            AssetKind[] kinds = { AssetKind.Texture, AssetKind.Model, AssetKind.Mesh,
                AssetKind.Skeleton, AssetKind.AnimationClip, AssetKind.ShaderMeta,
                AssetKind.MaterialProgram, AssetKind.Material, AssetKind.MaterialProgram, AssetKind.Material };
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
        public static void VerifyWarmHostBindings(AssetMountId mount,
            AssetLink<MaterialProgram> graphProgram, AssetLink<CodeMaterialProgram> codeProgram,
            AssetLink<Material> graphMaterial, AssetLink<AuthoredMaterial> authoredMaterial)
        {
            VerifyWarmKind<Texture>(mount);
            VerifyWarmKind<Model>(mount);
            VerifyWarmKind<Mesh>(mount);
            VerifyWarmKind<Skeleton>(mount);
            VerifyWarmKind<AnimationClip>(mount);
            VerifyWarmKind<ShaderMeta>(mount);
            // Kind enumeration includes both representations; the fixture supplies
            // compatible exact links rather than assuming the first root can load.
            VerifyWarmLink(graphProgram);
            VerifyWarmLink(codeProgram);
            VerifyWarmLink(graphMaterial);
            VerifyWarmLink(authoredMaterial);
            VerifyWarmLink(new AssetLink<Material>(authoredMaterial.Asset, authoredMaterial.Subasset));
            VerifySameKindEnumeration<MaterialProgram, CodeMaterialProgram>(mount);
            VerifySameKindEnumeration<Material, AuthoredMaterial>(mount);
            Require(!AssetDepot.TryAcquire(new AssetLink<CodeMaterialProgram>(graphProgram.Asset), out var wrongCode)
                && wrongCode is null, "Graph CPU owner was acquired as a code program");
            Require(!AssetDepot.TryAcquire(new AssetLink<MaterialProgram>(codeProgram.Asset), out var wrongGraph)
                && wrongGraph is null, "Code CPU owner was acquired as a graph program");
            Require(!AssetDepot.TryAcquire(new AssetLink<AuthoredMaterial>(graphMaterial.Asset), out var wrongMaterial)
                && wrongMaterial is null, "Runtime graph facade was acquired as authored Material");
        }

        private static void VerifyWarmKind<T>(AssetMountId mount)
        {
            AssetLink<T>[] roots = AssetDepot.ListRootLinks<T>(mount);
            Require(roots.Length != 0, "Missing supported root in mounted fixture");
            VerifyWarmLink(roots[0]);
        }

        private static void VerifySameKindEnumeration<TFirst, TSecond>(AssetMountId mount)
        {
            var first = AssetDepot.ListRootLinks<TFirst>(mount);
            var second = AssetDepot.ListRootLinks<TSecond>(mount);
            Require(first.Length == second.Length, "Concrete view changed identity-only root enumeration");
            for (int index = 0; index < first.Length; ++index)
            {
                Require(first[index].ToString() == second[index].ToString(), "Concrete view changed a listed stable link");
            }
        }

        private static void VerifyWarmLink<T>(AssetLink<T> link)
        {
            Require(AssetDepot.TryAcquire(link, out var resident), "Fixture root is not warm");
            using (resident)
            using (var first = AssetDepot.RequestAsync(link))
            using (var second = AssetDepot.RequestAsync(link))
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
                        Require(AssetDepot.TryAcquire(link, out var retained), "Result owner no longer pins resident generation");
                        retained!.Dispose();
                    }
                }
            }
        }

        private sealed class Unregistered { }
    }

#if CREATOR_ASSET_DEPOT_INTERNAL_ABI_FIXTURE
    // Optional source-linked ScriptCore fixture, run only in an isolated harness
    // with no active engine session. Stubs test the POD call boundary, never
    // native owner behavior; the C++ fixture above remains the ownership test.
    internal static unsafe class NativeAbiFixture
    {
        private static uint _lastSelector;
        private static AssetKind _lastKind;
        private static uint _lastRole;
        private static int _requestCalls;
        private static int _residentCalls;
        private static int _legacyCalls;
        private static int _cameraCutCalls;

        private static void Require(bool condition, string message)
        {
            if (!condition)
            {
                throw new InvalidOperationException(message);
            }
        }

        internal static void VerifyVersionAndTypedEntries()
        {
            Require(!Native.IsReady, "ABI fixture requires a fresh isolated managed session");
            var table = new ScriptApiTable { Version = 35, StructSize = sizeof(ScriptApiTable) };
            Require(Native.ExpectedVersion == 36, "Fixture expects the intentional ABI 36 migration");
            Require(!Native.Bind(&table), "A version 35 table bound to managed API 36");
            table.Version = 36;
            Require(!Native.Bind(&table), "Missing typed entries were accepted");
            table.Asset_RequestTyped = &RequestStub;
            Require(!Native.Bind(&table), "Missing resident entry was accepted");
            table.Asset_TryAcquireTyped = &ResidentStub;
            Require(!Native.Bind(&table), "Missing camera-cut entry was accepted");
            table.Camera_NotifyCameraCut = &CameraCutStub;
            table.Asset_Request = &LegacyRequestStub;
            table.StructSize = sizeof(ScriptApiTable) - IntPtr.Size;
            Require(!Native.Bind(&table), "The old v35 table size without the camera-cut slot was accepted");
            table.StructSize = sizeof(ScriptApiTable);
            table.Version = 37;
            Require(!Native.Bind(&table), "An unknown future table version was accepted");
            table.Version = 36;
            Require(Native.Bind(&table), "Complete ABI 36 table failed to bind");
            try
            {
                _requestCalls = 0;
                _residentCalls = 0;
                _legacyCalls = 0;
                _cameraCutCalls = 0;
                Native.CameraNotifyCameraCut(default);
                Require(_cameraCutCalls == 1, "Camera cut did not cross the appended ABI entry");
                Probe<Texture>(3u);
                Probe<Model>(0x00010001u);
                Probe<Mesh>(0x00010002u);
                Probe<Skeleton>(0x00010003u);
                Probe<AnimationClip>(0x00010004u);
                Probe<ShaderMeta>(0x00010005u);
                Probe<MaterialProgram>(0x00010006u);
                Probe<Material>(0x00010007u);
                Probe<CodeMaterialProgram>(0x00010008u);
                Probe<AuthoredMaterial>(0x00010009u);
                Require(_requestCalls == 10 && _residentCalls == 10 && _legacyCalls == 0,
                    "Managed requests did not use the two separate appended entries");
            }
            finally
            {
                Native.Unbind();
            }
        }

        [System.Runtime.InteropServices.UnmanagedCallersOnly]
        private static void CameraCutStub(ObjectHandle handle)
        {
            ++_cameraCutCalls;
        }

        private static void Probe<T>(uint concreteType)
        {
            var link = new AssetLink<T>(AssetId.Parse("11111111-1111-4111-8111-111111111111"));
            Require(AssetType<T>.TokenType == concreteType, "Static concrete token registration changed");
            Require(!AssetDepot.TryAcquire(link, out var owner) && owner is null,
                "Resident miss fabricated a managed owner");
            Require(_lastSelector == concreteType && _lastKind == link.ExpectedKind && _lastRole == 0u,
                "Resident call changed identity or used options as a selector");
            bool rejected = false;
            try
            {
                using var request = AssetDepot.RequestAsync(link);
            }
            catch (NotSupportedException)
            {
                rejected = true;
            }
            Require(rejected && _lastSelector == concreteType && _lastKind == link.ExpectedKind && _lastRole == 0u,
                "Request call did not preserve explicit concrete view and stable link kind");
        }

        [System.Runtime.InteropServices.UnmanagedCallersOnly]
        private static int RequestStub(AssetLinkABI* link, uint concreteType,
            TextureAssetVariantABI* options, AssetToken* token)
        {
            *token = default;
            _lastSelector = concreteType;
            _lastKind = link->Kind;
            _lastRole = options->Role;
            ++_requestCalls;
            return (int)AssetBindingResult.UnsupportedType;
        }

        [System.Runtime.InteropServices.UnmanagedCallersOnly]
        private static int ResidentStub(AssetLinkABI* link, uint concreteType,
            TextureAssetVariantABI* options, AssetToken* token)
        {
            *token = default;
            _lastSelector = concreteType;
            _lastKind = link->Kind;
            _lastRole = options->Role;
            ++_residentCalls;
            return (int)AssetBindingResult.NotResident;
        }

        [System.Runtime.InteropServices.UnmanagedCallersOnly]
        private static int LegacyRequestStub(AssetLinkABI* link, TextureAssetVariantABI* options,
            int residentOnly, AssetToken* token)
        {
            *token = default;
            ++_legacyCalls;
            return (int)AssetBindingResult.InternalError;
        }
    }
#endif

}
