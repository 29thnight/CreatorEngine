#include "TextureSourceProcessing.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "TextureCodecImage.h"
#include "../../Engine/EngineDiagnostics/ProfileScope.h"

#include <d3d11.h>
#include <DirectXTex.h>
#include <objbase.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    struct SourceImageApartment final
    {
        HRESULT result{ CoInitializeEx(nullptr, COINIT_MULTITHREADED) };
        ~SourceImageApartment()
        {
            if (SUCCEEDED(result))
            {
                CoUninitialize();
            }
        }
        bool Ready() const noexcept
        {
            return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
        }
    };

    struct SourceCodecImage final : Texture::CodecImage
    {
        DirectX::ScratchImage scratch;
    };

    RHIFormat TextureDecodedFormatToRHI(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:      return RHIFormat::RGBA8Unorm;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return RHIFormat::RGBA8UnormSrgb;
        case DXGI_FORMAT_B8G8R8A8_UNORM:      return RHIFormat::BGRA8Unorm;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return RHIFormat::BGRA8UnormSrgb;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:  return RHIFormat::RGBA16Float;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:  return RHIFormat::RGBA32Float;
        case DXGI_FORMAT_BC1_UNORM:           return RHIFormat::BC1Unorm;
        case DXGI_FORMAT_BC1_UNORM_SRGB:      return RHIFormat::BC1UnormSrgb;
        case DXGI_FORMAT_BC3_UNORM:           return RHIFormat::BC3Unorm;
        case DXGI_FORMAT_BC3_UNORM_SRGB:      return RHIFormat::BC3UnormSrgb;
        case DXGI_FORMAT_BC5_UNORM:           return RHIFormat::BC5Unorm;
        case DXGI_FORMAT_BC7_UNORM:           return RHIFormat::BC7Unorm;
        case DXGI_FORMAT_BC7_UNORM_SRGB:      return RHIFormat::BC7UnormSrgb;
        default:                              return RHIFormat::Unknown;
        }
    }

    DXGI_FORMAT TextureMipFormatFromRHI(RHIFormat format)
    {
        switch (format)
        {
        case RHIFormat::RGBA8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8UnormSrgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::BGRA8Unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8UnormSrgb: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHIFormat::RGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHIFormat::RGBA32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::BC1Unorm: return DXGI_FORMAT_BC1_UNORM;
        case RHIFormat::BC1UnormSrgb: return DXGI_FORMAT_BC1_UNORM_SRGB;
        case RHIFormat::BC3Unorm: return DXGI_FORMAT_BC3_UNORM;
        case RHIFormat::BC3UnormSrgb: return DXGI_FORMAT_BC3_UNORM_SRGB;
        case RHIFormat::BC5Unorm: return DXGI_FORMAT_BC5_UNORM;
        case RHIFormat::BC7Unorm: return DXGI_FORMAT_BC7_UNORM;
        case RHIFormat::BC7UnormSrgb: return DXGI_FORMAT_BC7_UNORM_SRGB;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    bool TextureIsUploadableShape(const DirectX::TexMetadata& metadata)
    {
        return DirectX::TEX_DIMENSION_TEXTURE2D == metadata.dimension
            && 0 != metadata.width && 0 != metadata.height
            && 0 != metadata.mipLevels && 0 != metadata.arraySize
            && metadata.width <= UINT32_MAX && metadata.height <= UINT32_MAX;
    }

    bool TextureAdoptScratch(SourceCodecImage& out, DirectX::ScratchImage&& image,
        RHIFormat format)
    {
        out.scratch = std::move(image);
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        const auto imageCount = out.scratch.GetImageCount();
        const auto tableBytes = imageCount > maximum / sizeof(DirectX::Image)
            ? maximum : imageCount * sizeof(DirectX::Image);
        out.sourceRetainedBytes = sizeof(SourceCodecImage) - sizeof(Texture::CodecImage);
        const auto add = [&](std::size_t bytes)
        {
            out.sourceRetainedBytes = bytes > maximum - out.sourceRetainedBytes
                ? maximum : out.sourceRetainedBytes + bytes;
        };
        add(out.scratch.GetPixelsSize());
        add(tableBytes);
        const DirectX::TexMetadata& metadata = out.scratch.GetMetadata();

        out.format = format;
        out.width = static_cast<uint32_t>(metadata.width);
        out.height = static_cast<uint32_t>(metadata.height);
        out.mipLevels = static_cast<uint32_t>(metadata.mipLevels);
        out.arraySize = static_cast<uint32_t>(metadata.arraySize);
        out.isCube = metadata.IsCubemap();

        out.subresources.clear();
        out.subresources.reserve(
            static_cast<size_t>(out.arraySize) * out.mipLevels);
        // item 바깥, mip 안쪽 — TextureImage.h 의 레이아웃 규약과 같다.
        for (uint32_t item = 0; item < out.arraySize; ++item)
        {
            for (uint32_t mip = 0; mip < out.mipLevels; ++mip)
            {
                const DirectX::Image* source = out.scratch.GetImage(mip, item, 0);
                if (nullptr == source || nullptr == source->pixels)
                {
                    return false;
                }
                TextureSubimage subresource;
                subresource.pixels = reinterpret_cast<const std::byte*>(source->pixels);
                subresource.rowPitch = source->rowPitch;
                subresource.slicePitch = source->slicePitch;
                subresource.width = static_cast<uint32_t>(source->width);
                subresource.height = static_cast<uint32_t>(source->height);
                out.subresources.push_back(subresource);
            }
        }
        return !out.subresources.empty();
    }

    bool TextureLowerToRgba8(const DirectX::ScratchImage& image, DirectX::ScratchImage& out)
    {
        const DirectX::TexMetadata& metadata = image.GetMetadata();
        const DXGI_FORMAT target = DirectX::IsSRGB(metadata.format)
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        const HRESULT lowering = DirectX::IsCompressed(metadata.format)
            ? DirectX::Decompress(image.GetImages(), image.GetImageCount(), metadata,
                target, out)
            : DirectX::Convert(image.GetImages(), image.GetImageCount(), metadata, target,
                DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, out);
        return SUCCEEDED(lowering) && 0 != out.GetImageCount();
    }

    own::shared_owner<const Texture::CodecImage> TextureMakeCodecImage(DirectX::ScratchImage&& image)
    {
        if (!TextureIsUploadableShape(image.GetMetadata()))
        {
            return {};
        }
        auto codec = own::make_shared<SourceCodecImage>();
        codec->hasAlpha = !image.IsAlphaAllOpaque();
        const RHIFormat known = TextureDecodedFormatToRHI(image.GetMetadata().format);
        if (known != RHIFormat::Unknown)
        {
            if (!TextureAdoptScratch(*codec, std::move(image), known))
            {
                return {};
            }
        }
        else
        {
            DirectX::ScratchImage lowered;
            if (!TextureLowerToRgba8(image, lowered))
            {
                return {};
            }
            const auto format = TextureDecodedFormatToRHI(lowered.GetMetadata().format);
            if (format == RHIFormat::Unknown || !TextureAdoptScratch(*codec, std::move(lowered), format))
            {
                return {};
            }
        }
        codec->Account();
        return std::move(codec);
    }

    bool TextureCopyScratchToImage(const DirectX::ScratchImage& image, RHIFormat format,
        TextureImage& out)
    {
        const DirectX::TexMetadata& metadata = image.GetMetadata();
        out = TextureImage::Allocate(format,
            static_cast<uint32_t>(metadata.width),
            static_cast<uint32_t>(metadata.height),
            static_cast<uint32_t>(metadata.arraySize),
            static_cast<uint32_t>(metadata.mipLevels),
            metadata.IsCubemap());
        if (!out.IsValid())
        {
            return false;
        }

        for (uint32_t item = 0; item < out.ArraySize(); ++item)
        {
            for (uint32_t mip = 0; mip < out.MipLevels(); ++mip)
            {
                const TextureSubimage* destination = out.Find(mip, item);
                const DirectX::Image* source = image.GetImage(mip, item, 0);
                if (nullptr == destination || nullptr == source
                    || nullptr == source->pixels)
                {
                    return false;
                }
                std::byte* destinationPixels = out.MutablePixelsAt(*destination);
                if (nullptr == destinationPixels)
                {
                    return false;
                }

                CopyImageRows(destinationPixels, destination->rowPitch,
                    reinterpret_cast<const std::byte*>(source->pixels),
                    source->rowPitch,
                    RHIFormatRowCount(format, destination->height),
                    destination->rowPitch);
            }
        }
        return true;
    }

    bool TextureDecodeImageBytes(std::span<const std::byte> bytes,
        DirectX::TexMetadata& metadata, DirectX::ScratchImage& image, bool& outAlreadyFinal)
    {
        ce::profile_scope profile{ ce::marker<"Texture.Decode">() };
        outAlreadyFinal = false;
        if (bytes.empty())
        {
            return false;
        }

        const auto startsWith = [&bytes](std::string_view magic)
        {
            if (bytes.size() < magic.size())
            {
                return false;
            }
            for (size_t index = 0; index < magic.size(); ++index)
            {
                if (static_cast<char>(bytes[index]) != magic[index])
                {
                    return false;
                }
            }
            return true;
        };

        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
        HRESULT result = E_FAIL;
        if (startsWith("DDS "))
        {
            result = DirectX::LoadFromDDSMemory(data, bytes.size(),
                DirectX::DDS_FLAGS_FORCE_RGB, &metadata, image);
            outAlreadyFinal = true;
        }
        else if (startsWith("#?RADIANCE") || startsWith("#?RGBE"))
        {
            result = DirectX::LoadFromHDRMemory(data, bytes.size(), &metadata, image);
            outAlreadyFinal = true;
        }
        else
        {
            result = DirectX::LoadFromWICMemory(data, bytes.size(),
                DirectX::WIC_FLAGS_IGNORE_SRGB, &metadata, image);
            if (FAILED(result))
            {
                result = DirectX::LoadFromTGAMemory(data, bytes.size(),
                    DirectX::TGA_FLAGS_NONE, &metadata, image);
            }
        }
        return SUCCEEDED(result) && 0 != image.GetImageCount();
    }

    own::shared_owner<const Texture::CodecImage> TextureGenerateMipImage(
        const TextureImageView& view,
        std::string& outFailure)
    {
        const SourceImageApartment apartment;
        if (!apartment.Ready())
        {
            outFailure = "Cannot initialize mip image COM apartment.";
            return {};
        }
        const auto fail = [&](std::string_view message) -> own::shared_owner<const Texture::CodecImage>
        {
            outFailure = message;
            return {};
        };
        const DXGI_FORMAT format = TextureMipFormatFromRHI(view.Format());
        if (format == DXGI_FORMAT_UNKNOWN)
        {
            return fail("Unsupported material mip format");
        }
        DirectX::TexMetadata metadata{};
        metadata.width = view.Width(); metadata.height = view.Height(); metadata.depth = 1;
        metadata.arraySize = view.ArraySize(); metadata.mipLevels = 1;
        metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D; metadata.format = format;
        if (view.IsCube())
        {
            metadata.miscFlags = DirectX::TEX_MISC_TEXTURECUBE;
        }
        std::vector<DirectX::Image> images;
        for (uint32_t item = 0; item < view.ArraySize(); ++item)
        {
            const auto* base = view.Find(0, item);
            if (!base || !base->pixels)
            {
                return fail("Mip source subresource is empty");
            }
            images.push_back({base->width, base->height, format, base->rowPitch,
                base->slicePitch, const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(base->pixels))});
        }
        DirectX::ScratchImage decoded;
        const DirectX::Image* input = images.data();
        size_t count = images.size();
        if (DirectX::IsCompressed(format))
        {
            // Keep the encoded transfer function while unpacking BC blocks.
            const auto rgba = DirectX::IsSRGB(format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            if (FAILED(DirectX::Decompress(input, count, metadata, rgba, decoded)))
            {
                return fail("Mip source decompression failed");
            }
            input = decoded.GetImages(); count = decoded.GetImageCount(); metadata = decoded.GetMetadata();
        }
        DirectX::ScratchImage chain;
        // Non-WIC keeps alpha independent and HDR unclamped. DirectXTex chooses box
        // for power-of-two sizes and linear for NPOT; sRGB formats imply RGB decode/encode.
        if (FAILED(DirectX::GenerateMipMaps(input, count, metadata, DirectX::TEX_FILTER_FORCE_NON_WIC, 0, chain)))
        {
            return fail("Material mip generation failed");
        }
        if (DirectX::IsCompressed(format))
        {
            DirectX::ScratchImage compressed;
            auto targetMetadata = chain.GetMetadata();
            targetMetadata.format = format;
            if (FAILED(compressed.Initialize(targetMetadata)))
            {
                return fail("Material mip allocation failed");
            }
            // Recompress only the added levels. Reprocessing mip 0 would spend most
            // of the compression time on blocks that must be copied back unchanged.
            for (uint32_t item = 0; item < view.ArraySize(); ++item)
            {
                for (size_t mip = 1; mip < targetMetadata.mipLevels; ++mip)
                {
                    DirectX::ScratchImage level;
                    const auto* inputLevel = chain.GetImage(mip, item, 0);
                    if (!inputLevel || FAILED(DirectX::Compress(*inputLevel, format, DirectX::TEX_COMPRESS_DEFAULT,
                            DirectX::TEX_THRESHOLD_DEFAULT, level)))
                    {
                        return fail("Material mip compression failed");
                    }
                    const auto* from = level.GetImage(0, 0, 0);
                    const auto* to = compressed.GetImage(mip, item, 0);
                    if (!from || !to)
                    {
                        return fail("Compressed mip level is missing");
                    }
                    CopyImageRows(reinterpret_cast<std::byte*>(to->pixels), to->rowPitch,
                        reinterpret_cast<const std::byte*>(from->pixels), from->rowPitch,
                        RHIFormatRowCount(view.Format(), static_cast<uint32_t>(to->height)), to->rowPitch);
                }
            }
            chain = std::move(compressed);
        }
        // Compression may choose different endpoints even for the same input. Restore
        // the original base bytes; only the added levels may have new BC blocks.
        for (uint32_t item = 0; item < view.ArraySize(); ++item)
        {
            const auto* base = view.Find(0, item);
            const auto* target = chain.GetImage(0, item, 0);
            if (!target)
            {
                return fail("Generated mip base is missing");
            }
            CopyImageRows(reinterpret_cast<std::byte*>(target->pixels), target->rowPitch,
                base->pixels, base->rowPitch, RHIFormatRowCount(view.Format(), base->height),
                static_cast<size_t>(RHIFormatRowPitch(view.Format(), base->width)));
        }
        auto codec = TextureMakeCodecImage(std::move(chain));
        if (!codec)
        {
            return fail("Generated mip chain is invalid");
        }
        return codec;
    }

    bool DecodeSourceTextureRgba8(std::span<const std::byte> bytes,
        TextureImage& outImage, std::string& outFailure)
    {
        const SourceImageApartment apartment;
        if (!apartment.Ready())
        {
            outFailure = "Cannot initialize source image COM apartment.";
            return false;
        }
        outFailure.clear();
        DirectX::ScratchImage image{};
        DirectX::TexMetadata metadata{};
        bool alreadyFinal = false;
        if (!TextureDecodeImageBytes(bytes, metadata, image, alreadyFinal))
        {
            outFailure = "image decoder가 픽셀을 만들지 못했다.";
            return false;
        }

        // ── 색공간은 라벨로만 정한다 ──────────────────────────────────
        //
        // ★ 여기 있던 코드는 target 을 semantic(_UNORM_SRGB)으로 잡고
        //   DirectX::Convert 를 불렀다. DirectXTex 는 **출력 포맷이 DirectX::IsSRGB 면
        //   SRGB_OUT 이 기본 on** 이라고 스스로 문서화한다
        //   (DirectXTex.h "if the output format type is DirectX::IsSRGB(), then
        //   SRGB_OUT is on by default"). 입력은 _UNORM 이라 SRGB_IN 이
        //   꺼진 채로, 이미 sRGB 로 인코딩된 PNG 바이트에 linear→sRGB
        //   인코드가 한 번 더 먹었다.
        //
        //   런타임 SRV 는 호출자가 정하는 semantic 포맷을 쓰므로 하드웨어가
        //   디코드를 한 번 한다. 두 연산이 정확히 상쇄돼 **셰이더가 받는
        //   알베도가 sRGB 바이트값 그대로**였다 — Gunner 본체 텍스처
        //   기준 밝기 2.1~3.7 배, 채도비 2.96 → 1.70 (43% 탈색).
        //
        //   고침은 "바이트를 건드리지 않는다"다. 레이아웃만 RGBA8 로
        //   맞추되 목표 포맷의 sRGB 성질을 **소스와 같게** 두면
        //   DirectXTex 가 전달 함수에 손대지 않는다. 최종 라벨은 호출자가
        //   색공간으로 따로 정한다.
        const DXGI_FORMAT layoutTarget = DirectX::IsSRGB(metadata.format)
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;

        DirectX::ScratchImage converted;
        const DirectX::ScratchImage* finalImage = &image;
        HRESULT conversion = S_OK;
        if (DirectX::IsCompressed(metadata.format))
        {
            conversion = DirectX::Decompress(image.GetImages(), image.GetImageCount(),
                metadata, layoutTarget, converted);
            finalImage = &converted;
        }
        else if (metadata.format != layoutTarget)
        {
            conversion = DirectX::Convert(image.GetImages(), image.GetImageCount(), metadata,
                layoutTarget, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted);
            finalImage = &converted;
        }
        if (FAILED(conversion) || 0 == finalImage->GetImageCount())
        {
            outFailure = "image를 backend-neutral RGBA8로 변환하지 못했다.";
            return false;
        }

        const DirectX::TexMetadata& finalMetadata = finalImage->GetMetadata();
        if (DirectX::TEX_DIMENSION_TEXTURE2D != finalMetadata.dimension
            || 0 == finalMetadata.width || 0 == finalMetadata.height
            || 0 == finalMetadata.mipLevels || 0 == finalMetadata.arraySize
            || finalMetadata.width > UINT32_MAX || finalMetadata.height > UINT32_MAX)
        {
            outFailure = "2D texture descriptor 범위를 벗어났다.";
            return false;
        }

        const RHIFormat format = TextureDecodedFormatToRHI(finalMetadata.format);
        if (RHIFormat::Unknown == format
            || !TextureCopyScratchToImage(*finalImage, format, outImage))
        {
            outFailure = "decoded texture subresource가 비었다.";
            return false;
        }
        return true;
    }

    own::shared_owner<const Texture::CodecImage> LoadSourceTexture(
        const file::path& path, std::span<const std::byte> bytes, TextureSourceCompression compression)
    {
        const SourceImageApartment apartment;
        if (!apartment.Ready())
        {
            return {};
        }
        DirectX::ScratchImage image;
        DirectX::TexMetadata metadata{};
        bool alreadyFinal{};
        if (path.empty())
        {
            if (!TextureDecodeImageBytes(bytes, metadata, image, alreadyFinal))
            {
                return {};
            }
        }
        else
        {
            ce::profile_scope profile{ ce::marker<"Texture.Decode">() };
            std::string extension = path.extension().string();
            for (char& character : extension)
            {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            HRESULT result{};
            if (extension == ".dds")
            {
                result = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_FORCE_RGB, &metadata, image);
                alreadyFinal = true;
            }
            else if (extension == ".hdr")
            {
                result = DirectX::LoadFromHDRFile(path.c_str(), &metadata, image);
                alreadyFinal = true;
            }
            else if (extension == ".tga")
            {
                result = DirectX::LoadFromTGAFile(path.c_str(), DirectX::TGA_FLAGS_NONE, &metadata, image);
            }
            else
            {
                result = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, &metadata, image);
            }
            if (FAILED(result))
            {
                return {};
            }
        }
        if (compression != TextureSourceCompression::None && !alreadyFinal && !DirectX::IsCompressed(metadata.format))
        {
            ce::profile_scope profile{ ce::marker<"Texture.CompressBC1">() };
            DirectX::ScratchImage compressed;
            const bool material = compression == TextureSourceCompression::MaterialColor;
            const auto format = compression == TextureSourceCompression::LegacyLinear
                ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_BC1_UNORM_SRGB;
            const auto flags = material
                ? static_cast<DirectX::TEX_COMPRESS_FLAGS>(DirectX::TEX_COMPRESS_SRGB |
                    DirectX::TEX_COMPRESS_DITHER | DirectX::TEX_COMPRESS_UNIFORM)
                : DirectX::TEX_COMPRESS_PARALLEL;
            if (FAILED(DirectX::Compress(image.GetImages(), image.GetImageCount(), metadata,
                format, flags, 0.5f, compressed)))
            {
                if (!path.empty())
                {
                    return {};
                }
            }
            else
            {
                image = std::move(compressed);
            }
        }
        return TextureMakeCodecImage(std::move(image));
    }

    bool load_terrain_image(const std::filesystem::path& path, DirectX::ScratchImage& image)
    {
        // Treat PNG channels as stored bytes: neither sRGB metadata nor alpha
        // opacity is permission to transform Terrain height or mask data.
        const auto flags = static_cast<DirectX::WIC_FLAGS>(
            DirectX::WIC_FLAGS_IGNORE_SRGB | DirectX::WIC_FLAGS_FORCE_RGB);
        std::string extension = path.extension().string();
        for (char& character : extension)
        {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        HRESULT loaded{};
        if (extension == ".hdr")
        {
            loaded = DirectX::LoadFromHDRFile(path.c_str(), nullptr, image);
        }
        else if (extension == ".tga")
        {
            loaded = DirectX::LoadFromTGAFile(path.c_str(),
                static_cast<DirectX::TGA_FLAGS>(DirectX::TGA_FLAGS_IGNORE_SRGB |
                    DirectX::TGA_FLAGS_ALLOW_ALL_ZERO_ALPHA), nullptr, image);
        }
        else
        {
            loaded = DirectX::LoadFromWICFile(path.c_str(), flags, nullptr, image);
        }
        if (FAILED(loaded))
        {
            return false;
        }
        const auto& metadata = image.GetMetadata();
        return metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D &&
            metadata.arraySize == 1 && metadata.mipLevels == 1 &&
            metadata.width > 0 && metadata.height > 0 &&
            metadata.width <= static_cast<size_t>((std::numeric_limits<int>::max)()) &&
            metadata.height <= static_cast<size_t>((std::numeric_limits<int>::max)());
    }

    const DirectX::Image* terrain_rgba8(const DirectX::ScratchImage& source,
        DirectX::ScratchImage& converted)
    {
        const auto* image = source.GetImage(0, 0, 0);
        if (!image || !image->pixels)
        {
            return nullptr;
        }
        if (image->format == DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            return image;
        }
        if (FAILED(DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM,
            DirectX::TEX_FILTER_FORCE_NON_WIC, DirectX::TEX_THRESHOLD_DEFAULT, converted)))
        {
            return nullptr;
        }
        return converted.GetImage(0, 0, 0);
    }

    bool terrain_gray8(const DirectX::ScratchImage& source, std::vector<uint8_t>& gray)
    {
        const auto* image = source.GetImage(0, 0, 0);
        if (!image || !image->pixels)
        {
            return false;
        }
        gray.resize(image->width * image->height);
        // Authored splats are gray8. Preserve their byte values and respect pitch.
        if (image->format == DXGI_FORMAT_R8_UNORM)
        {
            for (size_t row = 0; row < image->height; ++row)
            {
                std::memcpy(gray.data() + row * image->width,
                    image->pixels + row * image->rowPitch, image->width);
            }
            return true;
        }
        // The former mask decoder reduced 16-bit samples only after integer
        // luminance conversion, then discarded the low byte (no rounding).
        if (image->format == DXGI_FORMAT_R16_UNORM ||
            image->format == DXGI_FORMAT_R16G16B16A16_UNORM)
        {
            for (size_t row = 0; row < image->height; ++row)
            {
                const auto* pixels = reinterpret_cast<const uint16_t*>(
                    image->pixels + row * image->rowPitch);
                for (size_t column = 0; column < image->width; ++column)
                {
                    const uint32_t value = image->format == DXGI_FORMAT_R16_UNORM
                        ? pixels[column]
                        : (77u * pixels[column * 4] + 150u * pixels[column * 4 + 1] +
                            29u * pixels[column * 4 + 2]) >> 8;
                    gray[row * image->width + column] = static_cast<uint8_t>(value >> 8);
                }
            }
            return true;
        }
        if (image->format == DXGI_FORMAT_R32G32B32A32_FLOAT)
        {
            // Keep the HDR mask policy (average RGB, then gamma 1/2.2).
            // DirectXTex RGBE samples differ from the historical decoder;
            // cross-decoder HDR/JPEG parity remains unverified.
            for (size_t row = 0; row < image->height; ++row)
            {
                const auto* pixels = reinterpret_cast<const float*>(
                    image->pixels + row * image->rowPitch);
                for (size_t column = 0; column < image->width; ++column)
                {
                    const auto* pixel = pixels + column * 4;
                    const float average = (pixel[0] + pixel[1] + pixel[2]) / 3.0f;
                    if (!std::isfinite(average))
                    {
                        return false;
                    }
                    const float value = std::pow((std::max)(average, 0.0f), 1.0f / 2.2f) *
                        255.0f + 0.5f;
                    gray[row * image->width + column] = static_cast<uint8_t>(
                        std::clamp(value, 0.0f, 255.0f));
                }
            }
            return true;
        }
        DirectX::ScratchImage converted;
        image = terrain_rgba8(source, converted);
        if (!image || !image->pixels)
        {
            return false;
        }
        for (size_t row = 0; row < image->height; ++row)
        {
            const auto* pixels = image->pixels + row * image->rowPitch;
            for (size_t column = 0; column < image->width; ++column)
            {
                const auto* pixel = pixels + column * 4;
                // Keep the previous integer grayscale rule; WIC/DirectXTex's
                // color-to-luminance conversion has a different weighting.
                gray[row * image->width + column] = static_cast<uint8_t>(
                    (77u * pixel[0] + 150u * pixel[1] + 29u * pixel[2]) >> 8);
            }
        }
        return true;
    }

    bool ReadTerrainSourceImage(const file::path& path, TerrainSourceImageKind kind,
        TerrainSourceImage& result)
    {
        const SourceImageApartment apartment;
        if (!apartment.Ready())
        {
            return false;
        }
        DirectX::ScratchImage source;
        if (!load_terrain_image(path, source))
        {
            return false;
        }
        TerrainSourceImage decoded;
        decoded.width = static_cast<uint32_t>(source.GetMetadata().width);
        decoded.height = static_cast<uint32_t>(source.GetMetadata().height);
        if (kind == TerrainSourceImageKind::Gray8)
        {
            if (!terrain_gray8(source, decoded.gray))
            {
                return false;
            }
        }
        else
        {
            DirectX::ScratchImage converted;
            const auto* image = terrain_rgba8(source, converted);
            if (!image || !image->pixels)
            {
                return false;
            }
            decoded.heights.resize(image->width * image->height);
            for (size_t row = 0; row < image->height; ++row)
            {
                const auto* pixels = image->pixels + row * image->rowPitch;
                for (size_t column = 0; column < image->width; ++column)
                {
                    const auto* pixel = pixels + column * 4;
                    const uint32_t bits = (static_cast<uint32_t>(pixel[0]) << 24) |
                        (static_cast<uint32_t>(pixel[1]) << 16) |
                        (static_cast<uint32_t>(pixel[2]) << 8) | pixel[3];
                    // Alpha is the low byte of an exact float bit pattern.
                    static_assert(sizeof(float) == sizeof(bits));
                    std::memcpy(&decoded.heights[row * image->width + column], &bits, sizeof(bits));
                }
            }
        }
        result = std::move(decoded);
        return true;
    }
}

namespace Authoring
{
    void InstallTextureSourceProcessing() noexcept
    {
        AssetAuthoringPort::InstallSourceTextureLoader(&LoadSourceTexture);
        AssetAuthoringPort::InstallSourceTextureMipGenerator(&TextureGenerateMipImage);
        AssetAuthoringPort::InstallSourceTextureRgbaDecoder(&DecodeSourceTextureRgba8);
        AssetAuthoringPort::InstallTerrainSourceImageReader(&ReadTerrainSourceImage);
    }

    void UninstallTextureSourceProcessing() noexcept
    {
        AssetAuthoringPort::UninstallTerrainSourceImageReader(&ReadTerrainSourceImage);
        AssetAuthoringPort::UninstallSourceTextureRgbaDecoder(&DecodeSourceTextureRgba8);
        AssetAuthoringPort::UninstallSourceTextureMipGenerator(&TextureGenerateMipImage);
        AssetAuthoringPort::UninstallSourceTextureLoader(&LoadSourceTexture);
    }
}
