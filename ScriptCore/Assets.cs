using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Threading;

namespace CreatorEngine
{
    /// <summary>A canonical stable identity. This value never pins a resident asset.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct AssetId : IEquatable<AssetId>
    {
        internal readonly ulong First;
        internal readonly ulong Second;

        public AssetId(Guid value)
        {
            Span<byte> bytes = stackalloc byte[16];
            value.TryWriteBytes(bytes, bigEndian: true, out _);
            First = BinaryPrimitives.ReadUInt64LittleEndian(bytes);
            Second = BinaryPrimitives.ReadUInt64LittleEndian(bytes[8..]);
        }

        public bool IsEmpty => First == 0 && Second == 0;
        public bool IsValid
        {
            get
            {
                ulong version = (First >> 52) & 15;
                return !IsEmpty && (version == 4 || version == 8) && (Second & 0xc0) == 0x80;
            }
        }

        public static AssetId Parse(string text)
        {
            if (!TryParse(text, out AssetId id))
            {
                throw new FormatException("Expected a canonical UUIDv4 or UUIDv8 asset ID.");
            }
            return id;
        }

        public static bool TryParse(string? text, out AssetId id)
        {
            id = default;
            if (!Guid.TryParseExact(text, "D", out Guid guid))
            {
                return false;
            }
            var candidate = new AssetId(guid);
            if ((!candidate.IsEmpty && !candidate.IsValid) || candidate.ToString() != text)
            {
                return false;
            }
            id = candidate;
            return true;
        }

        public override string ToString()
        {
            Span<byte> bytes = stackalloc byte[16];
            BinaryPrimitives.WriteUInt64LittleEndian(bytes, First);
            BinaryPrimitives.WriteUInt64LittleEndian(bytes[8..], Second);
            return new Guid(bytes, bigEndian: true).ToString("D");
        }

        public bool Equals(AssetId other) => First == other.First && Second == other.Second;
        public override bool Equals(object? obj) => obj is AssetId other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(First, Second);
        public static bool operator ==(AssetId left, AssetId right) => left.Equals(right);
        public static bool operator !=(AssetId left, AssetId right) => !left.Equals(right);
    }

    // Closed compile-time markers. These are never managed resource instances
    // or wrappers around native pointers; AssetHandle<T> owns the native token.
    public sealed class Texture { private Texture() { } }
    public sealed class Model { private Model() { } }
    public sealed class Mesh { private Mesh() { } }
    public sealed class Skeleton { private Skeleton() { } }
    public sealed class AnimationClip { private AnimationClip() { } }
    public sealed class ShaderMeta { private ShaderMeta() { } }
    public sealed class MaterialProgram { private MaterialProgram() { } }
    public sealed class Material { private Material() { } }
    public sealed class CodeMaterialProgram { private CodeMaterialProgram() { } }
    public sealed class AuthoredMaterial { private AuthoredMaterial() { } }

    // Stable manifest kinds, persisted by AssetLink<T>. Not residency/type tokens.
    public enum AssetKind : uint
    {
        Model = 1,
        Material = 2,
        Texture = 3,
        ShaderMeta = 4,
        MaterialProgram = 8,
        Mesh = 12,
        Skeleton = 13,
        AnimationClip = 14,
    }

    internal static class AssetType<T>
    {
        // Static, closed registration is visible to NativeAOT. No type-name
        // reflection, MakeGenericType, native RTTI or arbitrary kind casts.
        // Native token IDs prove the concrete stored C++ type independently of
        // its manifest kind. Keep in sync with kScriptAssetConcreteType<T>.
        private static (AssetKind Kind, uint TokenType) Registration => Register();
        internal static AssetKind Kind => Registration.Kind;
        internal static uint TokenType => Registration.TokenType;

        private static (AssetKind, uint) Register()
        {
            if (typeof(T) == typeof(Texture))
            {
                return (AssetKind.Texture, 3u);
            }
            if (typeof(T) == typeof(Model))
            {
                return (AssetKind.Model, 0x00010001u);
            }
            if (typeof(T) == typeof(Mesh))
            {
                return (AssetKind.Mesh, 0x00010002u);
            }
            if (typeof(T) == typeof(Skeleton))
            {
                return (AssetKind.Skeleton, 0x00010003u);
            }
            if (typeof(T) == typeof(AnimationClip))
            {
                return (AssetKind.AnimationClip, 0x00010004u);
            }
            if (typeof(T) == typeof(ShaderMeta))
            {
                return (AssetKind.ShaderMeta, 0x00010005u);
            }
            if (typeof(T) == typeof(MaterialProgram))
            {
                return (AssetKind.MaterialProgram, 0x00010006u);
            }
            if (typeof(T) == typeof(Material))
            {
                return (AssetKind.Material, 0x00010007u);
            }
            if (typeof(T) == typeof(CodeMaterialProgram))
            {
                return (AssetKind.MaterialProgram, 0x00010008u);
            }
            if (typeof(T) == typeof(AuthoredMaterial))
            {
                return (AssetKind.Material, 0x00010009u);
            }
            throw new NotSupportedException("This concrete type has no AssetDepot runtime binding.");
        }

        internal static void ValidateToken(AssetToken token, bool request)
        {
            uint expected = TokenType | (request ? 0x80000000u : 0u);
            if (token.Generation == 0u || token.Type != expected)
            {
                throw new InvalidOperationException("Native asset token concrete type/role mismatch.");
            }
        }
    }

    /// <summary>
    /// Serializable typed identity: asset ID, optional stable subasset ID and
    /// expected kind. Merely storing/deserializing a link never loads or pins it.
    /// </summary>
    public readonly struct AssetLink<T> : IEquatable<AssetLink<T>>
    {
        public AssetId Asset { get; }
        public AssetId Subasset { get; }
        public AssetKind ExpectedKind => AssetType<T>.Kind;
        public bool IsValid => Asset.IsValid && (Subasset.IsEmpty || Subasset.IsValid);

        public AssetLink(AssetId asset, AssetId subasset = default)
        {
            _ = AssetType<T>.Kind;
            if ((!asset.IsEmpty && !asset.IsValid) || (!subasset.IsEmpty && !subasset.IsValid)
                || (asset.IsEmpty && !subasset.IsEmpty))
            {
                throw new ArgumentException("Invalid stable asset/subasset identity.");
            }
            Asset = asset;
            Subasset = subasset;
        }

        // The format includes a wire version and expected kind, not CLR type
        // names, pointers, slot numbers, file paths or resident generations.
        public override string ToString() => $"1:{(uint)ExpectedKind}:{Asset}:{Subasset}";

        public static bool TryParse(string? text, out AssetLink<T> link)
        {
            link = default;
            string[] parts = text?.Split(':') ?? Array.Empty<string>();
            if (parts.Length != 4 || parts[0] != "1"
                || parts[1] != ((uint)AssetType<T>.Kind).ToString(CultureInfo.InvariantCulture)
                || !AssetId.TryParse(parts[2], out AssetId asset)
                || !AssetId.TryParse(parts[3], out AssetId subasset)
                || (asset.IsEmpty && !subasset.IsEmpty))
            {
                return false;
            }
            link = new AssetLink<T>(asset, subasset);
            return true;
        }

        public static AssetLink<T> Parse(string text)
        {
            if (!TryParse(text, out AssetLink<T> link))
            {
                throw new FormatException("Invalid AssetLink version, identity or expected kind.");
            }
            return link;
        }

        internal AssetLinkABI ToABI() => new() { Asset = Asset, Subasset = Subasset, Kind = ExpectedKind };
        public bool Equals(AssetLink<T> other) => Asset == other.Asset && Subasset == other.Subasset;
        public override bool Equals(object? obj) => obj is AssetLink<T> other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(Asset, Subasset);
        public static bool operator ==(AssetLink<T> left, AssetLink<T> right) => left.Equals(right);
        public static bool operator !=(AssetLink<T> left, AssetLink<T> right) => !left.Equals(right);
    }

    /// <summary>A non-owning mount identifier supplied by the native host.</summary>
    public readonly record struct AssetMountId(ulong Value)
    {
        public bool IsValid => Value != 0;
    }

    public enum TextureAssetColorSpace : uint { Source, Linear, Srgb }

    public readonly record struct TextureAssetVariant(
        TextureAssetColorSpace ColorSpace = TextureAssetColorSpace.Source,
        bool Compress = false, uint Role = 0);

    // CPU readiness only. Ready is not a GPU upload or fence-completion signal.
    public enum AssetRequestStatus { Pending, Ready, Failed, Cancelled, Stale }
    public enum AssetRequestError
    {
        None, InvalidLink, NotMounted, TypeMismatch, HardDependencyCycle,
        UnsupportedType, UnsupportedRepresentation, ReadFailed, IntegrityFailed,
        DecodeFailed, DependencyFailed, SubmissionFailed, ShuttingDown, RevisionChanged,
    }

    public readonly record struct AssetRequestSnapshot(
        AssetRequestStatus Status, AssetRequestError Error, string Message, bool IsWorkComplete);

    [StructLayout(LayoutKind.Sequential)]
    public readonly struct TextureDescriptor
    {
        public readonly uint Width;
        public readonly uint Height;
        public readonly uint MipLevels;
        public readonly uint ArraySize;
        private readonly uint _isCube;
        public bool IsCube => _isCube != 0;
    }

    /// <summary>
    /// An independently owned native strong reference to one immutable CPU
    /// generation. Dispose releases only this owner; it does not wait for the GPU.
    /// </summary>
    public sealed class AssetHandle<T> : IDisposable
    {
        private readonly AssetToken _token;
        private int _disposed;
        private AssetHandle(AssetToken token) { _token = token; }
        internal static AssetHandle<T> FromToken(AssetToken token)
        {
            try
            {
                AssetType<T>.ValidateToken(token, request: false);
                return new AssetHandle<T>(token);
            }
            catch
            {
                AssetReleaseQueue.ReleaseOrEnqueue(token);
                throw;
            }
        }
        public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

        internal AssetToken Token
        {
            get
            {
                ObjectDisposedException.ThrowIf(IsDisposed, this);
                return _token;
            }
        }

        public void Dispose()
        {
            if (Interlocked.CompareExchange(ref _disposed, 1, 0) != 0)
            {
                // A concurrent winner owns the release attempt. Do not suppress
                // finalization here: that attempt may fail and restore state.
                return;
            }
            try
            {
                AssetReleaseQueue.ReleaseOrEnqueue(_token);
            }
            catch
            {
                // Enqueue can allocate. Preserve the immutable token and make
                // Dispose retryable; finalization remains enabled on failure.
                Volatile.Write(ref _disposed, 0);
                throw;
            }
            GC.SuppressFinalize(this);
        }

        ~AssetHandle()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                // Finalizers enqueue POD only. No native call, GPU completion or
                // DataSystem access can run on the GC finalizer thread.
                try
                {
                    AssetReleaseQueue.Enqueue(_token);
                }
                catch
                {
                    // Even queue/type-initialization OOM must not escape a
                    // finalizer. EndAssetSession sweeps the native owner slot.
                }
            }
        }
    }

    /// <summary>
    /// A consumer-specific asynchronous request. Poll on the game thread. The
    /// native request retains its ready result until disposed; acquire a separate
    /// handle to keep that exact generation after disposing this request.
    /// </summary>
    public sealed class AssetRequest<T> : IDisposable
    {
        private readonly AssetToken _token;
        private int _disposed;
        private AssetRequest(AssetToken token) { _token = token; }
        internal static AssetRequest<T> FromToken(AssetToken token)
        {
            try
            {
                AssetType<T>.ValidateToken(token, request: true);
                return new AssetRequest<T>(token);
            }
            catch
            {
                AssetReleaseQueue.ReleaseOrEnqueue(token);
                throw;
            }
        }
        public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

        private AssetToken Token
        {
            get
            {
                ObjectDisposedException.ThrowIf(IsDisposed, this);
                return _token;
            }
        }

        public AssetRequestSnapshot Snapshot() => Native.AssetSnapshot(Token);
        public void Cancel() => Native.AssetCancel(Token);

        public bool TryAcquireResult(out AssetHandle<T>? asset)
        {
            if (!Native.AssetAcquireResult(Token, out AssetToken owner))
            {
                asset = null;
                return false;
            }
            asset = AssetHandle<T>.FromToken(owner);
            return true;
        }

        public void Dispose()
        {
            if (Interlocked.CompareExchange(ref _disposed, 1, 0) != 0)
            {
                // A concurrent winner owns the release attempt. Do not suppress
                // finalization here: that attempt may fail and restore state.
                return;
            }
            try
            {
                AssetReleaseQueue.ReleaseOrEnqueue(_token);
            }
            catch
            {
                // Enqueue can allocate. Preserve the immutable token and make
                // Dispose retryable; finalization remains enabled on failure.
                Volatile.Write(ref _disposed, 0);
                throw;
            }
            GC.SuppressFinalize(this);
        }

        ~AssetRequest()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                try
                {
                    AssetReleaseQueue.Enqueue(_token);
                }
                catch
                {
                    // No native call or allocation here. If enqueue fails,
                    // EndAssetSession cancels/releases the native request slot.
                }
            }
        }
    }

    /// <summary>Typed CPU AssetDepot bindings. Mount publication stays with the native host.</summary>
    public static class AssetDepot
    {
        public static AssetRequest<T> RequestAsync<T>(AssetLink<T> link, TextureAssetVariant variant = default)
        {
            ValidateVariant<T>(variant);
            return AssetRequest<T>.FromToken(Native.AssetRequestTyped(link.ToABI(), AssetType<T>.TokenType, variant, residentOnly: false));
        }

        public static bool TryAcquire<T>(AssetLink<T> link, out AssetHandle<T>? asset, TextureAssetVariant variant = default)
        {
            ValidateVariant<T>(variant);
            AssetToken token = Native.AssetRequestTyped(link.ToABI(), AssetType<T>.TokenType, variant, residentOnly: true);
            if (token.Generation == 0)
            {
                asset = null;
                return false;
            }
            asset = AssetHandle<T>.FromToken(token);
            return true;
        }

        private static void ValidateVariant<T>(TextureAssetVariant variant)
        {
            if (AssetType<T>.Kind != AssetKind.Texture && variant != default)
            {
                throw new ArgumentException("Texture variants apply only to Texture links.", nameof(variant));
            }
        }

        public static AssetLink<T>[] ListRootLinks<T>(AssetMountId mount)
        {
            AssetLinkABI[] values = Native.AssetListRoots(mount.Value, AssetType<T>.Kind);
            var links = new AssetLink<T>[values.Length];
            for (int index = 0; index < links.Length; ++index)
            {
                if (values[index].Kind != AssetType<T>.Kind || values[index].Reserved != 0)
                {
                    throw new InvalidOperationException("Native asset root kind/layout mismatch.");
                }
                links[index] = new AssetLink<T>(values[index].Asset, values[index].Subasset);
            }
            return links;
        }

        public static TextureDescriptor Describe(AssetHandle<Texture> texture)
        {
            ArgumentNullException.ThrowIfNull(texture);
            return Native.AssetReadTexture(texture.Token);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AssetLinkABI
    {
        internal AssetId Asset;
        internal AssetId Subasset;
        internal AssetKind Kind;
        internal uint Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct AssetToken
    {
        internal readonly uint Index;
        internal readonly uint Generation;
        internal readonly uint Type;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TextureAssetVariantABI
    {
        internal uint ColorSpace;
        internal uint Compress;
        internal uint Role;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AssetRequestSnapshotABI
    {
        internal AssetRequestStatus Status;
        internal AssetRequestError Error;
        internal int WorkComplete;
        internal int MessageBytes;
    }

    internal enum AssetBindingResult
    {
        Success, Unavailable, InvalidToken, InvalidLink, UnsupportedType,
        WrongThread, InvalidArgument, NotResident, InternalError,
    }

    internal static class AssetReleaseQueue
    {
        private static readonly ConcurrentQueue<AssetToken> Pending = new();

        internal static void Enqueue(AssetToken token) => Pending.Enqueue(token);

        internal static void ReleaseOrEnqueue(AssetToken token)
        {
            if (Native.IsReady && Native.IsGameThread)
            {
                Native.AssetRelease(token);
            }
            else
            {
                Enqueue(token);
            }
        }

        internal static void Drain()
        {
            // Called only at existing GT entry points. Old tokens left after
            // shutdown are rejected by native generations on the next session.
            while (Pending.TryDequeue(out AssetToken token))
            {
                Native.AssetRelease(token);
            }
        }
    }
}
