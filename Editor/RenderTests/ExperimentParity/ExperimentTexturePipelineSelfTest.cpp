#include "ExperimentParity/ExperimentTexturePipelineSelfTest.h"

#include "Experiment/Cooked/CookedTexture.h"

#include <d3d11.h>
#include <DirectXTex.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        struct TexturePipelineChecker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const char* message)
            {
                if (condition)
                {
                    ++passed;
                }
                else
                {
                    ++failed;
                    log += "    [failed] ";
                    log += message;
                    log += '\n';
                }
            }
        };

        void TexturePipelinePut32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value)
        {
            for (std::size_t index = 0u; index < 4u; ++index)
            {
                bytes[offset + index] = static_cast<std::byte>((value >> (index * 8u)) & 0xffu);
            }
        }

        // Small DX10 DDS authoring fixture: explicit little-endian bytes rather
        // than saving the same runtime representation that is under test.
        std::vector<std::byte> TexturePipelineDds(std::uint32_t width, std::uint32_t height,
            std::uint32_t levels, std::uint32_t arraySize = 1u, bool cube = false, bool hdr = false)
        {
            std::vector<std::byte> bytes(148u);
            TexturePipelinePut32(bytes, 0u, 0x20534444u);
            TexturePipelinePut32(bytes, 4u, 124u);
            TexturePipelinePut32(bytes, 8u, 0x100fu | (levels > 1u ? 0x20000u : 0u));
            TexturePipelinePut32(bytes, 12u, height);
            TexturePipelinePut32(bytes, 16u, width);
            TexturePipelinePut32(bytes, 20u, width * (hdr ? 16u : 4u));
            TexturePipelinePut32(bytes, 28u, levels);
            TexturePipelinePut32(bytes, 76u, 32u);
            TexturePipelinePut32(bytes, 80u, 4u);
            TexturePipelinePut32(bytes, 84u, 0x30315844u);
            TexturePipelinePut32(bytes, 108u, 0x1000u | (levels > 1u ? 0x400008u : 0u) | (cube ? 8u : 0u));
            TexturePipelinePut32(bytes, 112u, cube ? 0xfe00u : 0u);
            TexturePipelinePut32(bytes, 128u, hdr ? 2u : 28u);
            TexturePipelinePut32(bytes, 132u, 3u);
            TexturePipelinePut32(bytes, 136u, cube ? 4u : 0u);
            TexturePipelinePut32(bytes, 140u, cube ? arraySize / 6u : arraySize);
            for (std::uint32_t item = 0u; item < arraySize; ++item)
            {
                for (std::uint32_t mip = 0u; mip < levels; ++mip)
                {
                    const auto count = (std::max)(1u, width >> mip) * (std::max)(1u, height >> mip);
                    for (std::uint32_t pixel = 0u; pixel < count; ++pixel)
                    {
                        if (hdr)
                        {
                            const auto offset = bytes.size();
                            bytes.resize(offset + 16u);
                            TexturePipelinePut32(bytes, offset, std::bit_cast<std::uint32_t>(4.0f));
                            TexturePipelinePut32(bytes, offset + 4u, std::bit_cast<std::uint32_t>(-0.25f));
                            TexturePipelinePut32(bytes, offset + 8u, std::bit_cast<std::uint32_t>(1.5f));
                            TexturePipelinePut32(bytes, offset + 12u, std::bit_cast<std::uint32_t>(1.0f));
                        }
                        else
                        {
                            bytes.push_back(static_cast<std::byte>((pixel * 29u + item * 17u + mip * 41u) & 255u));
                            bytes.push_back(static_cast<std::byte>((pixel * 11u + 67u) & 255u));
                            bytes.push_back(static_cast<std::byte>((pixel * 7u + 149u) & 255u));
                            bytes.push_back(std::byte{ 255 });
                        }
                    }
                }
            }
            return bytes;
        }

        bool TexturePipelineCook(std::span<const std::byte> source,
            const experiment::cooked::TextureImportSettings& settings, TextureImage& image,
            std::vector<std::byte>& artifact, std::string& failure, experiment::cooked::CookedTextureInfo* info = nullptr)
        {
            return experiment::cooked::CookTexture(source, settings, artifact, failure)
                && experiment::cooked::DecodeCookedTexture(artifact, image, failure, info);
        }

        float TexturePipelineFloat(const std::byte* bytes)
        {
            float value = 0.0f;
            std::memcpy(&value, bytes, sizeof(value));
            return value;
        }
    }

    bool RunExperimentTexturePipelineSelfTest(std::string& outLog)
    {
        namespace ck = experiment::cooked;
        outLog += "  PHASE 12 texture pipeline byte/quality contracts\n";
        TexturePipelineChecker check{ outLog };
        std::string failure;
        struct TexturePipelineApartment final
        {
            HRESULT result{ CoInitializeEx(nullptr, COINIT_MULTITHREADED) };
            ~TexturePipelineApartment()
            {
                if (SUCCEEDED(result))
                {
                    CoUninitialize();
                }
            }
        } apartment;
        check.Check(SUCCEEDED(apartment.result) || apartment.result == RPC_E_CHANGED_MODE,
            "source-codec test COM initialization succeeds");
        ck::TextureImportSettings settings;
        settings.colorSpace = ck::TextureColorSpace::Srgb;
        settings.mipPolicy = ck::TextureMipPolicy::GenerateFull;
        auto checker = TexturePipelineDds(2u, 2u, 1u);
        const std::array<std::uint8_t, 16u> checkerPixels{
            0u, 0u, 0u, 0u, 255u, 255u, 255u, 64u,
            0u, 0u, 0u, 128u, 255u, 255u, 255u, 255u };
        std::memcpy(checker.data() + 148u, checkerPixels.data(), checkerPixels.size());
        TextureImage srgb;
        std::vector<std::byte> srgbArtifact;
        ck::CookedTextureInfo info;
        const bool srgbCooked = TexturePipelineCook(checker, settings, srgb, srgbArtifact, failure, &info);
        check.Check(srgbCooked, "sRGB fixture cooks and decodes");
        if (srgbCooked)
        {
            check.Check(srgb.MipLevels() == 2u && srgb.Format() == RHIFormat::RGBA8UnormSrgb,
                "sRGB GenerateFull emits a full chain with the sRGB format");
            const auto* base = srgb.Find(0u, 0u);
            const auto* tail = srgb.Find(1u, 0u);
            check.Check(base && std::memcmp(base->pixels, checkerPixels.data(), checkerPixels.size()) == 0,
                "mip generation preserves base bytes exactly");
            check.Check(tail && std::abs(static_cast<int>(tail->pixels[0]) - 188) <= 2,
                "sRGB RGB averages in linear light");
            check.Check(tail && std::abs(static_cast<int>(tail->pixels[3]) - 112) <= 1,
                "alpha averages independently without the RGB transfer function");
            check.Check(info.hasAlpha && info.colorSpaceLocked, "cooked alpha and filtered color-space policy survive the header");
        }
        settings.colorSpace = ck::TextureColorSpace::Linear;
        TextureImage linear;
        std::vector<std::byte> linearArtifact;
        const bool linearCooked = TexturePipelineCook(checker, settings, linear, linearArtifact, failure);
        check.Check(linearCooked, "linear fixture cooks");
        if (linearCooked)
        {
            const auto* tail = linear.Find(1u, 0u);
            check.Check(tail && std::abs(static_cast<int>(tail->pixels[0]) - 128) <= 1,
                "linear role has its own correct filtered chain");
            check.Check(linearArtifact != srgbArtifact, "role-specific filtering produces distinct immutable bytes");
        }
        auto graySource = TexturePipelineDds(2u, 2u, 1u);
        TexturePipelinePut32(graySource, 20u, 2u);
        TexturePipelinePut32(graySource, 128u, 61u); // DXGI_FORMAT_R8_UNORM has no sRGB twin.
        graySource.resize(152u);
        std::fill(graySource.begin() + 148u, graySource.end(), std::byte{ 128 });
        settings.colorSpace = ck::TextureColorSpace::Srgb;
        settings.mipPolicy = ck::TextureMipPolicy::PreserveAuthored;
        TextureImage gray;
        std::vector<std::byte> grayArtifact;
        const bool grayCooked = TexturePipelineCook(graySource, settings, gray, grayArtifact, failure);
        check.Check(grayCooked && gray.Format() == RHIFormat::RGBA8UnormSrgb
            && gray.Find(0u, 0u)->pixels[0] == std::byte{ 128 },
            "explicit sRGB grayscale lowering preserves stored bytes without gamma-encoding them");
        DirectX::ScratchImage jpegPixels;
        DirectX::Blob jpegBytes;
        const auto jpegFixture = TexturePipelineDds(2u, 2u, 1u);
        const bool jpegEncoded = SUCCEEDED(DirectX::LoadFromDDSMemory(jpegFixture.data(), jpegFixture.size(),
            DirectX::DDS_FLAGS_NONE, nullptr, jpegPixels))
            && SUCCEEDED(DirectX::SaveToWICMemory(*jpegPixels.GetImage(0u, 0u, 0u), DirectX::WIC_FLAGS_NONE,
                DirectX::GetWICCodec(DirectX::WIC_CODEC_JPEG), jpegBytes));
        check.Check(jpegEncoded, "JPEG fixture is encoded with the existing DirectXTex/WIC dependency");
        if (jpegEncoded)
        {
            TextureImage jpeg;
            std::vector<std::byte> jpegArtifact;
            check.Check(TexturePipelineCook(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(jpegBytes.GetBufferPointer()), jpegBytes.GetBufferSize()),
                settings, jpeg, jpegArtifact, failure) && jpeg.Width() == 2u && jpeg.Height() == 2u,
                "JPEG source bytes cook into a validated GPU-ready artifact");
        }
        settings.mipPolicy = ck::TextureMipPolicy::PreserveAuthored;
        const auto authored = TexturePipelineDds(8u, 4u, 2u, 2u);
        TextureImage partial;
        std::vector<std::byte> partialArtifact;
        settings.colorSpace = ck::TextureColorSpace::Source;
        const bool partialCooked = TexturePipelineCook(authored, settings, partial, partialArtifact, failure);
        check.Check(partialCooked, "authored partial array chain cooks");
        if (partialCooked)
        {
            check.Check(partial.MipLevels() == 2u && partial.ArraySize() == 2u,
                "PreserveAuthored retains an authored partial chain");
            std::size_t offset = 148u;
            for (std::uint32_t item = 0u; item < partial.ArraySize(); ++item)
            {
                for (std::uint32_t mip = 0u; mip < partial.MipLevels(); ++mip)
                {
                    const auto* level = partial.Find(mip, item);
                    check.Check(level && std::memcmp(level->pixels, authored.data() + offset, level->slicePitch) == 0,
                        "authored subresources retain exact source bytes and item/mip ordering");
                    offset += level->slicePitch;
                }
            }
        }
        settings.mipPolicy = ck::TextureMipPolicy::GenerateFull;
        TextureImage completed;
        std::vector<std::byte> completedArtifact;
        const bool completedCooked = TexturePipelineCook(authored, settings, completed, completedArtifact, failure);
        check.Check(completedCooked && completed.MipLevels() == 4u,
            "explicit GenerateFull appends the missing tail of an authored partial chain");
        if (completedCooked && partialCooked)
        {
            for (std::uint32_t item = 0u; item < partial.ArraySize(); ++item)
            {
                for (std::uint32_t mip = 0u; mip < partial.MipLevels(); ++mip)
                {
                    const auto* before = partial.Find(mip, item);
                    const auto* after = completed.Find(mip, item);
                    check.Check(after && std::memcmp(before->pixels, after->pixels, before->slicePitch) == 0,
                        "completing the chain preserves every authored mip byte");
                }
            }
        }
        settings.mipPolicy = ck::TextureMipPolicy::PreserveAuthored;
        settings.maxDimension = 4u;
        TextureImage resized;
        std::vector<std::byte> resizedArtifact;
        const bool resizedCooked = TexturePipelineCook(authored, settings, resized, resizedArtifact, failure);
        check.Check(resizedCooked && resized.Width() == 4u && resized.Height() == 2u && resized.MipLevels() == 2u,
            "maxDimension respects aspect ratio and retains the authored partial mip count");
        settings = {};
        settings.mipPolicy = ck::TextureMipPolicy::GenerateFull;
        const auto hdrSource = TexturePipelineDds(2u, 2u, 1u, 1u, false, true);
        TextureImage hdr;
        std::vector<std::byte> hdrArtifact;
        const bool hdrCooked = TexturePipelineCook(hdrSource, settings, hdr, hdrArtifact, failure);
        check.Check(hdrCooked && hdr.Format() == RHIFormat::RGBA32Float && hdr.MipLevels() == 2u,
            "HDR remains floating-point through mip generation");
        if (hdrCooked)
        {
            const auto* tail = hdr.Find(1u, 0u);
            check.Check(tail && std::abs(TexturePipelineFloat(tail->pixels) - 4.0f) < 0.001f
                && std::abs(TexturePipelineFloat(tail->pixels + 4u) + 0.25f) < 0.001f,
                "HDR values outside [0,1] survive filtering without clipping");
        }
        settings.compression = ck::TextureCompression::BC7;
        std::vector<std::byte> rejected;
        check.Check(!ck::CookTexture(hdrSource, settings, rejected, failure) && rejected.empty(),
            "unsupported HDR-to-LDR compression fails explicitly");
        settings = {};
        settings.colorSpace = ck::TextureColorSpace::Linear;
        settings.normalMap = true;
        settings.mipPolicy = ck::TextureMipPolicy::GenerateFull;
        auto normalSource = TexturePipelineDds(2u, 2u, 1u);
        const std::array<std::uint8_t, 16u> normals{
            255u, 128u, 128u, 255u, 128u, 255u, 128u, 255u,
            255u, 128u, 128u, 255u, 128u, 255u, 128u, 255u };
        std::memcpy(normalSource.data() + 148u, normals.data(), normals.size());
        TextureImage normal;
        std::vector<std::byte> normalArtifact;
        const bool normalCooked = TexturePipelineCook(normalSource, settings, normal, normalArtifact, failure);
        check.Check(normalCooked, "normal mip fixture cooks");
        if (normalCooked)
        {
            const auto* tail = normal.Find(1u, 0u);
            float lengthSquared = 0.0f;
            if (tail)
            {
                for (std::size_t channel = 0u; channel < 3u; ++channel)
                {
                    const float component = static_cast<float>(tail->pixels[channel]) / 127.5f - 1.0f;
                    lengthSquared += component * component;
                }
            }
            check.Check(tail && std::abs(lengthSquared - 1.0f) < 0.025f,
                "generated normal mip vectors are renormalized");
        }
        settings.compression = ck::TextureCompression::BC5;
        TextureImage bc5;
        std::vector<std::byte> bc5Artifact;
        auto normalBc5Source = TexturePipelineDds(4u, 4u, 1u);
        for (std::size_t pixel = 0u; pixel < 16u; ++pixel)
        {
            std::memcpy(normalBc5Source.data() + 148u + pixel * 4u, normals.data() + (pixel % 4u) * 4u, 4u);
        }
        const bool bc5Cooked = TexturePipelineCook(normalBc5Source, settings, bc5, bc5Artifact, failure);
        check.Check(bc5Cooked && bc5.Format() == RHIFormat::BC5Unorm, "normal compression emits GPU-ready BC5");
        if (bc5Cooked)
        {
            check.Check(bc5.Find(0u, 0u)->slicePitch == 16u && bc5.Find(1u, 0u)->slicePitch == 16u,
                "sub-four-texel BC5 mip levels retain a complete block");
        }
        settings = {};
        settings.compression = ck::TextureCompression::BC7;
        settings.compressionQuality = ck::TextureCompressionQuality::Fast;
        settings.mipPolicy = ck::TextureMipPolicy::GenerateFull;
        const auto npot = TexturePipelineDds(12u, 20u, 1u);
        TextureImage bc7;
        std::vector<std::byte> bc7Artifact;
        const bool bc7Cooked = TexturePipelineCook(npot, settings, bc7, bc7Artifact, failure);
        check.Check(bc7Cooked && bc7.Format() == RHIFormat::BC7Unorm && bc7.MipLevels() == 5u,
            "NPOT BC7 chain reaches 1x1 with valid block layout");
        std::vector<std::byte> repeat;
        check.Check(ck::CookTexture(npot, settings, repeat, failure) && repeat == bc7Artifact,
            "repeated serial CPU cooking is byte deterministic");
        const auto unaligned = TexturePipelineDds(5u, 7u, 1u);
        std::vector<std::byte> unalignedArtifact;
        check.Check(!ck::CookTexture(unaligned, settings, unalignedArtifact, failure) && unalignedArtifact.empty(),
            "portable desktop BC output rejects unaligned base dimensions instead of silently padding UVs");
        const auto fastRecipe = ck::TextureImportRecipe(settings);
        settings.compressionQuality = ck::TextureCompressionQuality::High;
        check.Check(ck::TextureImportRecipe(settings) != fastRecipe,
            "compression quality is part of the recipe fingerprint");
        settings = {};
        const auto cubeSource = TexturePipelineDds(2u, 2u, 1u, 6u, true);
        TextureImage cube;
        std::vector<std::byte> cubeArtifact;
        check.Check(TexturePipelineCook(cubeSource, settings, cube, cubeArtifact, failure)
            && cube.IsCube() && cube.ArraySize() == 6u, "cube faces preserve their validated shape and array ordering");
        settings.mipPolicy = ck::TextureMipPolicy::GenerateFull;
        settings.preserveAlphaCoverage = true;
        auto cutout = TexturePipelineDds(4u, 4u, 1u);
        for (std::size_t pixel = 0u; pixel < 16u; ++pixel)
        {
            cutout[148u + pixel * 4u + 3u] = pixel < 8u ? std::byte{ 255 } : std::byte{ 0 };
        }
        TextureImage covered;
        std::vector<std::byte> coverageArtifact;
        const bool coveredCooked = TexturePipelineCook(cutout, settings, covered, coverageArtifact, failure);
        check.Check(coveredCooked && covered.MipLevels() == 3u, "alpha coverage policy processes a cutout chain");
        if (coveredCooked)
        {
            check.Check(std::memcmp(covered.Find(0u, 0u)->pixels, cutout.data() + 148u, 64u) == 0,
                "coverage preservation never alters base alpha");
            const auto* level = covered.Find(1u, 0u);
            std::size_t count = 0u;
            for (std::size_t y = 0u; y < level->height; ++y)
            {
                for (std::size_t x = 0u; x < level->width; ++x)
                {
                    if (level->pixels[y * level->rowPitch + x * 4u + 3u] >= std::byte{ 128 })
                    {
                        ++count;
                    }
                }
            }
            check.Check(count == 2u, "representable half-coverage remains half-coverage at the first mip");
        }
        outLog += "  passed " + std::to_string(check.passed) + ", failed " + std::to_string(check.failed) + '\n';
        return check.failed == 0u;
    }
}
