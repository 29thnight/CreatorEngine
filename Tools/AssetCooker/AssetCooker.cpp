#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/CookedModelCodec.h"
#include "Experiment/Cooked/MaterialCookProducer.h"
#include "Experiment/Cooked/ModelCookProducer.h"
#include "Experiment/Cooked/SceneCookProducer.h"
#include "Experiment/Cooked/ShaderMetaCookProducer.h"
#include "Experiment/Cooked/TextureCookProducer.h"
#include "ModelIdentityRefresher.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <process.h>
#include <set>
#include <sstream>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    namespace ck = experiment::cooked;

    struct Arguments final
    {
        enum class Mode
        {
            Cook,
            RefreshModelIdentities,
        };

        Mode mode{ Mode::Cook };
        std::filesystem::path assetRoot{};
        std::filesystem::path outputRoot{};
        std::vector<std::filesystem::path> models{};
        std::vector<std::filesystem::path> textures{};
        std::vector<std::filesystem::path> shaderMetas{};
        std::vector<std::filesystem::path> materials{};
        std::vector<std::filesystem::path> scenes{};
    };

    void PrintUsage()
    {
        std::cout
            << "Usage: AssetCooker --asset-root <Assets> --output <new-dir> "
               "[--model <source> ...] [--texture <source> ...] [--shadermeta <source> ...] [--material <source> ...] [--scene <source> ...]\n"
            << "       AssetCooker --refresh-model-identities "
               "--asset-root <Assets> --model <source> [--model <source> ...]\n";
    }

    [[nodiscard]] bool ParseArguments(int argc, wchar_t** argv,
        Arguments& out, std::string& failure)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::wstring_view option(argv[index]);
            if (option == L"--help" || option == L"-h")
            {
                PrintUsage();
                return false;
            }
            if (option == L"--refresh-model-identities")
            {
                if (out.mode == Arguments::Mode::RefreshModelIdentities)
                {
                    failure = "--refresh-model-identities는 한 번만 지정할 수 있다.";
                    return false;
                }
                out.mode = Arguments::Mode::RefreshModelIdentities;
                continue;
            }
            if (index + 1 >= argc)
            {
                failure = "option 값이 없다.";
                return false;
            }

            const std::filesystem::path value(argv[++index]);
            if (option == L"--asset-root")
            {
                if (!out.assetRoot.empty())
                {
                    failure = "--asset-root는 한 번만 지정할 수 있다.";
                    return false;
                }
                out.assetRoot = value;
            }
            else if (option == L"--output")
            {
                if (!out.outputRoot.empty())
                {
                    failure = "--output은 한 번만 지정할 수 있다.";
                    return false;
                }
                out.outputRoot = value;
            }
            else if (option == L"--model")
            {
                out.models.push_back(value);
            }
            else if (option == L"--texture")
            {
                out.textures.push_back(value);
            }
            else if (option == L"--shadermeta")
            {
                out.shaderMetas.push_back(value);
            }
            else if (option == L"--material")
            {
                out.materials.push_back(value);
            }
            else if (option == L"--scene")
            {
                // .creator 와 .prefab 을 함께 받는다. producer 가 확장자로
                // kind 를 정한다.
                out.scenes.push_back(value);
            }
            else
            {
                failure = "알 수 없는 option이다.";
                return false;
            }
        }

        if (out.assetRoot.empty())
        {
            failure = "--asset-root가 필요하다.";
            return false;
        }
        // identity refresh는 model 전용 경계다. texture sidecar는 authoring
        // 쪽에서 이미 발급돼 있고, 이 도구가 손댈 대상이 아니다.
        if (out.mode == Arguments::Mode::RefreshModelIdentities)
        {
            if (!out.textures.empty() || !out.shaderMetas.empty()
                || !out.materials.empty() || !out.scenes.empty())
            {
                failure = "identity refresh에는 --texture/--shadermeta/--material을 지정할 수 없다.";
                return false;
            }
            if (out.models.empty())
            {
                failure = "identity refresh에는 하나 이상의 --model이 필요하다.";
                return false;
            }
        }
        else if (out.models.empty() && out.textures.empty()
            && out.shaderMetas.empty() && out.materials.empty()
            && out.scenes.empty())
        {
            failure = "Cook에는 하나 이상의 --model/--texture/--shadermeta/--material이 필요하다.";
            return false;
        }
        if (out.mode == Arguments::Mode::Cook && out.outputRoot.empty())
        {
            failure = "Cook에는 --output이 필요하다.";
            return false;
        }
        if (out.mode == Arguments::Mode::RefreshModelIdentities
            && !out.outputRoot.empty())
        {
            failure = "identity refresh에는 --output을 지정할 수 없다.";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool IsContainedPath(const std::filesystem::path& root,
        const std::filesystem::path& child)
    {
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(child, root, error);
        if (error || relative.empty() || relative.is_absolute()) return false;
        for (const std::filesystem::path& part : relative)
        {
            if (part == "..") return false;
        }
        return true;
    }

    [[nodiscard]] std::filesystem::path ResolveOutputIntent(
        const std::filesystem::path& path, std::error_code& error)
    {
        std::filesystem::path absolute = std::filesystem::absolute(path, error);
        if (error || absolute.filename().empty()) return {};

        const std::filesystem::path parent =
            std::filesystem::weakly_canonical(absolute.parent_path(), error);
        if (error || parent.empty()) return {};
        return (parent / absolute.filename()).lexically_normal();
    }

    [[nodiscard]] bool WriteBinaryFile(const std::filesystem::path& path,
        std::span<const std::byte> bytes, std::string& failure)
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            failure = "산출물 디렉터리를 만들 수 없다: " + error.message();
            return false;
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            failure = "산출물 파일을 열 수 없다: " + path.string();
            return false;
        }
        if (!bytes.empty())
        {
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        }
        stream.flush();
        if (!stream)
        {
            failure = "산출물 파일을 완전히 쓰지 못했다: " + path.string();
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ReadBinaryFile(const std::filesystem::path& path,
        std::vector<std::byte>& out, std::string& failure)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            failure = "검증할 파일을 열 수 없다: " + path.string();
            return false;
        }
        stream.seekg(0, std::ios::end);
        const std::streamoff byteCount = stream.tellg();
        if (byteCount < 0)
        {
            failure = "검증할 파일 크기를 읽을 수 없다: " + path.string();
            return false;
        }
        stream.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(byteCount));
        if (!out.empty())
            stream.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
        if (!(stream.good() || stream.eof()))
        {
            failure = "검증할 파일을 완전히 읽지 못했다: " + path.string();
            return false;
        }
        return true;
    }

    class StagingCleanup final
    {
    public:
        explicit StagingCleanup(std::filesystem::path path)
            : path_(std::move(path))
        {
        }

        ~StagingCleanup()
        {
            if (!armed_) return;
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        void Release() noexcept { armed_ = false; }

    private:
        std::filesystem::path path_{};
        bool armed_{ true };
    };

    [[nodiscard]] std::filesystem::path MakeStagingPath(
        const std::filesystem::path& outputRoot)
    {
        const auto ticks = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        // Windows narrow-stream file open still fails at the legacy 260-character
        // boundary on some hosts. Keep the atomic sibling staging directory unique,
        // but do not repeat the output leaf or decimal timestamp in its name.
        std::ostringstream name;
        name << ".cook-" << std::hex << static_cast<unsigned int>(_getpid())
            << '-' << static_cast<std::uint64_t>(ticks);
        return outputRoot.parent_path() / name.str();
    }

    [[nodiscard]] int Run(const Arguments& arguments)
    {
        std::error_code error;
        const std::filesystem::path assetRoot =
            std::filesystem::weakly_canonical(arguments.assetRoot, error);
        if (error || !std::filesystem::is_directory(assetRoot, error))
        {
            std::cerr << "asset-cooker error: asset root가 유효하지 않다.\n";
            return 2;
        }

        error.clear();
        const std::filesystem::path outputRoot =
            ResolveOutputIntent(arguments.outputRoot, error);
        if (error || outputRoot.empty())
        {
            std::cerr << "asset-cooker error: output 경로를 해석할 수 없다.\n";
            return 2;
        }
        if (std::filesystem::exists(outputRoot, error))
        {
            std::cerr << "asset-cooker error: output은 존재하지 않는 새 디렉터리여야 한다.\n";
            return 2;
        }
        if (IsContainedPath(assetRoot, outputRoot) || outputRoot == assetRoot)
        {
            std::cerr << "asset-cooker error: output을 asset root 안에 둘 수 없다.\n";
            return 2;
        }

        std::vector<ck::ModelCookProduct> products;
        products.reserve(arguments.models.size());
        ck::CookedAssetManifest manifest;
        std::set<std::string> artifactPaths;
        std::size_t totalMaterials = 0u;
        std::size_t totalEmbeddedTextures = 0u;
        std::size_t totalTextureReferences = 0u;
        std::size_t totalExternalTextureReferences = 0u;
        std::uint64_t totalEmbeddedTextureBytes = 0u;
        std::uint64_t totalArtifactBytes = 0u;

        for (const std::filesystem::path& model : arguments.models)
        {
            ck::ModelCookProductResult result = ck::BuildModelCookProduct(
                { model, assetRoot });
            if (!result.Succeeded())
            {
                for (const ck::ModelCookProductIssue& issue : result.issues)
                {
                    std::cerr << "asset-cooker error: " << issue.context
                        << ": " << issue.message << '\n';
                }
                return 3;
            }

            ck::ModelCookProduct product = std::move(*result.product);
            if (!artifactPaths.insert(product.artifactPath).second)
            {
                std::cerr << "asset-cooker error: 중복 model artifact path다.\n";
                return 3;
            }
            bool embeddedPathCollision = false;
            for (const ck::EmbeddedTextureArtifact& embedded
                : product.embeddedTextures)
            {
                if (!artifactPaths.insert(embedded.artifactPath).second)
                {
                    // 경로는 GUID 에서 나온다. 충돌은 곧 두 자산이 같은 GUID 를
                    // 들고 있다는 뜻이고, 어느 파일인지 여기서만 보인다.
                    std::cerr << "asset-cooker error: 중복 embedded texture "
                        "artifact path다: " << embedded.artifactPath << '\n';
                    embeddedPathCollision = true;
                    break;
                }
                totalEmbeddedTextureBytes += embedded.artifactBytes.size();
            }
            if (embeddedPathCollision) return 3;

            totalMaterials += product.materialCount;
            totalEmbeddedTextures += product.embeddedTextureCount;
            totalTextureReferences += product.textureReferenceCount;
            totalExternalTextureReferences +=
                product.externalTextureReferenceCount;
            totalArtifactBytes += product.artifactBytes.size();
            // ★ 옮기지 않고 복사한다.
            //
            //   원래는 `std::move` 였다. 그때는 게시 전 검증이 `assetId` 하나만
            //   읽었고 `AssetId` 는 옮겨도 값이 남아서 아무 일이 없었다. b2c-3
            //   에서 kind·artifactPath·byteSize 까지 읽기 시작하자 곧바로
            //   **빈 artifactPath** 로 터졌다 — 옮겨진 `std::string` 이 비어
            //   있었기 때문이다. entry 는 작고 개수도 적으므로 복사가 맞다.
            for (const ck::CookedAssetManifestEntry& entry : product.manifestEntries)
                manifest.entries.push_back(entry);
            products.push_back(std::move(product));
        }

        std::vector<ck::TextureCookProduct> textureProducts;
        textureProducts.reserve(arguments.textures.size());
        std::uint64_t totalTextureBytes = 0u;

        for (const std::filesystem::path& texture : arguments.textures)
        {
            ck::TextureCookProductResult result = ck::BuildTextureCookProduct(
                { texture, assetRoot });
            if (!result.Succeeded())
            {
                for (const ck::TextureCookProductIssue& issue : result.issues)
                {
                    std::cerr << "asset-cooker error: " << issue.context
                        << ": " << issue.message << '\n';
                }
                return 3;
            }

            ck::TextureCookProduct product = std::move(*result.product);
            if (!artifactPaths.insert(product.artifactPath).second)
            {
                // 경로는 GUID 에서 나오므로, 이건 곧 두 texture 가 같은 GUID 를
                // 들고 있다는 뜻이다. manifest writer 도 중복 ID 를 거부하지만
                // 여기서 잡아야 어느 파일인지가 보인다.
                std::cerr << "asset-cooker error: 중복 texture artifact path다: "
                    << product.artifactPath << '\n';
                return 3;
            }
            totalTextureBytes += product.artifactBytes.size();
            manifest.entries.push_back(product.manifestEntry);
            textureProducts.push_back(std::move(product));
        }

        std::vector<ck::ShaderMetaCookProduct> shaderMetaProducts;
        shaderMetaProducts.reserve(arguments.shaderMetas.size());
        std::uint64_t totalShaderMetaBytes = 0u;

        for (const std::filesystem::path& shaderMeta : arguments.shaderMetas)
        {
            ck::ShaderMetaCookProductResult result =
                ck::BuildShaderMetaCookProduct({ shaderMeta, assetRoot });
            if (!result.Succeeded())
            {
                for (const ck::ShaderMetaCookProductIssue& issue : result.issues)
                {
                    std::cerr << "asset-cooker error: " << issue.context
                        << ": " << issue.message << '\n';
                }
                return 3;
            }

            ck::ShaderMetaCookProduct product = std::move(*result.product);
            if (!artifactPaths.insert(product.artifactPath).second)
            {
                std::cerr << "asset-cooker error: 중복 shadermeta artifact path다: "
                    << product.artifactPath << '\n';
                return 3;
            }
            totalShaderMetaBytes += product.artifactBytes.size();
            manifest.entries.push_back(product.manifestEntry);
            shaderMetaProducts.push_back(std::move(product));
        }

        std::vector<ck::MaterialCookProduct> materialProducts;
        materialProducts.reserve(arguments.materials.size());
        std::uint64_t totalStandaloneMaterialBytes = 0u;

        for (const std::filesystem::path& material : arguments.materials)
        {
            ck::MaterialCookProductResult result =
                ck::BuildMaterialCookProduct({ material, assetRoot });
            if (!result.Succeeded())
            {
                for (const ck::MaterialCookProductIssue& issue : result.issues)
                {
                    std::cerr << "asset-cooker error: " << issue.context
                        << ": " << issue.message << '\n';
                }
                return 3;
            }

            ck::MaterialCookProduct product = std::move(*result.product);
            if (!artifactPaths.insert(product.artifactPath).second)
            {
                std::cerr << "asset-cooker error: 중복 material artifact path다: "
                    << product.artifactPath << '\n';
                return 3;
            }
            totalStandaloneMaterialBytes += product.artifactBytes.size();
            manifest.entries.push_back(product.manifestEntry);
            materialProducts.push_back(std::move(product));
        }

        std::vector<ck::SceneCookProduct> sceneProducts;
        sceneProducts.reserve(arguments.scenes.size());
        std::uint64_t totalSceneBytes = 0u;
        std::size_t totalScenes = 0u;
        std::size_t totalPrefabs = 0u;
        std::size_t totalLegacyTextureNames = 0u;
        std::size_t totalUnproducedGuids = 0u;

        for (const std::filesystem::path& scene : arguments.scenes)
        {
            ck::SceneCookProductResult result =
                ck::BuildSceneCookProduct({ scene, assetRoot });
            if (!result.Succeeded())
            {
                for (const ck::SceneCookProductIssue& issue : result.issues)
                {
                    std::cerr << "asset-cooker error: " << issue.context
                        << ": " << issue.message << '\n';
                }
                return 3;
            }

            ck::SceneCookProduct product = std::move(*result.product);
            if (!artifactPaths.insert(product.artifactPath).second)
            {
                std::cerr << "asset-cooker error: 중복 scene/prefab artifact path다: "
                    << product.artifactPath << '\n';
                return 3;
            }
            if (product.kind == ck::CookedAssetKind::Scene) ++totalScenes;
            else ++totalPrefabs;
            totalSceneBytes += product.artifactBytes.size();
            totalLegacyTextureNames += product.legacyTextureNameReferences;
            totalUnproducedGuids += product.unproducedGuidReferences;
            manifest.entries.push_back(product.manifestEntry);
            sceneProducts.push_back(std::move(product));
        }

        const ck::AssetManifestWriteResult manifestWrite =
            ck::WriteAssetManifest(manifest);
        if (!manifestWrite.Succeeded())
        {
            for (const ck::AssetManifestIssue& issue : manifestWrite.issues)
            {
                std::cerr << "asset-cooker error: " << issue.context
                    << ": " << issue.message << '\n';
            }
            return 4;
        }

        const std::filesystem::path stagingRoot = MakeStagingPath(outputRoot);
        error.clear();
        const bool stagingExists = std::filesystem::exists(stagingRoot, error);
        if (error || stagingExists)
        {
            std::cerr << "asset-cooker error: staging 경로를 사용할 수 없다: "
                << (error ? error.message() : "path collision") << '\n';
            return 5;
        }
        std::filesystem::create_directories(stagingRoot, error);
        if (error)
        {
            std::cerr << "asset-cooker error: staging 디렉터리를 만들 수 없다: "
                << error.message() << '\n';
            return 5;
        }
        StagingCleanup cleanup(stagingRoot);

        std::string failure;
        for (const ck::ModelCookProduct& product : products)
        {
            const std::filesystem::path artifactFile =
                stagingRoot / std::filesystem::path(product.artifactPath);
            if (!WriteBinaryFile(artifactFile, product.artifactBytes, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }

            std::vector<std::byte> persisted;
            if (!ReadBinaryFile(artifactFile, persisted, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }
            experiment::ModelDraft restored;
            std::vector<experiment::ModelLoadIssue> readIssues;
            if (!ck::Read(persisted, restored, readIssues)
                || restored.metadata.assetId != product.modelAssetId)
            {
                std::cerr << "asset-cooker error: 게시 전 CEMC 재검증이 실패했다.\n";
                return 5;
            }

            for (const ck::EmbeddedTextureArtifact& embedded
                : product.embeddedTextures)
            {
                const std::filesystem::path embeddedFile =
                    stagingRoot / std::filesystem::path(embedded.artifactPath);
                if (!WriteBinaryFile(embeddedFile, embedded.artifactBytes, failure))
                {
                    std::cerr << "asset-cooker error: " << failure << '\n';
                    return 5;
                }
                std::vector<std::byte> embeddedPersisted;
                if (!ReadBinaryFile(embeddedFile, embeddedPersisted, failure))
                {
                    std::cerr << "asset-cooker error: " << failure << '\n';
                    return 5;
                }
                if (embeddedPersisted != embedded.artifactBytes)
                {
                    std::cerr << "asset-cooker error: 게시 전 embedded texture "
                        "재검증이 실패했다: " << embedded.artifactPath << '\n';
                    return 5;
                }
            }
        }

        for (const ck::TextureCookProduct& product : textureProducts)
        {
            const std::filesystem::path artifactFile =
                stagingRoot / std::filesystem::path(product.artifactPath);
            if (!WriteBinaryFile(artifactFile, product.artifactBytes, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }

            // 게시 전 재판독. pass-through 라 "디코드"할 것이 없으므로 바이트가
            // 그대로 돌아오는지를 본다 — 이게 이 artifact 에 대해 확인할 수 있는
            // 전부이고, 확인할 수 없는 것을 확인한 척하지 않는다.
            std::vector<std::byte> persisted;
            if (!ReadBinaryFile(artifactFile, persisted, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }
            if (persisted != product.artifactBytes)
            {
                std::cerr << "asset-cooker error: 게시 전 texture 재검증이 실패했다: "
                    << product.artifactPath << '\n';
                return 5;
            }
        }

        for (const ck::ShaderMetaCookProduct& product : shaderMetaProducts)
        {
            const std::filesystem::path artifactFile =
                stagingRoot / std::filesystem::path(product.artifactPath);
            if (!WriteBinaryFile(artifactFile, product.artifactBytes, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }

            std::vector<std::byte> persisted;
            if (!ReadBinaryFile(artifactFile, persisted, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }
            if (persisted != product.artifactBytes)
            {
                std::cerr << "asset-cooker error: 게시 전 shadermeta 재검증이 실패했다: "
                    << product.artifactPath << '\n';
                return 5;
            }
        }

        for (const ck::MaterialCookProduct& product : materialProducts)
        {
            const std::filesystem::path artifactFile =
                stagingRoot / std::filesystem::path(product.artifactPath);
            if (!WriteBinaryFile(artifactFile, product.artifactBytes, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }

            std::vector<std::byte> persisted;
            if (!ReadBinaryFile(artifactFile, persisted, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }
            if (persisted != product.artifactBytes)
            {
                std::cerr << "asset-cooker error: 게시 전 material 재검증이 실패했다: "
                    << product.artifactPath << '\n';
                return 5;
            }
        }

        for (const ck::SceneCookProduct& product : sceneProducts)
        {
            const std::filesystem::path artifactFile =
                stagingRoot / std::filesystem::path(product.artifactPath);
            if (!WriteBinaryFile(artifactFile, product.artifactBytes, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }

            std::vector<std::byte> persisted;
            if (!ReadBinaryFile(artifactFile, persisted, failure))
            {
                std::cerr << "asset-cooker error: " << failure << '\n';
                return 5;
            }
            if (persisted != product.artifactBytes)
            {
                std::cerr << "asset-cooker error: 게시 전 scene/prefab 재검증이 실패했다: "
                    << product.artifactPath << '\n';
                return 5;
            }
        }

        const std::filesystem::path manifestFile =
            stagingRoot / "Derived/asset-manifest.cemf";
        if (!WriteBinaryFile(manifestFile, manifestWrite.bytes, failure))
        {
            std::cerr << "asset-cooker error: " << failure << '\n';
            return 5;
        }

        std::vector<std::byte> persistedManifest;
        if (!ReadBinaryFile(manifestFile, persistedManifest, failure))
        {
            std::cerr << "asset-cooker error: " << failure << '\n';
            return 5;
        }
        ck::CookedAssetManifest restoredManifest;
        std::vector<ck::AssetManifestIssue> manifestIssues;
        if (!ck::ReadAssetManifest(persistedManifest,
            restoredManifest, manifestIssues)
            || restoredManifest.entries.size() != manifest.entries.size())
        {
            std::cerr << "asset-cooker error: 게시 전 CEMF 재검증이 실패했다.\n";
            return 5;
        }

        for (const ck::ModelCookProduct& product : products)
        {
            const ck::CookedAssetManifestEntry* modelEntry =
                restoredManifest.Find(product.modelAssetId);
            ck::Sha256Digest digest{};
            std::string hashError;
            manifestIssues.clear();
            if (!modelEntry
                || !ck::ComputeSha256(product.artifactBytes, digest, hashError)
                || !ck::VerifyArtifact(*modelEntry,
                    product.artifactBytes.size(), digest, manifestIssues))
            {
                std::cerr << "asset-cooker error: manifest artifact 검증이 실패했다.\n";
                return 5;
            }
            // ★ index 로 종류를 가정하지 않는다. b2c-3 부터 이 목록에는
            //   model·embedded texture·material 이 섞여 있고, "1번부터는 전부
            //   material" 이라는 옛 전제는 임베디드 entry 를 material 로
            //   오독한다. kind 로 갈라서 각자에게 맞는 검증을 건다.
            for (std::size_t index = 1u;
                index < product.manifestEntries.size(); ++index)
            {
                const ck::CookedAssetManifestEntry& expected =
                    product.manifestEntries[index];
                const ck::CookedAssetManifestEntry* found =
                    restoredManifest.Find(expected.assetId);
                if (!found || found->kind != expected.kind
                    || found->artifactPath != expected.artifactPath)
                {
                    std::cerr << "asset-cooker error: subasset lookup 검증이 실패했다: "
                        << expected.artifactPath << '\n';
                    return 5;
                }

                manifestIssues.clear();
                if (expected.kind == ck::CookedAssetKind::Material)
                {
                    // material 은 model CEMC 안의 subasset 이라 크기·해시가
                    // model artifact 의 것이다.
                    if (!ck::VerifyArtifact(*found,
                        product.artifactBytes.size(), digest, manifestIssues))
                    {
                        std::cerr << "asset-cooker error: material subasset artifact 검증이 실패했다.\n";
                        return 5;
                    }
                    continue;
                }

                // embedded texture 는 자기 artifact 를 가진다.
                if (!ck::VerifyArtifact(*found, expected.byteSize,
                    expected.contentSha256, manifestIssues))
                {
                    std::cerr << "asset-cooker error: embedded texture artifact 검증이 실패했다: "
                        << expected.artifactPath << '\n';
                    return 5;
                }
            }
        }

        for (const ck::TextureCookProduct& product : textureProducts)
        {
            const ck::CookedAssetManifestEntry* textureEntry =
                restoredManifest.Find(product.textureAssetId);
            ck::Sha256Digest digest{};
            std::string hashError;
            manifestIssues.clear();
            if (!textureEntry
                || textureEntry->kind != ck::CookedAssetKind::Texture
                || textureEntry->artifactPath != product.artifactPath
                || !ck::ComputeSha256(product.artifactBytes, digest, hashError)
                || !ck::VerifyArtifact(*textureEntry,
                    product.artifactBytes.size(), digest, manifestIssues))
            {
                std::cerr << "asset-cooker error: texture manifest 검증이 실패했다: "
                    << product.artifactPath << '\n';
                return 5;
            }
        }

        for (const ck::ShaderMetaCookProduct& product : shaderMetaProducts)
        {
            const ck::CookedAssetManifestEntry* entry =
                restoredManifest.Find(product.shaderMetaAssetId);
            ck::Sha256Digest digest{};
            std::string hashError;
            manifestIssues.clear();
            if (!entry
                || entry->kind != ck::CookedAssetKind::ShaderMeta
                || entry->artifactPath != product.artifactPath
                || !ck::ComputeSha256(product.artifactBytes, digest, hashError)
                || !ck::VerifyArtifact(*entry,
                    product.artifactBytes.size(), digest, manifestIssues))
            {
                std::cerr << "asset-cooker error: shadermeta manifest 검증이 실패했다: "
                    << product.artifactPath << '\n';
                return 5;
            }
        }

        for (const ck::MaterialCookProduct& product : materialProducts)
        {
            const ck::CookedAssetManifestEntry* entry =
                restoredManifest.Find(product.materialAssetId);
            ck::Sha256Digest digest{};
            std::string hashError;
            manifestIssues.clear();
            if (!entry
                || entry->kind != ck::CookedAssetKind::Material
                || entry->artifactPath != product.artifactPath
                || !ck::ComputeSha256(product.artifactBytes, digest, hashError)
                || !ck::VerifyArtifact(*entry,
                    product.artifactBytes.size(), digest, manifestIssues))
            {
                std::cerr << "asset-cooker error: material manifest 검증이 실패했다: "
                    << product.artifactPath << '\n';
                return 5;
            }
        }

        for (const ck::SceneCookProduct& product : sceneProducts)
        {
            const ck::CookedAssetManifestEntry* entry =
                restoredManifest.Find(product.sceneAssetId);
            ck::Sha256Digest digest{};
            std::string hashError;
            manifestIssues.clear();
            if (!entry
                || entry->kind != product.kind
                || entry->artifactPath != product.artifactPath
                || !ck::ComputeSha256(product.artifactBytes, digest, hashError)
                || !ck::VerifyArtifact(*entry,
                    product.artifactBytes.size(), digest, manifestIssues))
            {
                std::cerr << "asset-cooker error: scene/prefab manifest 검증이 실패했다: "
                    << product.artifactPath << '\n';
                return 5;
            }
        }

        error.clear();
        std::filesystem::rename(stagingRoot, outputRoot, error);
        if (error)
        {
            std::cerr << "asset-cooker error: 최종 디렉터리를 게시할 수 없다: "
                << error.message() << '\n';
            return 6;
        }
        cleanup.Release();

        std::cout << "asset-cooker models=" << products.size()
            << " materials=" << totalMaterials
            << " embeddedTextures=" << totalEmbeddedTextures
            << " textureReferences=" << totalTextureReferences
            << " externalTextureRefs=" << totalExternalTextureReferences
            << " embeddedTextureBytes=" << totalEmbeddedTextureBytes
            << " textures=" << textureProducts.size()
            << " shaderMetas=" << shaderMetaProducts.size()
            << " standaloneMaterials=" << materialProducts.size()
            << " standaloneMaterialBytes=" << totalStandaloneMaterialBytes
            << " scenes=" << totalScenes
            << " prefabs=" << totalPrefabs
            << " sceneBytes=" << totalSceneBytes
            // ★ 간선으로 그리지 못한 참조. 0 이 되어야 D5-c 의 "source path
            //   탐색 없이"가 성립한다 — 숫자를 숨기면 그 판정을 못 한다.
            << " legacyTextureNameRefs=" << totalLegacyTextureNames
            << " unproducedGuidRefs=" << totalUnproducedGuids
            << " manifestEntries=" << manifest.entries.size()
            << " artifactBytes=" << totalArtifactBytes
            << " textureBytes=" << totalTextureBytes
            << " shaderMetaBytes=" << totalShaderMetaBytes
            << " manifest=Derived/asset-manifest.cemf\n";
        for (const ck::ShaderMetaCookProduct& product : shaderMetaProducts)
        {
            // ★ source 셰이더 GUID 를 여기서 소비한다. 해소는 증명해 놓고
            //   아무도 안 읽는 필드로 두면 B3 가 그것을 못 믿는다.
            std::cout << "asset-cooker shadermeta="
                << Uuid::ToString(product.shaderMetaAssetId.value)
                << " name=" << product.name
                << " source=" << product.sourceRelativePath
                << " sourceGuid=" << Uuid::ToString(product.sourceShaderAssetId.value)
                << " properties=" << product.propertyCount
                << " keywordAxes=" << product.keywordAxisCount
                << " passes=" << product.passCount << '\n';
        }
        for (const ck::ModelCookProduct& product : products)
        {
            std::cout << "asset-cooker model="
                << Uuid::ToString(product.modelAssetId.value)
                << " artifact=" << product.artifactPath << '\n';
        }
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    Arguments arguments;
    std::string failure;
    if (!ParseArguments(argc, argv, arguments, failure))
    {
        if (failure.empty()) return 0;
        std::cerr << "asset-cooker error: " << failure << '\n';
        PrintUsage();
        return 2;
    }
    if (arguments.mode == Arguments::Mode::RefreshModelIdentities)
    {
        asset_cooker::ModelIdentityRefreshSummary summary;
        if (!asset_cooker::RefreshModelIdentities(arguments.assetRoot,
            arguments.models, summary, failure))
        {
            std::cerr << "asset-cooker error: " << failure << '\n';
            return 7;
        }
        std::cout << "asset-cooker identity-refresh models=" << summary.models
            << " materials=" << summary.materials
            << " embeddedTextures=" << summary.embeddedTextures << '\n';
        return 0;
    }
    return Run(arguments);
}
