using System.Buffers.Binary;

namespace CreatorBuildTool
{
    internal static class BlueNoiseResource
    {
        internal const string RelativePath = "Resources/VolumetricFog/blueNoise.cetex";
        internal const string SourceRelativePath = "VolumetricFog/blueNoise.dds";

        internal static bool IsSource(string assetRelativePath)
        {
            return assetRelativePath.Equals(SourceRelativePath, StringComparison.OrdinalIgnoreCase)
                || assetRelativePath.Equals(SourceRelativePath + ".meta", StringComparison.OrdinalIgnoreCase);
        }

        internal static async Task Cook(BuildContext context, string cooker, string source, string output)
        {
            Paths.NoReparseAncestors(source);
            Paths.NoReparseAncestors(output);
            if (!File.Exists(source))
            {
                throw new BuildException("Built-in volumetric blue-noise source is missing: " + source);
            }
            var sourceHash = Metadata.Hash(source);
            await context.Run(cooker, ["--cook-builtin-textures", "--blue-noise-source", source, "--output", output]);
            if (Metadata.Hash(source) != sourceHash)
            {
                throw new BuildException("Built-in blue-noise source changed during cooking.");
            }
            Validate(output);
        }

        internal static void Validate(string path)
        {
            Paths.NoReparseAncestors(path);
            if (!File.Exists(path))
            {
                throw new BuildException("Required volumetric blue-noise CECT2 resource is missing: " + path
                    + ". Rebuild AssetCooker and publish the matching engine resources.");
            }
            // Fixed engine contract: 64-byte CECT2 header, eight 40-byte entries,
            // then the 21,872 authored BC3 bytes from the tracked 128x128 DDS.
            const int expectedBytes = 22256;
            using var file = File.OpenRead(path);
            if (file.Length != expectedBytes)
            {
                throw new BuildException("Built-in blue-noise resource has an invalid CECT2 size: " + path);
            }
            var bytes = new byte[expectedBytes];
            file.ReadExactly(bytes);
            uint U32(int offset) => BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(offset, 4));
            ulong U64(int offset) => BinaryPrimitives.ReadUInt64LittleEndian(bytes.AsSpan(offset, 8));
            // CECT wire format 9 is BC3Unorm; it is not the native RHI enum value.
            if (!bytes.AsSpan(0, 4).SequenceEqual("CECT"u8) || U32(4) != 2 || U32(8) != 64 || U32(12) != 2
                || U32(16) != 9 || (U32(20) & ~2u) != 4u || U32(24) != 128 || U32(28) != 128
                || U32(32) != 8 || U32(36) != 1 || U32(40) != 8 || U32(44) != 40
                || U64(48) != 384 || U64(56) != expectedBytes)
            {
                throw new BuildException("Built-in blue-noise resource must be CECT2 BC3Unorm 128x128 with eight authored mips: " + path);
            }
            ulong payload = 384;
            for (var mip = 0; mip < 8; ++mip)
            {
                var dimension = (uint)Math.Max(1, 128 >> mip);
                var blocks = (dimension + 3u) / 4u;
                var row = (ulong)blocks * 16u;
                var slice = row * blocks;
                var entry = 64 + mip * 40;
                if (U32(entry) != dimension || U32(entry + 4) != dimension || U64(entry + 8) != row
                    || U64(entry + 16) != slice || U64(entry + 24) != payload || U64(entry + 32) != slice)
                {
                    throw new BuildException("Built-in blue-noise CECT2 mip table is invalid: " + path);
                }
                payload += slice;
            }
            if (payload != expectedBytes)
            {
                throw new BuildException("Built-in blue-noise CECT2 payload is incomplete: " + path);
            }
        }
    }
}
