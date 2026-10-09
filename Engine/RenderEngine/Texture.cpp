#include "Texture.h"
#include "TextureCodecImage.h"
#include "AssetDepot/TextureAssetRuntime.h"
#include "Experiment/Cooked/CookedTexture.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "PathFinder.h"
#include "Core.Memory.hpp"
#include "LogSystem.h"
#include <limits>
#include <atomic>
#include <fstream>

namespace
{
    std::atomic<std::size_t> TextureLiveImageBytes{};
    std::atomic<std::size_t> TextureNonRehydratableBytes{};
    std::atomic<std::size_t> TextureLiveImageCount{};
}

void Texture::CodecImage::Account()
{
    for (const auto& subresource : subresources)
    {
        accountedBytes += subresource.slicePitch;
    }
    TextureLiveImageBytes.fetch_add(accountedBytes, std::memory_order_relaxed);
    TextureLiveImageCount.fetch_add(1u, std::memory_order_relaxed);
    if (!key)
    {
        TextureNonRehydratableBytes.fetch_add(accountedBytes, std::memory_order_relaxed);
    }
}

Texture::CodecImage::~CodecImage()
{
    if (accountedBytes != 0u)
    {
        TextureLiveImageBytes.fetch_sub(accountedBytes, std::memory_order_relaxed);
        TextureLiveImageCount.fetch_sub(1u, std::memory_order_relaxed);
        if (!key)
        {
            TextureNonRehydratableBytes.fetch_sub(accountedBytes, std::memory_order_relaxed);
        }
    }
}

namespace
{
    bool TextureAdoptImage(Texture::CodecImage& out, TextureImage&& image)
    {
        if (!image.IsValid())
        {
            return false;
        }
        out.owned = std::move(image);

        out.format = out.owned.Format();
        out.width = out.owned.Width();
        out.height = out.owned.Height();
        out.mipLevels = out.owned.MipLevels();
        out.arraySize = out.owned.ArraySize();
        out.isCube = out.owned.IsCube();

        out.subresources.clear();
        out.subresources.reserve(
            static_cast<size_t>(out.arraySize) * out.mipLevels);
        for (uint32_t item = 0; item < out.arraySize; ++item)
        {
            for (uint32_t mip = 0; mip < out.mipLevels; ++mip)
            {
                const TextureSubimage* source = out.owned.Find(mip, item);
                if (nullptr == source || nullptr == source->pixels)
                {
                    return false;
                }
                out.subresources.push_back(*source);
            }
        }
        return !out.subresources.empty();
    }

    own::shared_owner<const Texture::CodecImage> TextureMakeCodecImage(TextureImage&& image)
    {
        auto codec = own::make_shared<Texture::CodecImage>();
        if (!TextureAdoptImage(*codec, std::move(image)))
        {
            return {};
        }
        codec->hasAlpha = RHIFormatChannels(codec->format) == 4u
            || RHIFormatIsBlockCompressed(codec->format);
        codec->Account();
        return std::move(codec);
    }
}

TextureImageDescription Texture::GetImageDescription() const noexcept
{
    return m_imageDescription;
}

TextureImageView Texture::GetImageView(const own::shared_owner<const CodecImage>& image) const
{
    if (!image || image->subresources.empty())
    {
        return {};
    }
    if (m_assetOrigin && (!image->key || *image->key != m_assetOrigin->imageKey))
    {
        return {};
    }
    if (!m_assetOrigin && (!m_nonRehydratableImage
        || &*image.borrow() != &*m_nonRehydratableImage.borrow()))
    {
        return {};
    }
    const CodecImage& codec = *image;
    return TextureImageView(m_imageDescription.format, codec.width, codec.height,
        codec.mipLevels, codec.arraySize, codec.isCube, codec.subresources.data(),
        static_cast<uint32_t>(codec.subresources.size()));
}

own::shared_owner<const Texture::CodecImage> Texture::NonRehydratableImage() const
{
    return m_nonRehydratableImage;
}

void Texture::SetNonRehydratableImage(own::shared_owner<const CodecImage> image)
{
    m_nonRehydratableImage = std::move(image);
    if (m_nonRehydratableImage)
    {
        const auto& codec = *m_nonRehydratableImage;
        m_imageDescription = { codec.format, codec.width, codec.height,
            codec.mipLevels, codec.arraySize, codec.isCube };
        m_decodedBytes = codec.accountedBytes;
    }
}

own::shared_owner<const Texture> Texture::WithColorSpace(
    const own::shared_owner<const Texture>& source, bool srgb)
{
    if (!source || source->GetImageDescription().IsEmpty())
    {
        return nullptr;
    }
    const auto format = source->GetImageDescription().Format();
    RHIFormat target = format;
    switch (format)
    {
    case RHIFormat::RGBA8Unorm: case RHIFormat::RGBA8UnormSrgb:
        target = srgb ? RHIFormat::RGBA8UnormSrgb : RHIFormat::RGBA8Unorm; break;
    case RHIFormat::BGRA8Unorm: case RHIFormat::BGRA8UnormSrgb:
        target = srgb ? RHIFormat::BGRA8UnormSrgb : RHIFormat::BGRA8Unorm; break;
    case RHIFormat::BC1Unorm: case RHIFormat::BC1UnormSrgb:
        target = srgb ? RHIFormat::BC1UnormSrgb : RHIFormat::BC1Unorm; break;
    case RHIFormat::BC3Unorm: case RHIFormat::BC3UnormSrgb:
        target = srgb ? RHIFormat::BC3UnormSrgb : RHIFormat::BC3Unorm; break;
    case RHIFormat::BC7Unorm: case RHIFormat::BC7UnormSrgb:
        target = srgb ? RHIFormat::BC7UnormSrgb : RHIFormat::BC7Unorm; break;
    // Floating-point HDR images are already linear radiance.
    case RHIFormat::RGBA16Float: case RHIFormat::RGBA32Float: break;
    default:
        if (srgb)
        {
            return nullptr;
        }
        break;
    }
    if (target == format)
    {
        return source;
    }
    if (source->m_cookedColorSpaceLocked)
    {
        // Filtering/compression was performed under an explicit offline recipe.
        // A role requiring different pixels needs a separately cooked asset.
        return {};
    }
    auto texture = own::make_shared<Texture>();
    texture->m_nonRehydratableImage = source->m_nonRehydratableImage;
    texture->m_imageDescription = source->m_imageDescription;
    texture->m_imageDescription.format = target;
    texture->m_decodedBytes = source->m_decodedBytes;
    texture->m_cookedPayload = source->m_cookedPayload;
    texture->m_cookedColorSpaceLocked = source->m_cookedColorSpaceLocked;
    if (source->m_assetOrigin)
    {
        auto origin = *source->m_assetOrigin;
        origin.variant.colorSpace = srgb ? AssetDepot::TextureAssetColorSpace::Srgb
            : AssetDepot::TextureAssetColorSpace::Linear;
        texture->m_assetOrigin = own::make_shared<const AssetDepot::TextureAssetOrigin>(std::move(origin));
    }
    texture->m_samplingFormat = target;
    texture->m_textureType = source->m_textureType;
    texture->m_name = source->m_name;
    texture->m_extension = source->m_extension;
    texture->m_assetPath = source->m_assetPath;
    texture->m_size = source->m_size;
    texture->m_isTextureAlpha = source->m_isTextureAlpha;
    return std::move(texture);
}

own::shared_owner<const Texture> Texture::WithMipChain(
    const own::shared_owner<const Texture>& source, std::string& outFailure)
{
    outFailure.clear();
    if (!source || source->GetImageDescription().IsEmpty())
    {
        outFailure = "Mip source is empty";
        return {};
    }
    // Cooked imports own their mip policy, including an intentional single mip
    // or authored partial chain. Runtime never changes their exact image recipe.
    if (source->m_assetOrigin || source->m_cookedPayload)
    {
        return source;
    }
    const auto description = source->GetImageDescription();
    if (description.MipLevels() > 1u || (description.Width() == 1u && description.Height() == 1u))
    {
        return source;
    }
    auto texture = own::make_shared<Texture>();
    texture->m_textureType = source->m_textureType;
    texture->m_name = source->m_name;
    texture->m_extension = source->m_extension;
    texture->m_assetPath = source->m_assetPath;
    texture->m_size = source->m_size;
    texture->m_isTextureAlpha = source->m_isTextureAlpha;
    texture->m_samplingFormat = source->m_samplingFormat;
    const auto image = source->NonRehydratableImage();
    const auto codec = AssetAuthoringPort::GenerateSourceTextureMips(source->GetImageView(image), outFailure);
    if (!codec)
    {
        return {};
    }
    texture->SetNonRehydratableImage(codec);
    return std::move(texture);
}

own::shared_owner<const Texture> Texture::CreateFromPixels(_In_ uint32 width, _In_ uint32 height,
    _In_ std::string_view name, _In_ RHIFormat textureFormat,
    _In_reads_bytes_(rowPitch* height) const void* pixels, _In_opt_ size_t rowPitch)
{
    if (0 == width || 0 == height || nullptr == pixels)
    {
        return nullptr;
    }

    // ★ 예전에는 여기서도 ScratchImage 를 세웠다(축 A 전). 만들려는 것이
    //   CPU 픽셀 한 장뿐인데 디코더 라이브러리의 컨테이너를 거칠 이유가
    //   없다 — 지금은 중립 이미지를 직접 잡는다.
    TextureImage image = TextureImage::Allocate(textureFormat, width, height, 1, 1);
    if (!image.IsValid())
    {
        return nullptr;
    }

    const TextureSubimage* destination = image.Find(0, 0);
    if (nullptr == destination)
    {
        return nullptr;
    }
    std::byte* destinationPixels = image.MutablePixelsAt(*destination);
    if (nullptr == destinationPixels)
    {
        return nullptr;
    }

    // 원본 행 간격을 안 주면 빈틈없이 채워진 것으로 본다.
    //
    // ★ 행 단위로 옮긴다. 대상의 행 간격은 정렬 때문에 원본보다 클 수
    //   있고, 그때 통째로 memcpy하면 그림이 한 행씩 밀려 비스듬해진다 —
    //   1x1에서는 안 드러나고 폭이 커지는 순간 나타나는 부류다.
    const size_t sourcePitch = (0 != rowPitch)
        ? rowPitch : static_cast<size_t>(RHIFormatRowPitch(textureFormat, width));

    CopyImageRows(destinationPixels, destination->rowPitch,
        static_cast<const std::byte*>(pixels), sourcePitch,
        RHIFormatRowCount(textureFormat, height), destination->rowPitch);

    auto texture = own::make_shared<Texture>();
    texture->m_name = std::string(name);
    texture->m_textureType = TextureType::ImageTexture;
    texture->m_size = { float(width), float(height) };

    // 파일 로더가 남기는 자리와 같다 — 텍스처 캐시가 여기서 가져간다(T1·T4).
    texture->SetNonRehydratableImage(TextureMakeCodecImage(std::move(image)));
    if (!texture->m_nonRehydratableImage)
    {
        return nullptr;
    }

    return std::move(texture);
}

own::shared_owner<const Texture> Texture::CreateSharedFromImage(
    std::string_view name, TextureImage image)
{
    if (!image.IsValid())
    {
        return nullptr;
    }

    const bool isCube = image.IsCube();
    const uint32_t arraySize = image.ArraySize();
    const float width = float(image.Width());
    const float height = float(image.Height());
    const RHIFormat format = image.Format();

    own::shared_owner<const CodecImage> codecImage = TextureMakeCodecImage(std::move(image));
    if (!codecImage)
    {
        return nullptr;
    }

    auto texture = own::make_shared<Texture>();
    texture->m_name = std::string(name);
    texture->m_textureType = isCube
        ? TextureType::TextureCube
        : (arraySize > 1 ? TextureType::TextureArray : TextureType::ImageTexture);
    texture->m_size = { width, height };
    // ★ 예전에는 DirectX::HasAlpha(포맷)를 물었다. 그 물음은 "이 포맷에
    //   알파 채널이 있는가"이지 "이 그림에 투명한 데가 있는가"가 아니다 —
    //   중립 어휘의 8비트 색 포맷은 전부 알파를 가지므로 답이 늘 참이었다.
    //   지금은 채널 수로 같은 답을 준다(뜻이 바뀌지 않는다).
    texture->m_isTextureAlpha = (4 == RHIFormatChannels(format))
        || RHIFormatIsBlockCompressed(format);
    texture->SetNonRehydratableImage(std::move(codecImage));
    return std::move(texture);
}

own::shared_owner<const Texture> Texture::CreateFromSourceImage(
    own::shared_owner<const CodecImage> image, std::string_view name,
    std::string_view extension, std::string_view assetPath)
{
    if (!image || image->subresources.empty())
    {
        return {};
    }
    auto texture = own::make_shared<Texture>();
    texture->m_name = std::string(name);
    texture->m_extension = std::string(extension);
    texture->m_assetPath = std::string(assetPath);
    texture->m_textureType = image->isCube ? TextureType::TextureCube
        : (image->arraySize > 1u ? TextureType::TextureArray : TextureType::ImageTexture);
    texture->m_size = { float(image->width), float(image->height) };
    texture->m_isTextureAlpha = image->hasAlpha;
    texture->SetNonRehydratableImage(std::move(image));
    return std::move(texture);
}

own::shared_owner<const Texture> Texture::LoadFormPath(_In_ const file::path& path, bool isCompress)
{
    const file::path materialPath = PathFinder::RelativeToMaterial(path.string());
    const file::path resolved = file::is_regular_file(materialPath) ? materialPath : path;
    if (resolved.extension() == ".cetex")
    {
        return LoadSharedFromPath(resolved, false);
    }
    if (!file::is_regular_file(resolved))
    {
        return {};
    }
    return CreateFromSourceImage(AssetAuthoringPort::LoadSourceTexture(resolved, {},
        isCompress ? TextureSourceCompression::LegacyColor : TextureSourceCompression::None), {}, {}, {});
}

own::shared_owner<const Texture> Texture::LoadSharedFromPath(const file::path& path, bool isCompress, std::string_view assetPath)
{
    file::path matPath = PathFinder::RelativeToMaterial(path.string());
    // 폴더는 파일이 아니다. 빈 이름이 `Textures\` 폴더가 되어 "있음" 을 통과하고 디코더가
    // 폴더를 열다 예외로 에디터를 죽였다(DecalComponent 를 붙이는 순간).
    if (!file::is_regular_file(path) && !file::is_regular_file(matPath))
    {
        return nullptr;
    }

    const file::path preparePath = file::is_regular_file(path) ? path : matPath;
    if (preparePath.extension() == ".cetex")
    {
        std::ifstream stream(preparePath, std::ios::binary | std::ios::ate);
        const auto size = stream.tellg();
        if (!stream || size <= 0 || static_cast<std::uint64_t>(size) > experiment::cooked::kCookedTextureMaxBytes)
        {
            return {};
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream || stream.peek() != std::char_traits<char>::eof())
        {
            return {};
        }
        return LoadCookedFromBytes(bytes, path.stem().string(), assetPath);
    }

    const auto image = AssetAuthoringPort::LoadSourceTexture(preparePath, {},
        isCompress ? TextureSourceCompression::MaterialColor : TextureSourceCompression::None);
    return CreateFromSourceImage(image, path.stem().string(), path.extension().string(),
        assetPath.empty() ? path.lexically_normal().generic_string() : std::string(assetPath));
}


own::shared_owner<const Texture> Texture::LoadSharedFromMemory(
    std::span<const std::byte> bytes, bool isCompress)
{
    if (bytes.size() >= 4u && bytes[0] == std::byte{ 'C' } && bytes[1] == std::byte{ 'E' }
        && bytes[2] == std::byte{ 'C' } && bytes[3] == std::byte{ 'T' })
    {
        return LoadCookedFromBytes(bytes, {}, {});
    }
    return CreateFromSourceImage(AssetAuthoringPort::LoadSourceTexture({}, bytes,
        isCompress ? TextureSourceCompression::MaterialColor : TextureSourceCompression::None), {}, {}, {});
}

own::shared_owner<const Texture> Texture::LoadCookedFromBytes(
    std::span<const std::byte> bytes, std::string_view name, std::string_view assetPath)
{
    TextureImage image;
    experiment::cooked::CookedTextureInfo info;
    std::string failure;
    if (!experiment::cooked::DecodeCookedTexture(bytes, image, failure, &info))
    {
        return {};
    }
    return CreateSharedFromCookedImage(name, std::move(image), info, assetPath);
}

own::shared_owner<const Texture> Texture::CreateSharedFromCookedImage(
    std::string_view name, TextureImage image,
    const experiment::cooked::CookedTextureInfo& info, std::string_view assetPath)
{
    if (!image.IsValid())
    {
        return {};
    }
    auto codec = own::make_shared<CodecImage>();
    codec->hasAlpha = info.hasAlpha;
    codec->colorSpaceLocked = info.colorSpaceLocked;
    if (!TextureAdoptImage(*codec, std::move(image)))
    {
        return {};
    }
    codec->Account();
    auto texture = own::make_shared<Texture>();
    texture->m_name = std::string(name);
    texture->m_assetPath = std::string(assetPath);
    texture->m_extension = ".cetex";
    texture->m_textureType = codec->isCube ? TextureType::TextureCube
        : (codec->arraySize > 1u ? TextureType::TextureArray : TextureType::ImageTexture);
    texture->m_size = { float(codec->width), float(codec->height) };
    texture->m_isTextureAlpha = codec->hasAlpha;
    texture->m_cookedPayload = true;
    texture->m_cookedColorSpaceLocked = info.colorSpaceLocked;
    texture->SetNonRehydratableImage(std::move(codec));
    return std::move(texture);
}

own::shared_owner<const Texture::CodecImage> Texture::DecodeOwnedImage(
    std::span<const std::byte> bytes, const AssetDepot::TextureImageKey& key, std::string& failure)
{
    failure.clear();
    if (key.representation != experiment::cooked::kCookedTextureRepresentationVersion
        || key.schemaVersion != experiment::cooked::kCookedTextureSchemaVersion)
    {
        failure = "Texture requires a GPU-ready CECT artifact; recook this asset.";
        return {};
    }
    TextureImage image;
    experiment::cooked::CookedTextureInfo info;
    if (!experiment::cooked::DecodeCookedTexture(bytes, image, failure, &info))
    {
        return {};
    }
    auto codec = own::make_shared<CodecImage>();
    codec->key = key;
    codec->hasAlpha = info.hasAlpha;
    codec->colorSpaceLocked = info.colorSpaceLocked;
    if (!TextureAdoptImage(*codec, std::move(image)))
    {
        failure = "Validated cooked texture could not be adopted.";
        return {};
    }
    codec->Account();
    return std::move(codec);
}

own::shared_owner<const Texture> Texture::CreateOwnedDescriptor(
    const own::shared_owner<const CodecImage>& codec,
    own::shared_owner<const AssetDepot::TextureAssetOrigin> origin, std::string& failure)
{
    if (!codec || !origin || !origin->imageSource || !codec->key || *codec->key != origin->imageKey)
    {
        failure = "Texture descriptor requires its verified exact image source and payload.";
        return {};
    }
    const auto& variant = origin->variant;
    RHIFormat samplingFormat = codec->format;
    if (variant.colorSpace != AssetDepot::TextureAssetColorSpace::Source)
    {
        const bool srgb = variant.colorSpace == AssetDepot::TextureAssetColorSpace::Srgb;
        switch (samplingFormat)
        {
        case RHIFormat::RGBA8Unorm: case RHIFormat::RGBA8UnormSrgb:
            samplingFormat = srgb ? RHIFormat::RGBA8UnormSrgb : RHIFormat::RGBA8Unorm;
            break;
        case RHIFormat::BGRA8Unorm: case RHIFormat::BGRA8UnormSrgb:
            samplingFormat = srgb ? RHIFormat::BGRA8UnormSrgb : RHIFormat::BGRA8Unorm;
            break;
        case RHIFormat::BC1Unorm: case RHIFormat::BC1UnormSrgb:
            samplingFormat = srgb ? RHIFormat::BC1UnormSrgb : RHIFormat::BC1Unorm;
            break;
        case RHIFormat::BC3Unorm: case RHIFormat::BC3UnormSrgb:
            samplingFormat = srgb ? RHIFormat::BC3UnormSrgb : RHIFormat::BC3Unorm;
            break;
        case RHIFormat::BC7Unorm: case RHIFormat::BC7UnormSrgb:
            samplingFormat = srgb ? RHIFormat::BC7UnormSrgb : RHIFormat::BC7Unorm;
            break;
        case RHIFormat::BC5Unorm:
            if (srgb)
            {
                failure = "BC5 normal/data texture cannot use sRGB sampling.";
                return {};
            }
            break;
        case RHIFormat::RGBA16Float: case RHIFormat::RGBA32Float:
            if (srgb)
            {
                failure = "Floating-point texture cannot use an sRGB sampling representation.";
                return {};
            }
            break;
        default:
            failure = "Texture sampling representation is unsupported.";
            return {};
        }
    }
    if (codec->colorSpaceLocked && samplingFormat != codec->format)
    {
        failure = "Texture role conflicts with its cooked color-space policy; cook a separate texture recipe.";
        return {};
    }
    if (variant.forceRgba8 && codec->format != RHIFormat::RGBA8Unorm
        && codec->format != RHIFormat::RGBA8UnormSrgb)
    {
        failure = "RGBA8 must be selected at cook time; runtime texture conversion is disabled.";
        return {};
    }
    const bool ignoresMipHint = variant.mipPolicy == AssetDepot::TextureMipPolicy::GenerateFull
        && codec->mipLevels == 1u && (codec->width > 1u || codec->height > 1u);
    const bool ignoresCompressionHint = variant.compress && !RHIFormatIsBlockCompressed(codec->format);
    if (ignoresMipHint || ignoresCompressionHint)
    {
        // One diagnostic at descriptor publication, never in the rehydration or
        // upload hot path. Authored bytes and intentional mip policy stay exact.
        Debug::PrintLog(spdlog::level::warn,
            "Cooked texture " + Uuid::ToString(origin->resolved.entry.assetId.value)
            + " preserves its offline mip/compression policy; legacy runtime hints were ignored.");
    }
    auto texture = own::make_shared<Texture>();
    texture->m_cookedPayload = true;
    texture->m_cookedColorSpaceLocked = codec->colorSpaceLocked;
    texture->m_textureType = codec->isCube ? TextureType::TextureCube
        : (codec->arraySize > 1u ? TextureType::TextureArray : TextureType::ImageTexture);
    texture->m_size = { float(codec->width), float(codec->height) };
    texture->m_samplingFormat = samplingFormat;
    texture->m_isTextureAlpha = codec->hasAlpha;
    texture->m_imageDescription = { samplingFormat, codec->width, codec->height,
        codec->mipLevels, codec->arraySize, codec->isCube };
    texture->m_decodedBytes = codec->accountedBytes;
    texture->m_assetOrigin = std::move(origin);
    return std::move(texture);
}

own::shared_owner<const AssetDepot::TextureAssetOrigin> Texture::GetAssetOrigin() const
{
    return m_assetOrigin;
}

std::size_t Texture::DecodedByteSize() const noexcept
{
    return m_decodedBytes;
}

std::size_t Texture::ImageByteSize(const own::shared_owner<const CodecImage>& image) noexcept
{
    return image ? image->accountedBytes : 0u;
}

std::size_t Texture::ImageRetainedCharge(const own::shared_owner<const CodecImage>& image) noexcept
{
    if (!image)
    {
        return 0u;
    }
    std::size_t bytes = sizeof(CodecImage);
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    const auto add = [&](std::size_t charge)
    {
        bytes = charge > maximum - bytes ? maximum : bytes + charge;
    };
    add(image->sourceRetainedBytes);
    add(image->owned.RetainedBytes());
    add(image->subresources.capacity() > maximum / sizeof(TextureSubimage)
        ? maximum : image->subresources.capacity() * sizeof(TextureSubimage));
    if (image->key)
    {
        add(image->key->targetPlatform.capacity());
        add(1u);
        add(image->key->targetAbi.capacity());
        add(1u);
    }
    return bytes;
}

std::size_t Texture::DescriptorByteSize() const noexcept
{
    std::size_t bytes = sizeof(Texture);
    const auto add = [&](std::size_t charge)
    {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        bytes = charge > maximum - bytes ? maximum : bytes + charge;
    };
    const auto addString = [&](const std::string& value)
    {
        add(value.capacity());
        add(1u);
    };
    const auto addVector = [&]<class T>(const std::vector<T>& values)
    {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        add(values.capacity() > maximum / sizeof(T) ? maximum : values.capacity() * sizeof(T));
    };
    addString(m_name);
    addString(m_extension);
    addString(m_assetPath);
    if (m_assetOrigin)
    {
        add(sizeof(AssetDepot::TextureAssetOrigin));
        addString(m_assetOrigin->resolved.blob.artifactPath);
        addString(m_assetOrigin->resolved.blob.targetPlatform);
        addString(m_assetOrigin->resolved.blob.targetAbi);
        addVector(m_assetOrigin->resolved.entry.dependencies);
        addString(m_assetOrigin->imageKey.targetPlatform);
        addString(m_assetOrigin->imageKey.targetAbi);
        addVector(m_assetOrigin->hardDependencies);
        addVector(m_assetOrigin->loadableDependencies);
        if (m_assetOrigin->imageSource)
        {
            // Shared exact-locator metadata is conservatively charged per retained
            // descriptor; no decoded pixels or source filesystem bytes are hidden.
            add(sizeof(AssetDepot::TextureImageSource));
            addString(m_assetOrigin->imageSource->artifactPath);
        }
    }
    return bytes;
}

Texture::ImageMemorySnapshot Texture::SnapshotImageMemory() noexcept
{
    return { TextureLiveImageBytes.load(std::memory_order_relaxed),
        TextureNonRehydratableBytes.load(std::memory_order_relaxed),
        TextureLiveImageCount.load(std::memory_order_relaxed) };
}

own::shared_owner<const Texture> Texture::LoadManagedFromPath(const file::path& path, bool isCompress)
{
    const file::path materialPath = PathFinder::RelativeToMaterial(path.string());
    const file::path resolved = file::is_regular_file(materialPath) ? materialPath : path;
    if (resolved.extension() == ".cetex")
    {
        return LoadSharedFromPath(resolved, false);
    }
    if (!file::is_regular_file(resolved))
    {
        return {};
    }
    return CreateFromSourceImage(AssetAuthoringPort::LoadSourceTexture(resolved, {},
        isCompress ? TextureSourceCompression::LegacyLinear : TextureSourceCompression::None), {}, {}, {});
}

// Source decoding exists only while an authoring host installs its adapter.
bool Texture::DecodeToRgba8(std::span<const std::byte> bytes,
    TextureImage& outImage, std::string& outFailure)
{
    return AssetAuthoringPort::DecodeSourceTextureRgba8(bytes, outImage, outFailure);
}

// ★ DX11 생성자·뷰 이관을 걷어낸 뒤의 수명 (T6, 2026-08-08).
//
//   예전 이동 생성자는 ID3D11 핸들 다섯과 desc를 옮기고, 화면 리사이즈
//   델리게이트 둘을 해제했다가 새 객체로 다시 걸었다. 그 전부가 DX11
//   자원과 화면 추종 정책의 것이라 함께 사라졌다.
//
//   남은 것은 CPU 자료뿐이고, 그것은 shared_ptr과 값이라 옮기기만 하면 된다.
Texture::Texture(Texture&& texture) noexcept
{
    m_nonRehydratableImage = std::move(texture.m_nonRehydratableImage);
    m_imageDescription = texture.m_imageDescription;
    m_decodedBytes = texture.m_decodedBytes;
    m_assetOrigin = std::move(texture.m_assetOrigin);
    m_samplingFormat = texture.m_samplingFormat;
    m_cookedPayload = texture.m_cookedPayload;
    m_cookedColorSpaceLocked = texture.m_cookedColorSpaceLocked;
    m_assetId = texture.m_assetId;
    m_textureType = texture.m_textureType;
    m_name = std::move(texture.m_name);
    m_extension = std::move(texture.m_extension);
    m_assetPath = std::move(texture.m_assetPath);
    m_size = texture.m_size;
    m_isTextureAlpha = texture.m_isTextureAlpha;

    texture.m_textureType = TextureType::Unknown;
    texture.m_size = {};
    texture.m_isTextureAlpha = false;
}


math::vector2 Texture::GetImageSize() const
{
    // ★ 예전에는 m_sizeRatio로 나눴다 (T6에서 정리).
    //   그 비율은 '화면의 1/N 해상도로 따라가는 렌더 타깃'을 위한 것이었고,
    //   화면 추종 정책과 함께 사라졌다. 파일에서 읽은 텍스처의 비율은 늘
    //   1이었으므로 이 함수가 돌려주던 값은 그대로다.
    return m_size;
}

