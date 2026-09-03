#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/CookedModelCodec.h"
#include "Experiment/Cooked/MaterialCookProducer.h"
#include "Experiment/Cooked/ModelGenerationExportProducer.h" // MBC11: generation 내보내기
#include "Assets/AssetIdentityProfile.h"                       // MBC11: UUIDv8 source identity
#include "Experiment/Cooked/SceneCookProducer.h"
#include "Experiment/Cooked/ShaderMetaCookProducer.h"
#include "Experiment/Cooked/TextureCookProducer.h"
#include "Assets/AssetIdentityEpoch.h"
#include "Assets/ModelAssetAuthoringTransaction.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringNodeEquality.h"
#include "AuthoringParsedDocument.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <process.h>
#include <objbase.h> // MBC11: CoInitializeEx
#include <set>
#include <sstream>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace
{
    namespace ck = experiment::cooked;

    struct Arguments final
    {
        enum class Mode
        {
            Cook,
            AuthorModelAsset,
            IssueModelIdentityEpoch,
            CompileRuntimeDocuments,
        };

        Mode mode{ Mode::Cook };
        std::filesystem::path assetRoot{};
        std::filesystem::path outputRoot{};
        std::filesystem::path runtimeRoot{};
        // MBC11 — 게시된 generation 루트. 비우면 <asset-root>/../Library/ModelAssetGenerations.
        std::filesystem::path generationRoot{};
        std::vector<std::filesystem::path> models{};
        std::vector<std::filesystem::path> textures{};
        std::vector<std::filesystem::path> shaderMetas{};
        std::vector<std::filesystem::path> materials{};
        std::vector<std::filesystem::path> scenes{};
        std::string identityEpoch{};
        assets::ModelAuthoringFailurePoint modelAuthoringFailurePoint{};
    };

    void PrintUsage()
    {
        std::cout
            << "Usage: AssetCooker --asset-root <Assets> --output <new-dir> "
               "[--generation-root <Library/ModelAssetGenerations>] "
               "[--model <source> ...] [--texture <source> ...] [--shadermeta <source> ...] [--material <source> ...] [--scene <source> ...]\n"
            << "       (--model은 게시된 generation을 내보낸다 — 먼저 --author-model-asset)\n"
            << "       AssetCooker --author-model-asset "
               "--asset-root <Assets> --output <generation-root> --model <source>\n"
            << "       AssetCooker --issue-model-identity-epoch "
               "--asset-root <Assets> --identity-epoch <name>\n"
            << "       AssetCooker --compile-runtime-documents "
               "--runtime-root <package-input-root>\n";
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
            if (option == L"--author-model-asset")
            {
                if (out.mode != Arguments::Mode::Cook)
                {
                    failure = "AssetCooker mode는 하나만 지정할 수 있다.";
                    return false;
                }
                out.mode = Arguments::Mode::AuthorModelAsset;
                continue;
            }
            if (option == L"--compile-runtime-documents")
            {
                if (out.mode != Arguments::Mode::Cook)
                {
                    failure = "AssetCooker mode는 하나만 지정할 수 있다.";
                    return false;
                }
                out.mode = Arguments::Mode::CompileRuntimeDocuments;
                continue;
            }
            if (option == L"--issue-model-identity-epoch")
            {
                if (out.mode != Arguments::Mode::Cook)
                {
                    failure = "AssetCooker mode는 하나만 지정할 수 있다.";
                    return false;
                }
                out.mode = Arguments::Mode::IssueModelIdentityEpoch;
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
            else if (option == L"--runtime-root")
            {
                if (!out.runtimeRoot.empty())
                {
                    failure = "--runtime-root는 한 번만 지정할 수 있다.";
                    return false;
                }
                out.runtimeRoot = value;
            }
            else if (option == L"--generation-root")
            {
                if (!out.generationRoot.empty())
                {
                    failure = "--generation-root는 한 번만 지정할 수 있다.";
                    return false;
                }
                out.generationRoot = value;
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
            else if (option == L"--identity-epoch")
            {
                if (!out.identityEpoch.empty())
                {
                    failure = "--identity-epoch는 한 번만 지정할 수 있다.";
                    return false;
                }
                out.identityEpoch = value.string();
            }
            else if (option == L"--model-authoring-fail")
            {
                if (value == L"after-decode")
                    out.modelAuthoringFailurePoint = assets::ModelAuthoringFailurePoint::AfterDecode;
                else if (value == L"after-identity")
                    out.modelAuthoringFailurePoint = assets::ModelAuthoringFailurePoint::AfterIdentity;
                else if (value == L"after-stage-write")
                    out.modelAuthoringFailurePoint = assets::ModelAuthoringFailurePoint::AfterStageWrite;
                else if (value == L"after-stage-validation")
                    out.modelAuthoringFailurePoint = assets::ModelAuthoringFailurePoint::AfterStageValidation;
                else if (value == L"after-generation-publish")
                    out.modelAuthoringFailurePoint = assets::ModelAuthoringFailurePoint::AfterGenerationPublish;
                else
                {
                    failure = "알 수 없는 model authoring failure point다.";
                    return false;
                }
            }
            else
            {
                failure = "알 수 없는 option이다.";
                return false;
            }
        }

        if (out.mode == Arguments::Mode::CompileRuntimeDocuments)
        {
            if (out.runtimeRoot.empty())
            {
                failure = "runtime document compile에는 --runtime-root가 필요하다.";
                return false;
            }
            if (!out.assetRoot.empty() || !out.outputRoot.empty()
                || !out.models.empty() || !out.textures.empty()
                || !out.shaderMetas.empty() || !out.materials.empty()
                || !out.scenes.empty())
            {
                failure = "runtime document compile에는 cook/identity option을 섞을 수 없다.";
                return false;
            }
            return true;
        }

        if (out.mode == Arguments::Mode::IssueModelIdentityEpoch)
        {
            if (out.assetRoot.empty() || out.identityEpoch.empty())
            {
                failure = "identity epoch 발급에는 --asset-root와 --identity-epoch가 필요하다.";
                return false;
            }
            if (!out.outputRoot.empty() || !out.runtimeRoot.empty()
                || !out.models.empty() || !out.textures.empty()
                || !out.shaderMetas.empty() || !out.materials.empty()
                || !out.scenes.empty())
            {
                failure = "identity epoch 발급에는 cook/model/runtime option을 섞을 수 없다.";
                return false;
            }
            return true;
        }

        if (out.assetRoot.empty())
        {
            failure = "--asset-root가 필요하다.";
            return false;
        }
        if (!out.runtimeRoot.empty())
        {
            failure = "--runtime-root는 runtime document compile 전용이다.";
            return false;
        }
        if (out.mode == Arguments::Mode::AuthorModelAsset)
        {
            if (!out.textures.empty() || !out.shaderMetas.empty()
                || !out.materials.empty() || !out.scenes.empty())
            {
                failure = "model authoring에는 --texture/--shadermeta/--material/--scene을 지정할 수 없다.";
                return false;
            }
            if (out.models.size() != 1u)
            {
                failure = "model authoring에는 정확히 하나의 --model이 필요하다.";
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
        if ((out.mode == Arguments::Mode::Cook
            || out.mode == Arguments::Mode::AuthorModelAsset)
            && out.outputRoot.empty())
        {
            failure = "Cook/model authoring에는 --output이 필요하다.";
            return false;
        }
        if (!out.identityEpoch.empty())
        {
            failure = "--identity-epoch는 identity epoch 발급 전용이다.";
            return false;
        }
        if (out.mode != Arguments::Mode::AuthorModelAsset
            && out.modelAuthoringFailurePoint != assets::ModelAuthoringFailurePoint::None)
        {
            failure = "--model-authoring-fail은 model authoring 전용이다.";
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

    // MBC11 — generation 디렉터리 내보내기로 artifact 경로가 깊어졌다
    // (Derived/Models/xx/<id>/<gen>/textures/<id>.png). 패키지 부모가 길면 legacy 260자
    // 경계를 넘으므로 절대 경로가 길 때 `\\?\` 접두로 연다. 상대·짧은 경로는 그대로.
    [[nodiscard]] std::filesystem::path LongPath(const std::filesystem::path& path)
    {
        const std::wstring native = path.lexically_normal().native();
        if (!path.is_absolute() || native.size() < 240u
            || native.rfind(L"\\\\?\\", 0) == 0)
        {
            return path;
        }
        std::wstring prefixed = L"\\\\?\\" + native;
        for (wchar_t& c : prefixed) if (c == L'/') c = L'\\';
        return std::filesystem::path(prefixed);
    }

    [[nodiscard]] bool WriteBinaryFile(const std::filesystem::path& path,
        std::span<const std::byte> bytes, std::string& failure)
    {
        std::error_code error;
        std::filesystem::create_directories(LongPath(path.parent_path()), error);
        if (error)
        {
            failure = "산출물 디렉터리를 만들 수 없다: " + error.message();
            return false;
        }

        std::ofstream stream(LongPath(path), std::ios::binary | std::ios::trunc);
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
        std::ifstream stream(LongPath(path), std::ios::binary);
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

    [[nodiscard]] bool IsRuntimeTextDocument(
        const std::filesystem::path& root, const std::filesystem::path& path)
    {
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(path, root, error);
        if (error || relative.empty() || relative.is_absolute()) return false;
        const std::string virtualPath = relative.generic_string();
        const std::string extension = path.extension().string();
        if (virtualPath.starts_with("ProjectSetting/") && extension == ".asset")
            return true;
        if (!virtualPath.starts_with("Assets/")) return false;
        return extension == ".inputmap" || extension == ".bt"
            || extension == ".blackboard" || extension == ".volume"
            || extension == ".terrain" || extension == ".foliage";
    }

    [[nodiscard]] int CompileRuntimeDocuments(const Arguments& arguments)
    {
        std::error_code error;
        const std::filesystem::path root =
            std::filesystem::weakly_canonical(arguments.runtimeRoot, error);
        if (error || !std::filesystem::is_directory(root, error)
            || !std::filesystem::is_directory(root / "Assets", error)
            || !std::filesystem::is_directory(root / "ProjectSetting", error))
        {
            std::cerr << "asset-cooker error: runtime root가 package input tree가 아니다.\n";
            return 2;
        }

        std::vector<std::filesystem::path> paths;
        error.clear();
        for (const std::filesystem::directory_entry& item :
            std::filesystem::recursive_directory_iterator(root,
                std::filesystem::directory_options::skip_permission_denied, error))
        {
            if (error) break;
            if (item.is_regular_file(error) && !error
                && IsRuntimeTextDocument(root, item.path()))
            {
                paths.push_back(item.path());
            }
            error.clear();
        }
        if (error || paths.empty())
        {
            std::cerr << "asset-cooker error: runtime document corpus를 열거하지 못했거나 0개다.\n";
            return 3;
        }
        std::ranges::sort(paths);

        struct Encoded final
        {
            std::filesystem::path path;
            std::vector<std::byte> bytes;
        };
        std::vector<Encoded> encoded;
        encoded.reserve(paths.size());
        std::uint64_t totalBytes{};
        for (const std::filesystem::path& path : paths)
        {
            std::string parseError;
            const Authoring::ParsedDocument source =
                Authoring::ParsedDocument::ParseFile(path.string(), parseError);
            if (!source)
            {
                std::cerr << "asset-cooker error: runtime document parse 실패: "
                    << path << " (" << parseError << ")\n";
                return 3;
            }

            Encoded item;
            item.path = path;
            std::string encodeError;
            if (!Authoring::EncodeCookedDocument(
                source.Root(), item.bytes, encodeError))
            {
                std::cerr << "asset-cooker error: runtime document encode 실패: "
                    << path << " (" << encodeError << ")\n";
                return 3;
            }
            std::string decodeError;
            const auto restored = Authoring::DecodeCookedDocument(
                item.bytes, decodeError);
            if (!restored || !Authoring::NodesEqual(
                source.Root(), restored->Root()))
            {
                std::cerr << "asset-cooker error: runtime document parity 실패: "
                    << path << " (" << decodeError << ")\n";
                return 3;
            }
            totalBytes += item.bytes.size();
            encoded.push_back(std::move(item));
        }

        std::size_t index{};
        for (const Encoded& item : encoded)
        {
            std::filesystem::path temporary = item.path;
            temporary += ".cedo-tmp-" + std::to_string(_getpid())
                + "-" + std::to_string(index++);
            std::string writeError;
            if (!WriteBinaryFile(temporary, item.bytes, writeError))
            {
                std::cerr << "asset-cooker error: " << writeError << '\n';
                return 4;
            }
            if (!MoveFileExW(temporary.c_str(), item.path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                const DWORD moveError = GetLastError();
                std::filesystem::remove(temporary, error);
                std::cerr << "asset-cooker error: runtime document replace 실패: "
                    << item.path << " (win32=" << moveError << ")\n";
                return 4;
            }
        }

        std::cout << "asset-cooker runtime-documents=" << encoded.size()
            << " bytes=" << totalBytes
            << " format=CEDO" << Authoring::kCookedDocumentVersion << '\n';
        return 0;
    }

    [[nodiscard]] bool BuildSourceIdentityTable(
        const std::filesystem::path& assetRoot,
        std::vector<ck::AssetSourceManifestEntry>& outEntries,
        std::string& failure)
    {
        std::vector<std::filesystem::path> sidecars;
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(
            assetRoot, std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end;
        if (error)
        {
            failure = "source identity sidecar를 열거할 수 없다: " + error.message();
            return false;
        }
        for (; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                failure = "source identity sidecar 열거가 끊겼다: " + error.message();
                return false;
            }
            if (iterator->is_regular_file(error) && !error
                && iterator->path().extension() == ".meta")
            {
                sidecars.push_back(iterator->path());
            }
            error.clear();
        }
        std::ranges::sort(sidecars);

        std::vector<ck::AssetSourceManifestEntry> entries;
        entries.reserve(sidecars.size());
        for (const std::filesystem::path& sidecar : sidecars)
        {
            std::filesystem::path source = sidecar;
            source.replace_extension();
            if (!std::filesystem::is_regular_file(source, error) || error)
            {
                failure = "sidecar target source가 없다: " + source.string();
                return false;
            }

            const std::filesystem::path relative =
                std::filesystem::relative(source, assetRoot, error);
            if (error || relative.empty() || relative.is_absolute())
            {
                failure = "source path를 Assets root에 상대화할 수 없다: "
                    + source.string();
                return false;
            }
            const std::u8string relativeU8 = relative.generic_u8string();
            const std::string sourcePath(relativeU8.begin(), relativeU8.end());

            std::string parseError;
            const Authoring::ParsedDocument document =
                Authoring::ParsedDocument::ParseFile(sidecar.string(), parseError);
            if (!document)
            {
                failure = "source sidecar parse 실패: " + sidecar.string()
                    + " (" + parseError + ")";
                return false;
            }
            // MBC11 — 모델 sidecar(schema v2)는 최상위 `guid`가 없고 `assetId`(UUIDv8)를
            // 든다. 다른 자산은 여전히 최상위 `guid`(UUIDv4)다.
            const Authoring::ReadNode guidNode = document.Root()["guid"];
            const Authoring::ReadNode assetIdNode = document.Root()["assetId"];
            experiment::AssetId assetId;
            bool identified = false;
            if (guidNode && guidNode.IsScalar())
            {
                identified = experiment::TryParseCanonicalAssetId(guidNode.AsString(), assetId);
            }
            else if (assetIdNode && assetIdNode.IsScalar())
            {
                identified = assets::TryParseCanonicalUuidV8(assetIdNode.AsString(), assetId.value);
            }
            if (!identified)
            {
                failure = "source sidecar 신원이 canonical UUIDv4(guid)/UUIDv8(assetId)가 아니다: "
                    + sidecar.string();
                return false;
            }

            entries.push_back(ck::AssetSourceManifestEntry{
                assetId, sourcePath });
        }
        if (entries.empty())
        {
            failure = "source identity sidecar가 0개다.";
            return false;
        }

        outEntries = std::move(entries);
        failure.clear();
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

        // MBC11 — 모델은 굽지 않고 게시된 generation을 검증해 내보낸다.
        std::vector<ck::ModelGenerationExportProduct> products;
        products.reserve(arguments.models.size());
        error.clear();
        const std::filesystem::path generationRoot = arguments.generationRoot.empty()
            ? (assetRoot.parent_path() / "Library" / "ModelAssetGenerations")
            : std::filesystem::weakly_canonical(arguments.generationRoot, error);
        const std::filesystem::path identityHeaderPath =
            assetRoot.parent_path() / "ProjectSetting" / "AssetIdentity.asset";
        ck::CookedAssetManifest manifest;
        std::set<std::string> artifactPaths;
        std::size_t totalMaterials = 0u;
        std::size_t totalEmbeddedTextures = 0u;
        std::size_t totalTextureReferences = 0u;
        std::size_t totalExternalTextureReferences = 0u;
        std::size_t closureArtifactPaths = 0u;
        std::size_t closureSweptFiles = 0u;
        std::uint64_t totalEmbeddedTextureBytes = 0u;
        std::uint64_t totalArtifactBytes = 0u;

        for (const std::filesystem::path& model : arguments.models)
        {
            const std::filesystem::path modelSource = model.is_relative()
                ? assetRoot / model : model;
            ck::ModelGenerationExportResult result = ck::BuildModelGenerationExportProduct(
                { modelSource, assetRoot, generationRoot, identityHeaderPath });
            if (!result.Succeeded())
            {
                for (const ck::ModelGenerationExportIssue& issue : result.issues)
                {
                    std::cerr << "asset-cooker error: " << issue.context
                        << ": " << issue.message << '\n';
                }
                return 3;
            }

            ck::ModelGenerationExportProduct product = std::move(*result.product);
            for (const ck::ModelGenerationExportFile& file : product.files)
            {
                if (!artifactPaths.insert(file.artifactPath).second)
                {
                    std::cerr << "asset-cooker error: 중복 model artifact path다: "
                        << file.artifactPath << '\n';
                    return 3;
                }
            }
            totalMaterials += product.materialCount;
            totalEmbeddedTextures += product.embeddedTextureCount;
            totalTextureReferences += product.embeddedTextureCount;
            totalEmbeddedTextureBytes += product.embeddedTextureBytes;
            totalArtifactBytes += product.artifactBytes;
            manifest.entries.push_back(product.manifestEntry);
            for (const ck::CookedAssetManifestEntry& entry : product.subAssetEntries)
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

        std::string sourceIdentityFailure;
        if (!BuildSourceIdentityTable(assetRoot, manifest.sourceAssets,
            sourceIdentityFailure))
        {
            std::cerr << "asset-cooker error: " << sourceIdentityFailure << '\n';
            return 3;
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
        std::filesystem::create_directories(LongPath(stagingRoot), error);
        if (error)
        {
            std::cerr << "asset-cooker error: staging 디렉터리를 만들 수 없다: "
                << error.message() << '\n';
            return 5;
        }
        StagingCleanup cleanup(stagingRoot);

        std::string failure;
        for (const ck::ModelGenerationExportProduct& product : products)
        {
            // generation 파일 전부를 바이트 동일하게 옮기고 다시 읽어 대조한다.
            for (const ck::ModelGenerationExportFile& file : product.files)
            {
                const std::filesystem::path artifactFile =
                    stagingRoot / std::filesystem::path(file.artifactPath);
                if (!WriteBinaryFile(artifactFile, file.bytes, failure))
                {
                    std::cerr << "asset-cooker error: " << failure << '\n';
                    return 5;
                }
                std::vector<std::byte> persisted;
                if (!ReadBinaryFile(artifactFile, persisted, failure)
                    || persisted != file.bytes)
                {
                    std::cerr << "asset-cooker error: 게시 전 generation 파일 재검증이 "
                        "실패했다: " << file.artifactPath << '\n';
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
            || restoredManifest.entries.size() != manifest.entries.size()
            || restoredManifest.sourceAssets.size() != manifest.sourceAssets.size())
        {
            std::cerr << "asset-cooker error: 게시 전 CEMF 재검증이 실패했다.\n";
            return 5;
        }

        for (const ck::ModelGenerationExportProduct& product : products)
        {
            const ck::CookedAssetManifestEntry* modelEntry =
                restoredManifest.Find(product.modelAssetId);
            const auto record = std::ranges::find(product.files, product.recordArtifactPath,
                &ck::ModelGenerationExportFile::artifactPath);
            ck::Sha256Digest digest{};
            std::string hashError;
            manifestIssues.clear();
            if (!modelEntry || record == product.files.end()
                || modelEntry->artifactPath != product.recordArtifactPath
                || !ck::ComputeSha256(record->bytes, digest, hashError)
                || !ck::VerifyArtifact(*modelEntry,
                    record->bytes.size(), digest, manifestIssues))
            {
                std::cerr << "asset-cooker error: manifest artifact 검증이 실패했다.\n";
                return 5;
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

        // ── 최종 폐포 스윕 (D5-b2c-5) ──────────────────────────────────
        //
        // 여기까지의 검증은 전부 **product 에서 출발한다** — "내가 만든 것이
        // manifest 에 있는가". 그 방향만으로는 두 가지를 못 본다:
        //
        //   1. manifest 가 이름 붙였는데 **디스크에 없는** artifact
        //   2. 디스크에 있는데 **manifest 가 모르는** artifact(orphan)
        //
        // 그래서 반대 방향으로 한 번 더 훑는다. manifest 를 정본으로 삼고
        // staging tree 전체와 맞춘다. 이게 pak 에 실리기 직전의 마지막 문이다.
        {
            std::set<std::string> namedPaths;
            // MBC11 — 모델 generation 레코드(kind Model, generation.asset)가 이름 붙인
            // 디렉터리 안의 파일(model.cemc·sidecar.meta·textures/*)은 레코드의 폐포다.
            // 레코드 리더가 파일 지문으로 검증하므로 manifest가 따로 이름 붙이지 않는다.
            std::vector<std::string> closurePrefixes;
            for (const ck::CookedAssetManifestEntry& entry
                : restoredManifest.entries)
            {
                namedPaths.insert(entry.artifactPath);
                if (entry.kind == ck::CookedAssetKind::Model
                    && entry.artifactPath.ends_with("/generation.asset"))
                {
                    closurePrefixes.push_back(entry.artifactPath.substr(0,
                        entry.artifactPath.size() - std::string("generation.asset").size()));
                }

                // dependency 는 writer 가 이미 해소를 강제하지만, 여기서는
                // **읽어 온 manifest** 로 다시 본다 — 기록과 판독 사이에
                // 어긋남이 있으면 그것도 결함이다.
                for (const experiment::AssetId& dependency : entry.dependencies)
                {
                    if (restoredManifest.Find(dependency)) continue;
                    std::cerr << "asset-cooker error: 폐포가 닫히지 않았다 — "
                        << entry.artifactPath << " 의 dependency "
                        << Uuid::ToString(dependency.value)
                        << " 가 manifest 에 없다.\n";
                    return 5;
                }
            }

            // manifest 가 이름 붙인 artifact 는 전부 실재하고, 크기·해시가 맞아야 한다.
            for (const ck::CookedAssetManifestEntry& entry
                : restoredManifest.entries)
            {
                const std::filesystem::path artifactFile =
                    stagingRoot / std::filesystem::path(entry.artifactPath);
                std::vector<std::byte> bytes;
                if (!ReadBinaryFile(artifactFile, bytes, failure))
                {
                    std::cerr << "asset-cooker error: manifest 가 이름 붙인 "
                        "artifact 가 없다: " << entry.artifactPath << '\n';
                    return 5;
                }
                ck::Sha256Digest digest{};
                std::string hashError;
                manifestIssues.clear();
                if (!ck::ComputeSha256(bytes, digest, hashError)
                    || !ck::VerifyArtifact(entry, bytes.size(), digest,
                        manifestIssues))
                {
                    std::cerr << "asset-cooker error: stale artifact — "
                        "디스크 내용이 manifest 와 다르다: "
                        << entry.artifactPath << '\n';
                    return 5;
                }
            }

            // 디스크에 있는 것 중 manifest 가 모르는 파일은 없어야 한다.
            std::size_t sweptFiles = 0u;
            std::size_t closureFiles = 0u;
            error.clear();
            const std::filesystem::path sweepRoot = LongPath(stagingRoot);
            for (const std::filesystem::directory_entry& item :
                std::filesystem::recursive_directory_iterator(sweepRoot, error))
            {
                if (!item.is_regular_file()) continue;
                ++sweptFiles;
                const std::filesystem::path relative =
                    item.path().lexically_relative(sweepRoot);
                if (relative.empty())
                {
                    std::cerr << "asset-cooker error: staging tree 를 훑지 못했다.\n";
                    return 5;
                }
                const std::string virtualPath = relative.generic_string();
                if (virtualPath == "Derived/asset-manifest.cemf") continue;
                if (namedPaths.contains(virtualPath)) continue;
                bool inClosure = false;
                for (const std::string& prefix : closurePrefixes)
                {
                    if (virtualPath.rfind(prefix, 0) == 0) { inClosure = true; break; }
                }
                if (inClosure) { ++closureFiles; continue; }

                std::cerr << "asset-cooker error: manifest 가 모르는 artifact 가 "
                    "staging tree 에 있다: " << virtualPath << '\n';
                return 5;
            }
            if (error)
            {
                std::cerr << "asset-cooker error: staging tree 순회가 실패했다.\n";
                return 5;
            }

            // 파일 수 = 서로 다른 artifact 경로 + manifest 하나.
            // material 처럼 model artifact 를 공유하는 subasset 이 있으므로
            // entry 수가 아니라 **경로 수**와 비교한다.
            if (sweptFiles != namedPaths.size() + closureFiles + 1u)
            {
                std::cerr << "asset-cooker error: staging 파일 수가 맞지 않는다: "
                    << sweptFiles << " != " << (namedPaths.size() + closureFiles + 1u) << '\n';
                return 5;
            }
            closureArtifactPaths = namedPaths.size();
            closureSweptFiles = sweptFiles;
        }

        error.clear();
        std::filesystem::rename(LongPath(stagingRoot), LongPath(outputRoot), error);
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
            << " artifactPaths=" << closureArtifactPaths
            << " files=" << closureSweptFiles
            << " manifestEntries=" << manifest.entries.size()
            << " sourceIdentities=" << manifest.sourceAssets.size()
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
        for (const ck::ModelGenerationExportProduct& product : products)
        {
            std::cout << "asset-cooker model="
                << Uuid::ToString(product.modelAssetId.value)
                << " generation=" << product.generation
                << " files=" << product.files.size()
                << " artifact=" << product.recordArtifactPath << '\n';
        }
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    // MBC11 — generation 검증(LoadModelAssetGeneration)이 embedded texture를 WIC로
    // 디코드한다. 에디터는 부팅이 COM을 올리지만 cooker는 여기서 올린다.
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope { HRESULT hr; ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); } } comScope{ comInit };
    Arguments arguments;
    std::string failure;
    if (!ParseArguments(argc, argv, arguments, failure))
    {
        if (failure.empty()) return 0;
        std::cerr << "asset-cooker error: " << failure << '\n';
        PrintUsage();
        return 2;
    }
    if (arguments.mode == Arguments::Mode::AuthorModelAsset)
    {
        const std::filesystem::path source = arguments.models.front().is_relative()
            ? arguments.assetRoot / arguments.models.front()
            : arguments.models.front();
        assets::ModelAssetAuthoringRequest request;
        request.assetRoot = arguments.assetRoot;
        request.sourcePath = source;
        request.identityHeaderPath = arguments.assetRoot.parent_path()
            / "ProjectSetting" / "AssetIdentity.asset";
        request.generationRoot = arguments.outputRoot;
        request.failurePoint = arguments.modelAuthoringFailurePoint;
        const assets::ModelAssetAuthoringResult result =
            assets::AuthorModelAsset(request);
        if (!result.Succeeded())
        {
            for (const assets::ModelAssetAuthoringIssue& issue : result.issues)
                std::cerr << "asset-cooker error [" << issue.stage << "]: "
                    << issue.message << '\n';
            return 7;
        }
        std::cout << "asset-cooker model-authoring model="
            << Uuid::ToString(result.modelAssetId)
            << " generation=" << result.generation
            << " subAssets=" << result.subAssetCount
            << " materials=" << result.materialCount
            << " embeddedTextures=" << result.embeddedTextureCount
            << " published=" << result.generationPath.generic_string()
            << '\n';
        return 0;
    }
    if (arguments.mode == Arguments::Mode::IssueModelIdentityEpoch)
    {
        const std::filesystem::path headerPath = arguments.assetRoot.parent_path()
            / "ProjectSetting" / assets::kIdentityEpochHeaderFileName;
        if (!assets::IssueIdentityEpochHeader(headerPath,
            arguments.identityEpoch, failure))
        {
            std::cerr << "asset-cooker error: " << failure << '\n';
            return 8;
        }
        std::cout << "asset-cooker identity-epoch issued="
            << arguments.identityEpoch << " header="
            << headerPath.generic_string() << '\n';
        return 0;
    }
    if (arguments.mode == Arguments::Mode::CompileRuntimeDocuments)
        return CompileRuntimeDocuments(arguments);
    return Run(arguments);
}
