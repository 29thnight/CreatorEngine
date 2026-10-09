#include "ExperimentParity/ExperimentTextureCookSelfTest.h"
#include "ExperimentParity/ExperimentTexturePipelineSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/TextureCookProducer.h"
#include "Experiment/Cooked/CookedTexture.h"
#include "Experiment/Cooked/TextureImportSettings.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <sstream>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ck = experiment::cooked;

        // 단정 하나. 실패해도 계속 진행한다 — 첫 실패에서 멈추면 "몇 개가
        // 깨졌는가"를 못 보고, 그러면 변이가 정확히 몇 건을 빨갛게 만드는지도
        // 셀 수 없다(이 저장소의 이빨 증명 규칙).
        struct Checker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& what)
            {
                if (condition)
                {
                    ++passed;
                    return;
                }
                ++failed;
                log += "    [실패] " + what + "\n";
            }
        };

        [[nodiscard]] std::filesystem::path MakeScratchRoot()
        {
            const auto ticks = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            std::ostringstream name;
            name << "cemc-texcook-" << std::hex
                << static_cast<unsigned int>(_getpid())
                << '-' << static_cast<std::uint64_t>(ticks);
            return std::filesystem::temp_directory_path() / name.str();
        }

        [[nodiscard]] bool WriteFile(const std::filesystem::path& path,
            const std::string& bytes)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                return false;
            }

            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                return false;
            }
            if (!bytes.empty())
            {
                stream.write(bytes.data(),
                    static_cast<std::streamsize>(bytes.size()));
            }
            stream.flush();
            return static_cast<bool>(stream);
        }

        [[nodiscard]] bool ReadFileBytes(const std::filesystem::path& path,
            std::string& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                return false;
            }
            stream.seekg(0, std::ios::end);
            const std::streamoff bytes = stream.tellg();
            if (bytes < 0)
            {
                return false;
            }
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(bytes));
            if (!out.empty())
            {
                stream.read(out.data(), static_cast<std::streamsize>(out.size()));
            }
            return stream.good() || stream.eof();
        }

        // 최소한의 sidecar. 실제 importer 가 쓰는 형태와 같은 최상위 guid 한 줄.
        [[nodiscard]] std::string MetaYaml(const std::string& guid)
        {
            return "guid: " + guid + "\nimportSettings:\n  extension: .png\n";
        }

        [[nodiscard]] std::string BytesToString(
            const std::vector<std::byte>& bytes)
        {
            std::string text;
            text.resize(bytes.size());
            for (std::size_t index = 0u; index < bytes.size(); ++index)
            {
                text[index] = static_cast<char>(bytes[index]);
            }
            return text;
        }

        [[nodiscard]] std::string MinimalImage(std::string_view extension)
        {
            if (extension == ".png")
            {
                static constexpr unsigned char bytes[]{
                0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au, 0x00u, 0x00u, 0x00u, 0x0du,
                0x49u, 0x48u, 0x44u, 0x52u, 0x00u, 0x00u, 0x00u, 0x04u, 0x00u, 0x00u, 0x00u, 0x04u,
                0x08u, 0x06u, 0x00u, 0x00u, 0x00u, 0xa9u, 0xf1u, 0x9eu, 0x7eu, 0x00u, 0x00u, 0x00u,
                0x1au, 0x49u, 0x44u, 0x41u, 0x54u, 0x78u, 0xdau, 0x63u, 0x60u, 0x58u, 0xf0u, 0xffu,
                0xffu, 0xffu, 0x13u, 0x0cu, 0xffu, 0xe1u, 0x34u, 0x0au, 0x07u, 0x48u, 0x33u, 0x10u,
                0x54u, 0x01u, 0x00u, 0xb0u, 0x7fu, 0x2bu, 0x21u, 0xceu, 0xcbu, 0x78u, 0x54u, 0x00u,
                0x00u, 0x00u, 0x00u, 0x49u, 0x45u, 0x4eu, 0x44u, 0xaeu, 0x42u, 0x60u, 0x82u,
                };
                return { reinterpret_cast<const char*>(bytes), sizeof(bytes) };
            }
            if (extension == ".hdr")
            {
                std::string bytes = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
                for (std::size_t index = 0; index < 4u; ++index)
                {
                    bytes.append("\x80\x40\x20\x81", 4u);
                }
                return bytes;
            }
            if (extension == ".dds")
            {
                // Legacy DDS: 2x2 RGBA8, one authored mip, tight eight-byte rows.
                std::string bytes(128u + 16u, '\0');
                const auto word = [&](std::size_t offset, std::uint32_t value)
                {
                    for (std::size_t index = 0; index < 4u; ++index)
                    {
                        bytes[offset + index] = static_cast<char>((value >> (index * 8u)) & 0xffu);
                    }
                };
                bytes.replace(0u, 4u, "DDS ");
                word(4u, 124u);
                word(8u, 0x100fu);
                word(12u, 2u);
                word(16u, 2u);
                word(20u, 8u);
                word(76u, 32u);
                word(80u, 0x41u);
                word(88u, 32u);
                word(92u, 0x000000ffu);
                word(96u, 0x0000ff00u);
                word(100u, 0x00ff0000u);
                word(104u, 0xff000000u);
                word(108u, 0x1000u);
                for (std::size_t pixel = 0; pixel < 4u; ++pixel)
                {
                    word(128u + pixel * 4u, 0xff204080u);
                }
                return bytes;
            }
            return {};
        }

        void StoreWord(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value,
            std::size_t width = 4u)
        {
            for (std::size_t index = 0; index < width; ++index)
            {
                bytes[offset + index] = static_cast<std::byte>((value >> (index * 8u)) & 0xffu);
            }
        }

        void CheckMalformedArtifacts(Checker& check, const std::vector<std::byte>& valid)
        {
            struct Mutation final
            {
                const char* name;
                std::size_t offset;
                std::uint64_t value;
                std::size_t width;
            };
            const Mutation mutations[]{
                { "magic", 0u, 0u, 4u },
                { "old source-image schema", 4u, 1u, 4u },
                { "header size", 8u, 63u, 4u },
                { "old source-image representation", 12u, 1u, 4u },
                { "unknown format", 16u, 0xffffffffu, 4u },
                { "reserved flags", 20u, 0xffffffffu, 4u },
                { "zero width", 24u, 0u, 4u },
                { "oversized height", 28u, 0xffffffffu, 4u },
                { "zero mip levels", 32u, 0u, 4u },
                { "zero array size", 36u, 0u, 4u },
                { "subresource count", 40u, 0xffffffffu, 4u },
                { "table entry size", 44u, 39u, 4u },
                { "overlapping payload header", 48u, 0u, 8u },
                { "total byte count", 56u, 0u, 8u },
                { "subresource extent", 64u, 0u, 4u },
                { "row pitch", 72u, 0u, 8u },
                { "slice pitch", 80u, 0xffffffffffffffffull, 8u },
                { "payload offset", 88u, 0u, 8u },
                { "payload size", 96u, 0u, 8u },
            };
            for (const auto& mutation : mutations)
            {
                auto bytes = valid;
                StoreWord(bytes, mutation.offset, mutation.value, mutation.width);
                TextureImage image;
                std::string failure;
                check.Check(!ck::DecodeCookedTexture(bytes, image, failure) && !failure.empty(),
                    std::string("Malformed CECT rejected: ") + mutation.name);
            }
            for (const auto size : { 0u, 63u, 103u })
            {
                TextureImage image;
                std::string failure;
                check.Check(!ck::DecodeCookedTexture(std::span(valid).first(size), image, failure),
                    "Truncated CECT header/table rejected");
            }
            auto trailing = valid;
            trailing.push_back(std::byte{});
            TextureImage image;
            std::string failure;
            check.Check(!ck::DecodeCookedTexture(trailing, image, failure), "Trailing CECT bytes rejected");
        }

        // 정상 자산 하나를 만든다. guid 는 canonical UUIDv4 여야 한다.
        struct Fixture final
        {
            std::filesystem::path assetRoot{};
            std::filesystem::path source{};
            std::string guid{};
            std::string payload{};
        };

        [[nodiscard]] Fixture MakeFixture(const std::filesystem::path& root,
            const std::string& relative, const std::string& guid,
            const std::string& payload, bool writeMeta = true,
            const std::string& metaGuid = {})
        {
            Fixture fixture;
            fixture.assetRoot = root;
            fixture.source = root / relative;
            fixture.guid = guid;
            fixture.payload = payload;
            (void)WriteFile(fixture.source, payload);
            if (writeMeta)
            {
                std::filesystem::path meta = fixture.source;
                meta += ".meta";
                (void)WriteFile(meta,
                    MetaYaml(metaGuid.empty() ? guid : metaGuid));
            }
            return fixture;
        }

        // ★ **어느 guard 가 걸었는지까지 본다.**
        //
        //   "거부됐다"만 보면 guard 를 지워도 초록일 수 있다 — 다른 guard 가
        //   우연히 같은 입력을 거부하기 때문이다. ShaderMeta producer 에서
        //   실제로 그 일이 났고(변이 둘이 통과), 거기서 죽은 guard 두 개가
        //   드러났다. 같은 결함이 여기 있을 이유가 없어서 함께 고친다.
        void ExpectRejected(Checker& check, const Fixture& fixture,
            const std::string& expectedContext, const std::string& what)
        {
            const ck::TextureCookProductResult result =
                ck::BuildTextureCookProduct({ fixture.source, fixture.assetRoot });
            check.Check(!result.Succeeded(), what + " — 거부해야 한다");
            // ★ 거부해 놓고 product 를 채워 주면 호출자가 그것을 게시한다.
            //   "실패했다"와 "아무것도 안 내놓았다"를 함께 봐야 한다.
            check.Check(!result.product.has_value(),
                what + " — 거부 시 product 가 없어야 한다");
            check.Check(!result.issues.empty(),
                what + " — 거부 사유가 있어야 한다");
            if (result.issues.empty())
            {
                return;
            }

            check.Check(result.issues.front().context == expectedContext,
                what + " — 사유가 '" + expectedContext + "' 여야 한다(실제 '"
                + result.issues.front().context + "')");
        }
    }

    bool RunExperimentTextureCookSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.texcook] 합성 검사\n";

        const std::filesystem::path root = MakeScratchRoot();
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
        {
            outLog += "    [실패] 임시 asset root 를 만들 수 없다\n";
            return false;
        }

        // Real encoded fixtures exercise source decoders and the new CECT boundary.
        struct Case final { const char* relative; const char* guid; const char* ext; };
        const Case cases[] = {
            { "Tex/a.png", "11111111-1111-4111-8111-111111111111", ".png" },
            { "Tex/b.hdr", "22222222-2222-4222-9222-222222222222", ".hdr" },
            { "Tex/c.dds", "33333333-3333-4333-a333-333333333333", ".dds" },
        };

        for (const Case& item : cases)
        {
            const std::string payload = MinimalImage(item.ext);
            const Fixture fixture = MakeFixture(root, item.relative,
                item.guid, payload);
            const ck::TextureCookProductResult result =
                ck::BuildTextureCookProduct({ fixture.source, root });

            const std::string tag = std::string("확장자 ") + item.ext;
            check.Check(result.Succeeded(), tag + " 는 통과해야 한다");
            if (!result.Succeeded())
            {
                continue;
            }

            const ck::TextureCookProduct& product = *result.product;
            TextureImage image;
            std::string failure;
            check.Check(ck::DecodeCookedTexture(product.artifactBytes, image, failure),
                tag + " artifact must be validated GPU-ready CECT");
            check.Check(BytesToString(product.artifactBytes) != payload,
                tag + " artifact must not retain the encoded source image");
            ck::Sha256Digest sourceDigest{};
            check.Check(ck::ComputeSha256(std::as_bytes(std::span(payload.data(), payload.size())),
                sourceDigest, failure) && product.sourceContentSha256 == sourceDigest,
                tag + " source snapshot hash must identify the consumed source bytes");
            if (std::string_view(item.ext) == ".png")
            {
                CheckMalformedArtifacts(check, product.artifactBytes);
            }
            check.Check(product.sourceExtension == item.ext,
                tag + " sourceExtension");

            const std::string expectedPath = std::string("Derived/Textures/")
                + std::string(item.guid).substr(0u, 2u) + "/" + item.guid
                + ".cetex";
            check.Check(product.artifactPath == expectedPath,
                tag + " artifactPath 가 GUID 주소여야 한다");

            const ck::CookedAssetManifestEntry& entry = product.manifestEntry;
            check.Check(entry.kind == ck::CookedAssetKind::Texture,
                tag + " manifest kind");
            check.Check(entry.formatVersion == ck::kTextureArtifactVersion,
                tag + " manifest formatVersion");
            check.Check(entry.byteSize == product.artifactBytes.size(),
                tag + " manifest byteSize");
            check.Check(entry.artifactPath == product.artifactPath,
                tag + " manifest artifactPath");
            // 텍스처는 잎이다. 의존이 붙으면 폐포 계산이 틀어진다.
            check.Check(entry.dependencies.empty(),
                tag + " 텍스처는 의존이 없어야 한다");

            // 해시가 내용에서 나오는가. 상수를 넣어 두고 통과하는 것을 막는다.
            ck::Sha256Digest expected{};
            std::string hashError;
            const bool hashed = ck::ComputeSha256(product.artifactBytes,
                expected, hashError);
            check.Check(hashed && entry.contentSha256 == expected,
                tag + " manifest contentSha256 가 내용 해시여야 한다");
        }

        // ── 2. 결정성 ──────────────────────────────────────────────────
        {
            const Fixture fixture = MakeFixture(root, "Det/x.png",
                "44444444-4444-4444-8444-444444444444", MinimalImage(".png"));
            const ck::TextureCookProductResult first =
                ck::BuildTextureCookProduct({ fixture.source, root });
            const ck::TextureCookProductResult second =
                ck::BuildTextureCookProduct({ fixture.source, root });
            check.Check(first.Succeeded() && second.Succeeded(),
                "결정성 — 두 번 다 통과해야 한다");
            if (first.Succeeded() && second.Succeeded())
            {
                check.Check(first.product->artifactBytes
                    == second.product->artifactBytes, "결정성 — 같은 바이트");
                check.Check(first.product->manifestEntry.contentSha256
                    == second.product->manifestEntry.contentSha256,
                    "결정성 — 같은 해시");
                check.Check(first.product->artifactPath
                    == second.product->artifactPath, "결정성 — 같은 경로");
            }
        }

        // Settings overlay is explicit, fingerprinted, and independent of GUID.
        {
            ck::TextureImportSettings inferred;
            inferred.colorSpace = ck::TextureColorSpace::Srgb;
            std::string failure;
            check.Check(ck::ParseTextureImportSettings("importSettings: { compression: None }\n", inferred, failure) &&
                inferred.colorSpace == ck::TextureColorSpace::Srgb, "Missing field retains model usage color space");
            check.Check(ck::ParseTextureImportSettings("importSettings: { colorSpace: Linear }\n", inferred, failure) &&
                inferred.colorSpace == ck::TextureColorSpace::Linear, "Explicit metadata overrides model usage default");
            const auto recipe = ck::TextureImportRecipe(inferred);
            auto changed = inferred;
            changed.maxDimension = 2u;
            check.Check(recipe != ck::TextureImportRecipe(changed), "Resize affects canonical recipe");
            changed = inferred;
            changed.mipPolicy = ck::TextureMipPolicy::GenerateFull;
            check.Check(recipe != ck::TextureImportRecipe(changed), "Mip policy affects canonical recipe");
            changed = inferred;
            changed.compression = ck::TextureCompression::BC7;
            check.Check(recipe != ck::TextureImportRecipe(changed), "Compression affects canonical recipe");
            changed = inferred;
            changed.normalMap = true;
            check.Check(recipe != ck::TextureImportRecipe(changed), "Normal interpretation affects canonical recipe");
            changed = inferred;
            changed.preserveAlphaCoverage = true;
            check.Check(recipe != ck::TextureImportRecipe(changed), "Alpha coverage affects canonical recipe");
            changed = inferred;
            changed.alphaCutoff = 0.25f;
            check.Check(recipe != ck::TextureImportRecipe(changed), "Alpha cutoff affects canonical recipe");
            changed = inferred;
            changed.compressionQuality = ck::TextureCompressionQuality::High;
            check.Check(recipe != ck::TextureImportRecipe(changed), "Compression quality affects canonical recipe");
            changed = inferred;
            changed.colorSpace = ck::TextureColorSpace::Srgb;
            check.Check(recipe != ck::TextureImportRecipe(changed), "Color space affects canonical recipe");
            check.Check(!ck::ParseTextureImportSettings("importSettings: { maxDimension: -1 }\n", inferred, failure),
                "Negative texture extent setting rejected");
            check.Check(!ck::ParseTextureImportSettings("importSettings: { colorSpace: Guess }\n", inferred, failure),
                "Unknown color-space setting rejected");
        }

        // A producer must consume the caller's pinned source and metadata, and
        // report a replacement snapshot instead of comparing source to CECT.
        {
            const auto fixture = MakeFixture(root, "Snapshot/a.png",
                "10101010-1010-4010-8010-101010101010", MinimalImage(".png"));
            std::size_t captures{};
            ck::TextureCookProductRequest request{ fixture.source, root };
            request.captureSource = [&](const auto& path, auto& bytes, std::string& failure)
            {
                ++captures;
                std::string input;
                if (!ReadFileBytes(path, input))
                {
                    failure = "fixture read failed";
                    return false;
                }
                const auto span = std::as_bytes(std::span(input.data(), input.size()));
                bytes.assign(span.begin(), span.end());
                return true;
            };
            check.Check(ck::BuildTextureCookProduct(request).Succeeded() && captures == 2u,
                "Producer uses the source and metadata capture callback exactly once");
            request.captureSource = [](const auto&, auto&, std::string& failure)
            {
                failure = "fixture denied capture";
                return false;
            };
            const auto denied = ck::BuildTextureCookProduct(request);
            check.Check(!denied.Succeeded() && !denied.product.has_value(),
                "Capture failure must never fall back to disk");
            request.captureSource = [&](const auto& path, auto& bytes, std::string& failure)
            {
                std::string input;
                if (!ReadFileBytes(path, input))
                {
                    failure = "fixture read failed";
                    return false;
                }
                const auto span = std::as_bytes(std::span(input.data(), input.size()));
                bytes.assign(span.begin(), span.end());
                if (path == fixture.source && !WriteFile(path, input + "changed after capture"))
                {
                    failure = "fixture replacement failed";
                    return false;
                }
                return true;
            };
            const auto replaced = ck::BuildTextureCookProduct(request);
            check.Check(!replaced.Succeeded() && !replaced.product && !replaced.issues.empty() &&
                replaced.issues.front().context == "texture.snapshot",
                "Source replaced after capture must fail the snapshot guard");
        }

        // ── 3. fail-closed ─────────────────────────────────────────────
        ExpectRejected(check, MakeFixture(root, "Bad/unsupported.tga",
            "55555555-5555-4555-8555-555555555555", "tga"),
            "texture.extension", "지원하지 않는 확장자(.tga)");

        ExpectRejected(check, MakeFixture(root, "Bad/nometa.png",
            "66666666-6666-4666-8666-666666666666", "png", false),
            "texture.meta", ".meta 누락");

        ExpectRejected(check, MakeFixture(root, "Bad/brace.png",
            "77777777-7777-4777-8777-777777777777", "png", true,
            "{77777777-7777-4777-8777-777777777777}"),
            "texture.meta", "brace 표기 GUID");

        ExpectRejected(check, MakeFixture(root, "Bad/upper.png",
            "88888888-8888-4888-8888-888888888888", "png", true,
            "88888888-8888-4888-8888-88888888888A"),
            "texture.meta", "대문자 GUID");

        ExpectRejected(check, MakeFixture(root, "Bad/nil.png",
            "99999999-9999-4999-8999-999999999999", "png", true,
            "00000000-0000-0000-0000-000000000000"),
            "texture.meta", "nil GUID");

        ExpectRejected(check, MakeFixture(root, "Bad/nonv4.png",
            "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "png", true,
            "aaaaaaaa-aaaa-1aaa-8aaa-aaaaaaaaaaaa"),
            "texture.meta", "UUIDv1 GUID");

        ExpectRejected(check, MakeFixture(root, "Bad/not-image.png",
            "abababab-abab-4bab-8bab-abababababab", "not an encoded image"),
            "texture.decode", "Malformed source image");

        ExpectRejected(check, MakeFixture(root, "Bad/empty.png",
            "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", ""),
            "texture.read", "0바이트 source");

        {
            // asset root 밖. root 의 형제로 두어 `..` 로만 닿게 한다.
            const std::filesystem::path outsideRoot = root / "Inside";
            std::filesystem::create_directories(outsideRoot, error);
            Fixture outside = MakeFixture(root, "Outside/o.png",
                "cccccccc-cccc-4ccc-8ccc-cccccccccccc", "outside");
            outside.assetRoot = outsideRoot;
            ExpectRejected(check, outside, "request.sourcePath",
                "asset root 밖 source");
        }

        {
            Fixture missing = MakeFixture(root, "Bad/present.png",
                "dddddddd-dddd-4ddd-8ddd-dddddddddddd", "present");
            missing.source = root / "Bad" / "does-not-exist.png";
            ExpectRejected(check, missing, "request.sourcePath",
                "존재하지 않는 source");
        }

        {
            Fixture badRoot = MakeFixture(root, "Bad/root.png",
                "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee", "root");
            badRoot.assetRoot = root / "no-such-directory";
            ExpectRejected(check, badRoot, "request.assetRoot",
                "디렉터리가 아닌 asset root");
        }

        // ── 4. 경로 헬퍼가 깨지는 표기를 막는가 ────────────────────────
        {
            experiment::AssetId id{};
            const bool parsed = experiment::TryParseCanonicalAssetId(
                "12345678-1234-4234-8234-123456789abc", id);
            check.Check(parsed, "경로 헬퍼 fixture GUID 파싱");

            check.Check(ck::MakeDerivedTextureArtifactPath(id, ".cetex")
                == "Derived/Textures/12/12345678-1234-4234-8234-123456789abc.cetex",
                "경로 헬퍼 정상 표기");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, "png").empty(),
                "경로 헬퍼 — 점 없는 확장자 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, "").empty(),
                "경로 헬퍼 — 빈 확장자 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, ".").empty(),
                "경로 헬퍼 — 점만 있는 확장자 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, ".a/b").empty(),
                "경로 헬퍼 — 슬래시 포함 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, ".a.b").empty(),
                "경로 헬퍼 — 점 두 개 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(
                experiment::AssetId{}, ".png").empty(),
                "경로 헬퍼 — nil GUID 거부");
        }

        // ── 5. allowlist 술어 ──────────────────────────────────────────
        check.Check(ck::IsSupportedTextureExtension(".png"), "allowlist .png");
        check.Check(ck::IsSupportedTextureExtension(".hdr"), "allowlist .hdr");
        check.Check(ck::IsSupportedTextureExtension(".dds"), "allowlist .dds");
        check.Check(ck::IsSupportedTextureExtension(".jpg"), "allowlist .jpg");
        check.Check(ck::IsSupportedTextureExtension(".jpeg"), "allowlist .jpeg source alias");
        check.Check(!ck::IsSupportedTextureExtension(".PNG"),
            "allowlist 는 소문자만 받는다");
        check.Check(!ck::IsSupportedTextureExtension(".tga"),
            "allowlist 에 없는 확장자");
        check.Check(!ck::IsSupportedTextureExtension(""), "allowlist 빈 문자열");

        std::filesystem::remove_all(root, error);

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        const bool pipelinePassed = RunExperimentTexturePipelineSelfTest(outLog);
        return check.failed == 0u && pipelinePassed;
    }

    bool RunExperimentTextureCookReal(const std::string& assetRootPath,
        const std::string& texturePath, std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.texcook] 실자산: " + texturePath + "\n";

        const std::filesystem::path source(texturePath);
        const ck::TextureCookProductResult result =
            ck::BuildTextureCookProduct({ source, std::filesystem::path(assetRootPath) });

        check.Check(result.Succeeded(), "실자산 cook 이 통과해야 한다");
        if (!result.Succeeded())
        {
            for (const ck::TextureCookProductIssue& issue : result.issues)
            {
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            }
            outLog += "  실자산 단정 실패\n";
            return false;
        }

        const ck::TextureCookProduct& product = *result.product;
        std::string original;
        check.Check(ReadFileBytes(source, original), "원본을 읽을 수 있어야 한다");
        TextureImage image;
        std::string failure;
        check.Check(ck::DecodeCookedTexture(product.artifactBytes, image, failure),
            "Artifact must contain validated GPU-ready subresources");
        check.Check(product.manifestEntry.byteSize == product.artifactBytes.size(),
            "Manifest byteSize must describe cooked bytes");
        ck::Sha256Digest sourceDigest{};
        check.Check(ck::ComputeSha256(std::as_bytes(std::span(original.data(), original.size())),
            sourceDigest, failure) && product.sourceContentSha256 == sourceDigest,
            "Source hash must identify the consumed encoded image");

        ck::Sha256Digest expected{};
        std::string hashError;
        const bool hashed = ck::ComputeSha256(product.artifactBytes,
            expected, hashError);
        check.Check(hashed && product.manifestEntry.contentSha256 == expected,
            "manifest 해시가 내용 해시여야 한다");

        char summary[256]{};
        std::snprintf(summary, sizeof(summary),
            "  실자산 단정 %zu/%zu · %llu B · %s\n",
            check.passed, check.passed + check.failed,
            static_cast<unsigned long long>(product.artifactBytes.size()),
            product.artifactPath.c_str());
        outLog += summary;
        return check.failed == 0u;
    }
}
