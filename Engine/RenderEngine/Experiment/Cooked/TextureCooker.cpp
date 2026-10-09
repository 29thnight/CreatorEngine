#include "CookedTexture.h"

#include <d3d11.h>
#include <DirectXTex.h>
#include <objbase.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <string_view>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        RHIFormat TextureCookerFormat(DXGI_FORMAT format)
        {
            switch (format)
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM: return RHIFormat::RGBA8Unorm;
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return RHIFormat::RGBA8UnormSrgb;
            case DXGI_FORMAT_B8G8R8A8_UNORM: return RHIFormat::BGRA8Unorm;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return RHIFormat::BGRA8UnormSrgb;
            case DXGI_FORMAT_R16G16B16A16_FLOAT: return RHIFormat::RGBA16Float;
            case DXGI_FORMAT_R32G32B32A32_FLOAT: return RHIFormat::RGBA32Float;
            case DXGI_FORMAT_BC1_UNORM: return RHIFormat::BC1Unorm;
            case DXGI_FORMAT_BC1_UNORM_SRGB: return RHIFormat::BC1UnormSrgb;
            case DXGI_FORMAT_BC3_UNORM: return RHIFormat::BC3Unorm;
            case DXGI_FORMAT_BC3_UNORM_SRGB: return RHIFormat::BC3UnormSrgb;
            case DXGI_FORMAT_BC5_UNORM: return RHIFormat::BC5Unorm;
            case DXGI_FORMAT_BC7_UNORM: return RHIFormat::BC7Unorm;
            case DXGI_FORMAT_BC7_UNORM_SRGB: return RHIFormat::BC7UnormSrgb;
            default: return RHIFormat::Unknown;
            }
        }

        bool TextureCookerIsHdr(DXGI_FORMAT format)
        {
            switch (format)
            {
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
            case DXGI_FORMAT_R32G32B32_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R32G32_FLOAT:
            case DXGI_FORMAT_R16G16_FLOAT:
            case DXGI_FORMAT_R32_FLOAT:
            case DXGI_FORMAT_R16_FLOAT:
            case DXGI_FORMAT_R11G11B10_FLOAT:
            case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC6H_SF16: return true;
            default: return false;
            }
        }

        std::size_t TextureCookerFullLevels(std::size_t width, std::size_t height)
        {
            std::size_t levels = 1u;
            for (auto size = (std::max)(width, height); size > 1u; size >>= 1u)
            {
                ++levels;
            }
            return levels;
        }

        bool TextureCookerBoundedMetadata(const DirectX::TexMetadata& metadata)
        {
            if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.depth != 1u
                || metadata.width == 0u || metadata.height == 0u
                || metadata.width > kCookedTextureMaxDimension || metadata.height > kCookedTextureMaxDimension
                || metadata.arraySize == 0u || metadata.arraySize > kCookedTextureMaxArraySize
                || metadata.mipLevels == 0u
                || metadata.mipLevels > TextureCookerFullLevels(metadata.width, metadata.height)
                || (metadata.IsCubemap() && (metadata.width != metadata.height || metadata.arraySize % 6u != 0u)))
            {
                return false;
            }
            std::uint64_t total = 0u;
            for (std::size_t mip = 0u; mip < metadata.mipLevels; ++mip)
            {
                std::size_t row = 0u;
                std::size_t slice = 0u;
                if (FAILED(DirectX::ComputePitch(metadata.format, (std::max)(std::size_t{ 1 }, metadata.width >> mip),
                    (std::max)(std::size_t{ 1 }, metadata.height >> mip), row, slice))
                    || slice > (kCookedTextureMaxBytes - total) / metadata.arraySize)
                {
                    return false;
                }
                total += static_cast<std::uint64_t>(slice) * metadata.arraySize;
            }
            return total != 0u;
        }

        bool TextureCookerDecode(std::span<const std::byte> bytes, DirectX::ScratchImage& image, std::string& failure)
        {
            if (bytes.empty() || bytes.size() > kCookedTextureMaxBytes)
            {
                failure = "Texture source is empty or exceeds the byte limit.";
                return false;
            }
            const auto startsWith = [&](std::string_view magic)
            {
                return bytes.size() >= magic.size() && std::memcmp(bytes.data(), magic.data(), magic.size()) == 0;
            };
            enum class SourceKind { Dds, Hdr, Wic, Tga };
            SourceKind kind = SourceKind::Wic;
            DirectX::TexMetadata metadata{};
            HRESULT result = E_FAIL;
            const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
            if (startsWith("DDS "))
            {
                kind = SourceKind::Dds;
                result = DirectX::GetMetadataFromDDSMemory(data, bytes.size(), DirectX::DDS_FLAGS_NONE, metadata);
            }
            else if (startsWith("#?RADIANCE") || startsWith("#?RGBE"))
            {
                kind = SourceKind::Hdr;
                result = DirectX::GetMetadataFromHDRMemory(data, bytes.size(), metadata);
            }
            else
            {
                result = DirectX::GetMetadataFromWICMemory(data, bytes.size(), DirectX::WIC_FLAGS_NONE, metadata);
                if (FAILED(result))
                {
                    kind = SourceKind::Tga;
                    result = DirectX::GetMetadataFromTGAMemory(data, bytes.size(), DirectX::TGA_FLAGS_NONE, metadata);
                }
            }
            if (FAILED(result) || !TextureCookerBoundedMetadata(metadata))
            {
                failure = "Texture source metadata is invalid, oversized, or not a supported 2D/array/cube image.";
                return false;
            }
            switch (kind)
            {
            case SourceKind::Dds:
                result = DirectX::LoadFromDDSMemory(data, bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, image);
                break;
            case SourceKind::Hdr:
                result = DirectX::LoadFromHDRMemory(data, bytes.size(), nullptr, image);
                break;
            case SourceKind::Wic:
                result = DirectX::LoadFromWICMemory(data, bytes.size(), DirectX::WIC_FLAGS_NONE, nullptr, image);
                break;
            case SourceKind::Tga:
                result = DirectX::LoadFromTGAMemory(data, bytes.size(), DirectX::TGA_FLAGS_NONE, nullptr, image);
                break;
            }
            if (FAILED(result) || !TextureCookerBoundedMetadata(image.GetMetadata()) || image.GetImageCount() == 0u)
            {
                failure = "DirectXTex could not decode the texture source.";
                return false;
            }
            return true;
        }

        bool TextureCookerConvert(const DirectX::ScratchImage& source, DXGI_FORMAT target,
            DirectX::ScratchImage& result)
        {
            const auto& metadata = source.GetMetadata();
            auto targetMetadata = metadata;
            targetMetadata.format = target;
            if (!TextureCookerBoundedMetadata(targetMetadata))
            {
                return false;
            }
            if (target == metadata.format)
            {
                if (FAILED(result.Initialize(metadata)))
                {
                    return false;
                }
                for (std::size_t index = 0u; index < source.GetImageCount(); ++index)
                {
                    const auto& from = source.GetImages()[index];
                    const auto& to = result.GetImages()[index];
                    CopyImageRows(reinterpret_cast<std::byte*>(to.pixels), to.rowPitch,
                        reinterpret_cast<const std::byte*>(from.pixels), from.rowPitch,
                        static_cast<std::uint32_t>(DirectX::ComputeScanlines(metadata.format, from.height)),
                        (std::min)(from.rowPitch, to.rowPitch));
                }
                return true;
            }
            // A format conversion here only changes channel layout/precision.
            // In particular R8/RG8 have no sRGB enum twin: Convert(R8, RGBA8_SRGB)
            // would gamma-encode authored bytes before their intended labeling.
            // Borrow linear-labeled descriptors for conversion, then restore the
            // selected transfer-function label. Mip/resize filtering is separate.
            auto linearMetadata = metadata;
            linearMetadata.format = DirectX::MakeLinear(metadata.format);
            std::vector<DirectX::Image> linearImages(source.GetImages(), source.GetImages() + source.GetImageCount());
            for (auto& image : linearImages)
            {
                image.format = linearMetadata.format;
            }
            const auto linearTarget = DirectX::MakeLinear(target);
            const HRESULT converted = DirectX::IsCompressed(linearMetadata.format)
                ? DirectX::Decompress(linearImages.data(), linearImages.size(), linearMetadata, linearTarget, result)
                : DirectX::Convert(linearImages.data(), linearImages.size(), linearMetadata, linearTarget,
                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, result);
            return SUCCEEDED(converted) && (target == linearTarget || result.OverrideFormat(target));
        }

        void TextureCookerCopyLevel(const DirectX::Image& source, const DirectX::Image& destination)
        {
            CopyImageRows(reinterpret_cast<std::byte*>(destination.pixels), destination.rowPitch,
                reinterpret_cast<const std::byte*>(source.pixels), source.rowPitch,
                static_cast<std::uint32_t>(DirectX::ComputeScanlines(destination.format, destination.height)),
                destination.rowPitch);
        }

        bool TextureCookerResize(const DirectX::ScratchImage& source, std::size_t width, std::size_t height,
            DirectX::ScratchImage& result)
        {
            auto metadata = source.GetMetadata();
            metadata.width = width;
            metadata.height = height;
            metadata.mipLevels = (std::min)(metadata.mipLevels, TextureCookerFullLevels(width, height));
            if (!TextureCookerBoundedMetadata(metadata) || FAILED(result.Initialize(metadata)))
            {
                return false;
            }
            // Preserve authored mip semantics: resize each authored level rather
            // than silently discarding the partial chain and regenerating it.
            for (std::size_t item = 0u; item < metadata.arraySize; ++item)
            {
                for (std::size_t mip = 0u; mip < metadata.mipLevels; ++mip)
                {
                    DirectX::ScratchImage level;
                    const auto* from = source.GetImage(mip, item, 0u);
                    const auto* to = result.GetImage(mip, item, 0u);
                    const auto flags = static_cast<DirectX::TEX_FILTER_FLAGS>(
                        DirectX::TEX_FILTER_FORCE_NON_WIC | DirectX::TEX_FILTER_SEPARATE_ALPHA);
                    if (!from || !to || FAILED(DirectX::Resize(*from, to->width, to->height, flags, level)))
                    {
                        return false;
                    }
                    TextureCookerCopyLevel(*level.GetImage(0u, 0u, 0u), *to);
                }
            }
            return true;
        }

        bool TextureCookerGenerateMissing(const DirectX::ScratchImage& source, DirectX::ScratchImage& result)
        {
            auto metadata = source.GetMetadata();
            const auto authoredLevels = metadata.mipLevels;
            metadata.mipLevels = TextureCookerFullLevels(metadata.width, metadata.height);
            if (!TextureCookerBoundedMetadata(metadata) || FAILED(result.Initialize(metadata)))
            {
                return false;
            }
            // Continue from the last authored mip instead of regenerating or
            // overwriting any authored level (including partial DDS chains).
            auto tailMetadata = source.GetMetadata();
            tailMetadata.width = (std::max)(std::size_t{ 1u }, metadata.width >> (authoredLevels - 1u));
            tailMetadata.height = (std::max)(std::size_t{ 1u }, metadata.height >> (authoredLevels - 1u));
            tailMetadata.mipLevels = 1u;
            std::vector<DirectX::Image> tailBases;
            tailBases.reserve(metadata.arraySize);
            for (std::size_t item = 0u; item < metadata.arraySize; ++item)
            {
                const auto* base = source.GetImage(authoredLevels - 1u, item, 0u);
                if (!base)
                {
                    return false;
                }
                tailBases.push_back(*base);
            }
            DirectX::ScratchImage tail;
            const auto flags = static_cast<DirectX::TEX_FILTER_FLAGS>(
                DirectX::TEX_FILTER_FORCE_NON_WIC | DirectX::TEX_FILTER_SEPARATE_ALPHA);
            if (FAILED(DirectX::GenerateMipMaps(tailBases.data(), tailBases.size(), tailMetadata, flags, 0u, tail))
                || tail.GetMetadata().mipLevels != metadata.mipLevels - authoredLevels + 1u)
            {
                return false;
            }
            for (std::size_t item = 0u; item < metadata.arraySize; ++item)
            {
                for (std::size_t mip = 0u; mip < metadata.mipLevels; ++mip)
                {
                    const auto* from = mip < authoredLevels ? source.GetImage(mip, item, 0u)
                        : tail.GetImage(mip - authoredLevels + 1u, item, 0u);
                    const auto* to = result.GetImage(mip, item, 0u);
                    if (!from || !to)
                    {
                        return false;
                    }
                    TextureCookerCopyLevel(*from, *to);
                }
            }
            return true;
        }

        bool TextureCookerNormalizeNormals(DirectX::ScratchImage& image, std::size_t firstMip, bool reconstructZ)
        {
            const auto& metadata = image.GetMetadata();
            if (metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM)
            {
                return false;
            }
            for (std::size_t item = 0u; item < metadata.arraySize; ++item)
            {
                for (std::size_t mip = firstMip; mip < metadata.mipLevels; ++mip)
                {
                    const auto* level = image.GetImage(mip, item, 0u);
                    for (std::size_t y = 0u; y < level->height; ++y)
                    {
                        for (std::size_t x = 0u; x < level->width; ++x)
                        {
                            auto* pixel = level->pixels + y * level->rowPitch + x * 4u;
                            float nx = static_cast<float>(pixel[0]) / 127.5f - 1.0f;
                            float ny = static_cast<float>(pixel[1]) / 127.5f - 1.0f;
                            float nz = reconstructZ ? std::sqrt((std::max)(0.0f, 1.0f - nx * nx - ny * ny))
                                : static_cast<float>(pixel[2]) / 127.5f - 1.0f;
                            const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                            if (length > 0.00001f)
                            {
                                nx /= length; ny /= length; nz /= length;
                            }
                            else
                            {
                                nx = 0.0f; ny = 0.0f; nz = 1.0f;
                            }
                            pixel[0] = static_cast<std::uint8_t>(std::clamp(nx * 127.5f + 127.5f, 0.0f, 255.0f) + 0.5f);
                            pixel[1] = static_cast<std::uint8_t>(std::clamp(ny * 127.5f + 127.5f, 0.0f, 255.0f) + 0.5f);
                            pixel[2] = static_cast<std::uint8_t>(std::clamp(nz * 127.5f + 127.5f, 0.0f, 255.0f) + 0.5f);
                        }
                    }
                }
            }
            return true;
        }

        double TextureCookerCoverage(const DirectX::Image& image, float cutoff, float scale)
        {
            std::size_t covered = 0u;
            for (std::size_t y = 0u; y < image.height; ++y)
            {
                for (std::size_t x = 0u; x < image.width; ++x)
                {
                    const float alpha = static_cast<float>(image.pixels[y * image.rowPitch + x * 4u + 3u]);
                    const auto quantized = static_cast<std::uint8_t>((std::min)(alpha * scale, 255.0f) + 0.5f);
                    if (static_cast<float>(quantized) / 255.0f >= cutoff)
                    {
                        ++covered;
                    }
                }
            }
            return static_cast<double>(covered) / static_cast<double>(image.width * image.height);
        }

        void TextureCookerAlphaCoverage(DirectX::ScratchImage& image, float cutoff, std::size_t firstMip)
        {
            const auto& metadata = image.GetMetadata();
            for (std::size_t item = 0u; item < metadata.arraySize; ++item)
            {
                const double target = TextureCookerCoverage(*image.GetImage(0u, item, 0u), cutoff, 1.0f);
                for (std::size_t mip = firstMip; mip < metadata.mipLevels; ++mip)
                {
                    const auto& level = *image.GetImage(mip, item, 0u);
                    float low = 0.0f;
                    float high = 255.0f;
                    float best = 1.0f;
                    double bestError = std::abs(TextureCookerCoverage(level, cutoff, best) - target);
                    // Fixed iterations and tie-breaking make the result reproducible.
                    for (std::size_t iteration = 0u; iteration < 24u; ++iteration)
                    {
                        const float scale = (low + high) * 0.5f;
                        const double coverage = TextureCookerCoverage(level, cutoff, scale);
                        const double error = std::abs(coverage - target);
                        if (error < bestError)
                        {
                            best = scale;
                            bestError = error;
                        }
                        if (coverage < target)
                        {
                            low = scale;
                        }
                        else
                        {
                            high = scale;
                        }
                    }
                    for (std::size_t y = 0u; y < level.height; ++y)
                    {
                        for (std::size_t x = 0u; x < level.width; ++x)
                        {
                            auto& alpha = level.pixels[y * level.rowPitch + x * 4u + 3u];
                            alpha = static_cast<std::uint8_t>((std::min)(static_cast<float>(alpha) * best, 255.0f) + 0.5f);
                        }
                    }
                }
            }
        }

        DirectX::TEX_COMPRESS_FLAGS TextureCookerCompressionFlags(TextureCompressionQuality quality, bool normalMap)
        {
            auto flags = DirectX::TEX_COMPRESS_DEFAULT;
            if (quality == TextureCompressionQuality::Fast)
            {
                flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(flags | DirectX::TEX_COMPRESS_BC7_QUICK);
            }
            else if (quality == TextureCompressionQuality::High)
            {
                flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(flags | DirectX::TEX_COMPRESS_BC7_USE_3SUBSETS);
            }
            if (normalMap)
            {
                flags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(flags | DirectX::TEX_COMPRESS_UNIFORM);
            }
            return flags;
        }
    }

    std::string TextureCookerFingerprint()
    {
        // Bump the algorithm token whenever filtering, alpha, normal, conversion,
        // decoder flags or compression policy changes. SDK upgrades invalidate it
        // independently; no compiler-specific object representation is hashed.
        return "texture-importer=2;cect=2;representation=2;algorithm=desktop-cpu-v2;directxtex="
            + std::to_string(DIRECTX_TEX_VERSION)
            + ";target=desktop-dx12-vulkan;decoder=wic-source-dds-hdr-tga;filter=non-wic-separate-alpha"
              ";normal=positive-z-renormalize;coverage=bisect24;bc=serial-cpu-aligned-base";
    }

    std::string TextureImportRecipe(const TextureImportSettings& settings)
    {
        // Decimal integer fields and IEEE float bits are locale-independent and
        // round-trip exactly; normalize negative zero because its bytes have no
        // effect on cutoff comparisons. All byte-affecting options are present.
        const auto cutoff = settings.alphaCutoff == 0.0f ? 0.0f : settings.alphaCutoff;
        return TextureCookerFingerprint()
            + ";colorSpace=" + std::to_string(static_cast<unsigned int>(settings.colorSpace))
            + ";compression=" + std::to_string(static_cast<unsigned int>(settings.compression))
            + ";mipPolicy=" + std::to_string(static_cast<unsigned int>(settings.mipPolicy))
            + ";maxDimension=" + std::to_string(settings.maxDimension)
            + ";normalMap=" + std::to_string(settings.normalMap)
            + ";preserveAlphaCoverage=" + std::to_string(settings.preserveAlphaCoverage)
            + ";alphaCutoffBits=" + std::to_string(std::bit_cast<std::uint32_t>(cutoff))
            + ";compressionQuality=" + std::to_string(static_cast<unsigned int>(settings.compressionQuality));
    }

    bool CookTexture(std::span<const std::byte> source, const TextureImportSettings& settings,
        std::vector<std::byte>& artifact, std::string& failure)
    {
        artifact.clear();
        if (!ValidateTextureImportSettings(settings, failure))
        {
            return false;
        }
        const auto fail = [&](const char* message)
        {
            failure = message;
            return false;
        };
        struct TextureCookerApartment final
        {
            HRESULT result{ CoInitializeEx(nullptr, COINIT_MULTITHREADED) };
            ~TextureCookerApartment()
            {
                if (SUCCEEDED(result))
                {
                    CoUninitialize();
                }
            }
        } apartment;
        if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
        {
            return fail("Texture cooker could not initialize COM for source decoding.");
        }
        try
        {
            DirectX::ScratchImage decoded;
            if (!TextureCookerDecode(source, decoded, failure))
            {
                return false;
            }
            auto sourceFormat = decoded.GetMetadata().format;
            const bool hdr = TextureCookerIsHdr(sourceFormat);
            if (hdr && (settings.colorSpace == TextureColorSpace::Srgb || settings.normalMap
                || settings.preserveAlphaCoverage || (settings.compression != TextureCompression::None
                    && settings.compression != TextureCompression::Auto)))
            {
                return fail("HDR requires linear floating-point output; selected LDR/normal/coverage policy is unsupported.");
            }
            // Color settings describe the transfer function of stored bytes; they
            // select sampling/filtering interpretation without changing mip zero.
            const bool srgb = !settings.normalMap && !hdr && (settings.colorSpace == TextureColorSpace::Srgb
                || (settings.colorSpace == TextureColorSpace::Source && DirectX::IsSRGB(sourceFormat)));
            const auto labeledFormat = srgb ? DirectX::MakeSRGB(sourceFormat) : DirectX::MakeLinear(sourceFormat);
            if (labeledFormat != sourceFormat && !decoded.OverrideFormat(labeledFormat))
            {
                return fail("Texture source cannot represent the requested color space.");
            }
            sourceFormat = decoded.GetMetadata().format;
            if (sourceFormat == DXGI_FORMAT_BC5_UNORM && settings.colorSpace == TextureColorSpace::Srgb)
            {
                return fail("BC5 normal data cannot use sRGB sampling.");
            }
            if (TextureCookerFormat(sourceFormat) == RHIFormat::Unknown)
            {
                DirectX::ScratchImage converted;
                const auto format = hdr ? DXGI_FORMAT_R32G32B32A32_FLOAT
                    : (srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
                if (!TextureCookerConvert(decoded, format, converted))
                {
                    return fail("Texture source cannot be converted to a supported GPU format.");
                }
                decoded = std::move(converted);
                sourceFormat = decoded.GetMetadata().format;
            }
            auto targetFormat = sourceFormat;
            switch (settings.compression)
            {
            case TextureCompression::None: break;
            case TextureCompression::Auto:
                targetFormat = hdr ? sourceFormat : (settings.normalMap ? DXGI_FORMAT_BC5_UNORM
                    : (srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM));
                break;
            case TextureCompression::BC1:
                targetFormat = srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
                break;
            case TextureCompression::BC3:
                targetFormat = srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
                break;
            case TextureCompression::BC5: targetFormat = DXGI_FORMAT_BC5_UNORM; break;
            case TextureCompression::BC7:
                targetFormat = srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
                break;
            }
            const auto& sourceMetadata = decoded.GetMetadata();
            auto width = sourceMetadata.width;
            auto height = sourceMetadata.height;
            const auto largest = (std::max)(width, height);
            const bool resize = settings.maxDimension != 0u && largest > settings.maxDimension;
            if (resize)
            {
                width = (std::max)(std::size_t{ 1u }, width * settings.maxDimension / largest);
                height = (std::max)(std::size_t{ 1u }, height * settings.maxDimension / largest);
            }
            if (DirectX::IsCompressed(targetFormat) && (width % 4u != 0u || height % 4u != 0u))
            {
                return fail("Desktop BC base dimensions must be multiples of 4. Resize the source to block-aligned dimensions or use uncompressed output.");
            }
            // PreserveAuthored leaves partial chains alone. Explicit GenerateFull
            // appends only missing levels, continuing from the final authored mip.
            const auto fullLevels = TextureCookerFullLevels(width, height);
            const auto authoredLevels = (std::min)(sourceMetadata.mipLevels, fullLevels);
            const bool generate = settings.mipPolicy == TextureMipPolicy::GenerateFull && authoredLevels < fullLevels;
            const bool normal = settings.normalMap || sourceFormat == DXGI_FORMAT_BC5_UNORM;
            const bool coverage = settings.preserveAlphaCoverage && (generate || resize);
            const bool transcode = targetFormat != sourceFormat || (settings.compression != TextureCompression::None && !hdr);
            const bool process = resize || generate || transcode;
            const DirectX::ScratchImage* finalImage = &decoded;
            DirectX::ScratchImage working;
            if (process)
            {
                const auto workingFormat = hdr ? sourceFormat
                    : (srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
                if (!TextureCookerConvert(decoded, workingFormat, working))
                {
                    return fail("Texture working-image conversion failed.");
                }
                if (normal && sourceFormat == DXGI_FORMAT_BC5_UNORM
                    && !TextureCookerNormalizeNormals(working, 0u, true))
                {
                    return fail("BC5 normal reconstruction failed.");
                }
                if (resize)
                {
                    DirectX::ScratchImage resized;
                    if (!TextureCookerResize(working, width, height, resized))
                    {
                        return fail("Texture resize failed.");
                    }
                    working = std::move(resized);
                }
                if (generate)
                {
                    DirectX::ScratchImage mips;
                    auto generatedMetadata = working.GetMetadata();
                    generatedMetadata.mipLevels = TextureCookerFullLevels(width, height);
                    if (!TextureCookerBoundedMetadata(generatedMetadata))
                    {
                        return fail("Generated mip chain exceeds the byte limit.");
                    }
                    if (!TextureCookerGenerateMissing(working, mips))
                    {
                        return fail("Texture mip generation failed.");
                    }
                    working = std::move(mips);
                }
                if (normal && (resize || generate)
                    && !TextureCookerNormalizeNormals(working, resize ? 0u : authoredLevels, false))
                {
                    return fail("Texture normal mip renormalization failed.");
                }
                if (coverage)
                {
                    TextureCookerAlphaCoverage(working, settings.alphaCutoff, resize ? 1u : authoredLevels);
                }
                if (DirectX::IsCompressed(targetFormat))
                {
                    DirectX::ScratchImage compressed;
                    if (FAILED(DirectX::Compress(working.GetImages(), working.GetImageCount(), working.GetMetadata(),
                        targetFormat, TextureCookerCompressionFlags(settings.compressionQuality, normal),
                        settings.alphaCutoff, compressed)))
                    {
                        return fail("DirectXTex CPU BC compression failed.");
                    }
                    working = std::move(compressed);
                }
                else if (working.GetMetadata().format != targetFormat)
                {
                    DirectX::ScratchImage converted;
                    if (!TextureCookerConvert(working, targetFormat, converted))
                    {
                        return fail("Texture output-format conversion failed.");
                    }
                    working = std::move(converted);
                }
                if (generate && !resize && targetFormat == sourceFormat)
                {
                    for (std::size_t item = 0u; item < sourceMetadata.arraySize; ++item)
                    {
                        for (std::size_t mip = 0u; mip < authoredLevels; ++mip)
                        {
                            TextureCookerCopyLevel(*decoded.GetImage(mip, item, 0u), *working.GetImage(mip, item, 0u));
                        }
                    }
                }
                finalImage = &working;
            }
            const auto& metadata = finalImage->GetMetadata();
            if (!TextureCookerBoundedMetadata(metadata))
            {
                return fail("Cooked texture exceeds the supported dimension or byte limits.");
            }
            std::vector<TextureSubimage> subresources;
            subresources.reserve(finalImage->GetImageCount());
            for (std::size_t index = 0u; index < finalImage->GetImageCount(); ++index)
            {
                const auto& image = finalImage->GetImages()[index];
                subresources.push_back({ reinterpret_cast<const std::byte*>(image.pixels), image.rowPitch,
                    image.slicePitch, static_cast<std::uint32_t>(image.width), static_cast<std::uint32_t>(image.height) });
            }
            const TextureImageView view(TextureCookerFormat(metadata.format), static_cast<std::uint32_t>(metadata.width),
                static_cast<std::uint32_t>(metadata.height), static_cast<std::uint32_t>(metadata.mipLevels),
                static_cast<std::uint32_t>(metadata.arraySize), metadata.IsCubemap(), subresources.data(),
                static_cast<std::uint32_t>(subresources.size()));
            const CookedTextureInfo info{ !finalImage->IsAlphaAllOpaque(),
                settings.colorSpace != TextureColorSpace::Source || resize || generate || transcode || normal
                    || sourceMetadata.mipLevels > 1u };
            return EncodeCookedTexture(view, artifact, failure, info);
        }
        catch (const std::exception& error)
        {
            failure = "Texture cooking failed: " + std::string(error.what());
            return false;
        }
    }
}
