#include "CookedTexture.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>

namespace experiment::cooked
{
    namespace
    {
        // Wire values are independent from the append-only RHI enumeration.
        std::uint32_t CookedTextureWireFormat(RHIFormat format)
        {
            switch (format)
            {
            case RHIFormat::RGBA8Unorm: return 1u;
            case RHIFormat::RGBA8UnormSrgb: return 2u;
            case RHIFormat::BGRA8Unorm: return 3u;
            case RHIFormat::BGRA8UnormSrgb: return 4u;
            case RHIFormat::RGBA16Float: return 5u;
            case RHIFormat::RGBA32Float: return 6u;
            case RHIFormat::BC1Unorm: return 7u;
            case RHIFormat::BC1UnormSrgb: return 8u;
            case RHIFormat::BC3Unorm: return 9u;
            case RHIFormat::BC3UnormSrgb: return 10u;
            case RHIFormat::BC5Unorm: return 11u;
            case RHIFormat::BC7Unorm: return 12u;
            case RHIFormat::BC7UnormSrgb: return 13u;
            default: return 0u;
            }
        }

        RHIFormat CookedTextureRuntimeFormat(std::uint32_t format)
        {
            switch (format)
            {
            case 1u: return RHIFormat::RGBA8Unorm;
            case 2u: return RHIFormat::RGBA8UnormSrgb;
            case 3u: return RHIFormat::BGRA8Unorm;
            case 4u: return RHIFormat::BGRA8UnormSrgb;
            case 5u: return RHIFormat::RGBA16Float;
            case 6u: return RHIFormat::RGBA32Float;
            case 7u: return RHIFormat::BC1Unorm;
            case 8u: return RHIFormat::BC1UnormSrgb;
            case 9u: return RHIFormat::BC3Unorm;
            case 10u: return RHIFormat::BC3UnormSrgb;
            case 11u: return RHIFormat::BC5Unorm;
            case 12u: return RHIFormat::BC7Unorm;
            case 13u: return RHIFormat::BC7UnormSrgb;
            default: return RHIFormat::Unknown;
            }
        }

        bool CookedTextureShape(std::uint32_t width, std::uint32_t height,
            std::uint32_t mipLevels, std::uint32_t arraySize, bool cube)
        {
            if (width == 0u || height == 0u || width > kCookedTextureMaxDimension
                || height > kCookedTextureMaxDimension || arraySize == 0u
                || arraySize > kCookedTextureMaxArraySize || mipLevels == 0u)
            {
                return false;
            }
            std::uint32_t fullLevels = 1u;
            for (auto size = (std::max)(width, height); size > 1u; size >>= 1u)
            {
                ++fullLevels;
            }
            return mipLevels <= fullLevels && (!cube || (width == height && arraySize % 6u == 0u));
        }

        void CookedTexturePut(std::vector<std::byte>& bytes, std::size_t offset,
            std::uint64_t value, std::size_t count)
        {
            for (std::size_t index = 0u; index < count; ++index)
            {
                bytes[offset + index] = static_cast<std::byte>((value >> (index * 8u)) & 0xffu);
            }
        }

        std::uint64_t CookedTextureGet(std::span<const std::byte> bytes,
            std::size_t offset, std::size_t count)
        {
            std::uint64_t value = 0u;
            for (std::size_t index = 0u; index < count; ++index)
            {
                value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8u);
            }
            return value;
        }
    }

    bool EncodeCookedTexture(TextureImageView image, std::vector<std::byte>& artifact,
        std::string& failure, CookedTextureInfo info)
    {
        artifact.clear();
        failure.clear();
        const auto fail = [&](const char* message)
        {
            failure = message;
            return false;
        };
        if (image.IsEmpty() || CookedTextureWireFormat(image.Format()) == 0u
            || !CookedTextureShape(image.Width(), image.Height(), image.MipLevels(), image.ArraySize(), image.IsCube())
            || image.SubresourceCount() != image.MipLevels() * image.ArraySize()
            || (RHIFormatIsBlockCompressed(image.Format()) && (image.Width() % 4u != 0u || image.Height() % 4u != 0u)))
        {
            return fail("CECT: unsupported format or invalid texture shape.");
        }
        const std::uint64_t payloadOffset = kCookedTextureHeaderBytes
            + static_cast<std::uint64_t>(image.SubresourceCount()) * kCookedTextureSubresourceBytes;
        std::uint64_t total = payloadOffset;
        for (std::uint32_t index = 0u; index < image.SubresourceCount(); ++index)
        {
            const auto* subimage = image.At(index);
            const auto mip = index % image.MipLevels();
            const auto width = (std::max)(1u, image.Width() >> mip);
            const auto height = (std::max)(1u, image.Height() >> mip);
            const auto row = RHIFormatRowPitch(image.Format(), width);
            const auto slice = RHIFormatSlicePitch(image.Format(), width, height);
            const auto rows = RHIFormatRowCount(image.Format(), height);
            if (!subimage || !subimage->pixels || subimage->width != width || subimage->height != height
                || subimage->rowPitch < row || subimage->rowPitch > kCookedTextureMaxBytes
                || subimage->slicePitch < subimage->rowPitch * static_cast<std::uint64_t>(rows)
                || subimage->slicePitch > kCookedTextureMaxBytes || slice > kCookedTextureMaxBytes - total)
            {
                return fail("CECT: invalid source subresource or texture exceeds the byte limit.");
            }
            total += slice;
        }
        try
        {
            std::vector<std::byte> result(static_cast<std::size_t>(total));
            result[0] = std::byte{ 'C' }; result[1] = std::byte{ 'E' };
            result[2] = std::byte{ 'C' }; result[3] = std::byte{ 'T' };
            CookedTexturePut(result, 4u, kCookedTextureSchemaVersion, 4u);
            CookedTexturePut(result, 8u, kCookedTextureHeaderBytes, 4u);
            CookedTexturePut(result, 12u, kCookedTextureRepresentationVersion, 4u);
            CookedTexturePut(result, 16u, CookedTextureWireFormat(image.Format()), 4u);
            CookedTexturePut(result, 20u, (image.IsCube() ? 1u : 0u) | (info.hasAlpha ? 2u : 0u)
                | (info.colorSpaceLocked ? 4u : 0u), 4u);
            CookedTexturePut(result, 24u, image.Width(), 4u);
            CookedTexturePut(result, 28u, image.Height(), 4u);
            CookedTexturePut(result, 32u, image.MipLevels(), 4u);
            CookedTexturePut(result, 36u, image.ArraySize(), 4u);
            CookedTexturePut(result, 40u, image.SubresourceCount(), 4u);
            CookedTexturePut(result, 44u, kCookedTextureSubresourceBytes, 4u);
            CookedTexturePut(result, 48u, payloadOffset, 8u);
            CookedTexturePut(result, 56u, total, 8u);
            auto offset = payloadOffset;
            for (std::uint32_t index = 0u; index < image.SubresourceCount(); ++index)
            {
                const auto& subimage = *image.At(index);
                const auto row = RHIFormatRowPitch(image.Format(), subimage.width);
                const auto slice = RHIFormatSlicePitch(image.Format(), subimage.width, subimage.height);
                const auto entry = kCookedTextureHeaderBytes + static_cast<std::size_t>(index) * kCookedTextureSubresourceBytes;
                CookedTexturePut(result, entry, subimage.width, 4u);
                CookedTexturePut(result, entry + 4u, subimage.height, 4u);
                CookedTexturePut(result, entry + 8u, row, 8u);
                CookedTexturePut(result, entry + 16u, slice, 8u);
                CookedTexturePut(result, entry + 24u, offset, 8u);
                CookedTexturePut(result, entry + 32u, slice, 8u);
                CopyImageRows(result.data() + static_cast<std::size_t>(offset), static_cast<std::size_t>(row),
                    subimage.pixels, subimage.rowPitch, RHIFormatRowCount(image.Format(), subimage.height),
                    static_cast<std::size_t>(row));
                offset += slice;
            }
            artifact = std::move(result);
            return true;
        }
        catch (const std::exception&)
        {
            return fail("CECT: artifact allocation failed.");
        }
    }

    bool DecodeCookedTexture(std::span<const std::byte> artifact, TextureImage& image,
        std::string& failure, CookedTextureInfo* info)
    {
        image = {};
        failure.clear();
        if (info)
        {
            *info = {};
        }
        const auto fail = [&](const char* message)
        {
            failure = message;
            return false;
        };
        if (artifact.size() < kCookedTextureHeaderBytes || artifact.size() > kCookedTextureMaxBytes
            || artifact[0] != std::byte{ 'C' } || artifact[1] != std::byte{ 'E' }
            || artifact[2] != std::byte{ 'C' } || artifact[3] != std::byte{ 'T' })
        {
            return fail("CECT: missing header, invalid magic, or excessive artifact size.");
        }
        const auto get32 = [&](std::size_t offset)
        {
            return static_cast<std::uint32_t>(CookedTextureGet(artifact, offset, 4u));
        };
        if (get32(4u) != kCookedTextureSchemaVersion || get32(8u) != kCookedTextureHeaderBytes
            || get32(12u) != kCookedTextureRepresentationVersion || get32(44u) != kCookedTextureSubresourceBytes)
        {
            return fail("CECT: unsupported schema or representation; recook this texture.");
        }
        const auto format = CookedTextureRuntimeFormat(get32(16u));
        const auto flags = get32(20u);
        const auto width = get32(24u);
        const auto height = get32(28u);
        const auto mipLevels = get32(32u);
        const auto arraySize = get32(36u);
        const auto count = get32(40u);
        const bool cube = (flags & 1u) != 0u;
        if (format == RHIFormat::Unknown || (flags & ~7u) != 0u
            || !CookedTextureShape(width, height, mipLevels, arraySize, cube)
            || count != static_cast<std::uint64_t>(mipLevels) * arraySize
            || (RHIFormatIsBlockCompressed(format) && (width % 4u != 0u || height % 4u != 0u)))
        {
            return fail("CECT: invalid format, flags, dimensions, mip count, or array/cube shape.");
        }
        const std::uint64_t tableEnd = kCookedTextureHeaderBytes
            + static_cast<std::uint64_t>(count) * kCookedTextureSubresourceBytes;
        if (tableEnd > artifact.size() || CookedTextureGet(artifact, 48u, 8u) != tableEnd
            || CookedTextureGet(artifact, 56u, 8u) != artifact.size())
        {
            return fail("CECT: table range or exact artifact length is invalid.");
        }
        std::uint64_t offset = tableEnd;
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const auto entry = kCookedTextureHeaderBytes + static_cast<std::size_t>(index) * kCookedTextureSubresourceBytes;
            const auto mip = index % mipLevels;
            const auto mipWidth = (std::max)(1u, width >> mip);
            const auto mipHeight = (std::max)(1u, height >> mip);
            const auto row = RHIFormatRowPitch(format, mipWidth);
            const auto slice = RHIFormatSlicePitch(format, mipWidth, mipHeight);
            if (get32(entry) != mipWidth || get32(entry + 4u) != mipHeight
                || CookedTextureGet(artifact, entry + 8u, 8u) != row
                || CookedTextureGet(artifact, entry + 16u, 8u) != slice
                || CookedTextureGet(artifact, entry + 24u, 8u) != offset
                || CookedTextureGet(artifact, entry + 32u, 8u) != slice
                || slice > artifact.size() - offset)
            {
                return fail("CECT: invalid subresource dimensions, pitch, order, offset, or byte range.");
            }
            offset += slice;
        }
        if (offset != artifact.size())
        {
            return fail("CECT: trailing or missing payload bytes.");
        }
        try
        {
            auto decoded = TextureImage::Allocate(format, width, height, arraySize, mipLevels, cube);
            if (!decoded.IsValid())
            {
                return fail("CECT: pixel allocation failed.");
            }
            offset = tableEnd;
            for (std::uint32_t item = 0u; item < arraySize; ++item)
            {
                for (std::uint32_t mip = 0u; mip < mipLevels; ++mip)
                {
                    const auto* subimage = decoded.Find(mip, item);
                    auto* destination = decoded.MutablePixelsAt(*subimage);
                    std::memcpy(destination, artifact.data() + static_cast<std::size_t>(offset), subimage->slicePitch);
                    offset += subimage->slicePitch;
                }
            }
            image = std::move(decoded);
            if (info)
            {
                info->hasAlpha = (flags & 2u) != 0u;
                info->colorSpaceLocked = (flags & 4u) != 0u;
            }
            return true;
        }
        catch (const std::exception&)
        {
            return fail("CECT: pixel allocation failed.");
        }
    }
}
