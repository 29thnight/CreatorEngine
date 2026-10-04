#include "ModelGenerationExportProducer.h"

#include "../../Assets/ModelAssetGeneration.h"
#include "../../Assets/ModelSidecarV2.h"
#include "../../Assets/ModelMaterialGraph.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringParsedDocument.h"
#include "Sha256.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace experiment::cooked
{
    namespace
    {
        void AddIssue(ModelGenerationExportResult& result, std::string context,
            std::string message)
        {
            result.issues.push_back({ std::move(context), std::move(message) });
        }

        [[nodiscard]] bool ReadFileText(const std::filesystem::path& path, std::string& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
            return true;
        }

        [[nodiscard]] bool ReadFileBytes(const std::filesystem::path& path,
            std::vector<std::byte>& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            std::vector<char> buffer((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
            out.resize(buffer.size());
            std::transform(buffer.begin(), buffer.end(), out.begin(),
                [](char value) { return static_cast<std::byte>(value); });
            return true;
        }
    }

    ModelGenerationExportResult BuildModelGenerationExportProduct(
        const ModelGenerationExportRequest& request)
    {
        ModelGenerationExportResult result;
        std::error_code error;
        const std::filesystem::path source =
            std::filesystem::weakly_canonical(request.sourcePath, error);
        if (error || !std::filesystem::is_regular_file(source, error))
        {
            AddIssue(result, "request.sourcePath", "source model이 없다: "
                + request.sourcePath.string());
            return result;
        }

        std::filesystem::path sidecarPath = source;
        sidecarPath += ".meta";
        std::string sidecarText;
        if (!ReadFileText(sidecarPath, sidecarText))
        {
            AddIssue(result, "model.meta", "model sidecar를 읽을 수 없다: "
                + sidecarPath.string());
            return result;
        }
        assets::ModelSidecarV2 sidecar;
        std::vector<assets::SidecarIssue> sidecarIssues;
        if (!assets::ReadModelSidecarV2(sidecarText, sidecar, sidecarIssues))
        {
            for (const assets::SidecarIssue& issue : sidecarIssues)
                AddIssue(result, "model.meta." + issue.context, issue.message);
            if (sidecarIssues.empty())
                AddIssue(result, "model.meta", "schema v2 sidecar가 아니다.");
            return result;
        }

        const std::filesystem::path generationPath = request.generationRoot
            / Uuid::ToString(sidecar.assetId) / std::to_string(sidecar.generation);
        if (!std::filesystem::is_directory(generationPath, error) || error)
        {
            AddIssue(result, "generation", "게시된 generation이 없다(--author-model-asset "
                "먼저): " + generationPath.generic_string());
            return result;
        }

        // 게시된 generation을 런타임과 같은 리더로 검증한다 — 검증 실패는 cook 실패다.
        assets::ModelAssetGenerationLoadRequest load;
        load.identityHeaderPath = request.identityHeaderPath;
        load.generationRoot = request.generationRoot;
        load.generationPath = generationPath;
        load.canonicalSidecarPath = sidecarPath;
        load.expectedModelId = sidecar.assetId;
        load.expectedGeneration = sidecar.generation;
        const assets::ModelAssetGenerationLoadResult loaded =
            assets::LoadModelAssetGeneration(load);
        if (!loaded.Succeeded())
        {
            for (const auto& issue : loaded.issues)
                AddIssue(result, "generation." + issue.context, issue.message);
            if (loaded.issues.empty())
                AddIssue(result, "generation", "generation 검증이 실패했다.");
            return result;
        }

        ModelGenerationExportProduct product;
        product.modelAssetId = AssetId{ sidecar.assetId };
        product.generation = sidecar.generation;
        product.materialCount = loaded.generation->Materials().size();
        product.embeddedTextureCount = loaded.generation->Textures().size();
        product.meshCount = loaded.generation->Meshes().size();

        const std::string idText = Uuid::ToString(sidecar.assetId);
        const std::string prefix = "Derived/Models/" + idText.substr(0, 2) + "/" + idText
            + "/" + std::to_string(sidecar.generation) + "/";

        std::vector<std::filesystem::path> files;
        for (std::filesystem::recursive_directory_iterator it(generationPath, error), end;
            !error && it != end; it.increment(error))
        {
            if (it->is_regular_file(error)) files.push_back(it->path());
        }
        if (error)
        {
            AddIssue(result, "generation", "generation 디렉터리를 열거하지 못했다: "
                + error.message());
            return result;
        }
        std::ranges::sort(files);

        for (const std::filesystem::path& file : files)
        {
            const std::filesystem::path relative = file.lexically_relative(generationPath);
            ModelGenerationExportFile exported;
            exported.artifactPath = prefix + relative.generic_string();
            if (!ReadFileBytes(file, exported.bytes))
            {
                AddIssue(result, "generation.file", "generation 파일을 읽지 못했다: "
                    + file.string());
                return result;
            }
            if (relative.generic_string().rfind("textures/", 0) == 0)
                product.embeddedTextureBytes += exported.bytes.size();
            if (relative == std::filesystem::path("generation.asset"))
            {
                product.recordArtifactPath = exported.artifactPath;
            }
            product.files.push_back(std::move(exported));
        }
        if (product.recordArtifactPath.empty())
        {
            AddIssue(result, "generation.asset", "generation record가 없다.");
            return result;
        }

        // The library keeps human-readable authoring records. The packaged
        // Player needs the same validated fields without a runtime text parse.
        auto findFile = [&product](std::string_view suffix)
        {
            return std::ranges::find_if(product.files, [suffix](const auto& file)
            { return file.artifactPath.ends_with(suffix); });
        };
        auto record = findFile("/generation.asset");
        auto generationSidecar = findFile("/sidecar.meta");
        if (record == product.files.end() || generationSidecar == product.files.end())
        {
            AddIssue(result, "generation", "generation record 또는 sidecar가 없다.");
            return result;
        }
        auto encode = [&result](std::string_view text, std::vector<std::byte>& out,
            const char* context)
        {
            std::string error;
            const auto document = Authoring::ParsedDocument::ParseText(std::string(text), error);
            if (!document || !Authoring::EncodeCookedDocument(document.Root(), out, error))
            {
                AddIssue(result, context, "CEDO 변환 실패: " + error);
                return false;
            }
            return true;
        };
        const std::string sidecarSource{
            reinterpret_cast<const char*>(generationSidecar->bytes.data()),
            generationSidecar->bytes.size() };
        if (!encode(sidecarSource, generationSidecar->bytes, "sidecar.meta")) return result;
        Sha256Digest sidecarDigest{};
        std::string hashError;
        if (!ComputeSha256(generationSidecar->bytes, sidecarDigest, hashError))
        {
            AddIssue(result, "sidecar.meta", "SHA-256 계산 실패: " + hashError);
            return result;
        }
        std::string recordSource{
            reinterpret_cast<const char*>(record->bytes.data()), record->bytes.size() };
        constexpr std::string_view key = "sidecarFingerprint: ";
        const std::size_t field = recordSource.find(key);
        const std::size_t value = field == std::string::npos ? field : field + key.size();
        const std::size_t end = field == std::string::npos ? field
            : recordSource.find_first_of("\r\n", value);
        if (field == std::string::npos || end == std::string::npos
            || recordSource.find(key, end) != std::string::npos)
        {
            AddIssue(result, "generation.asset", "sidecarFingerprint 필드가 유일하지 않다.");
            return result;
        }
        recordSource.replace(value, end - value, "sha256:" + Hash::ToHex(sidecarDigest));
        if (!encode(recordSource, record->bytes, "generation.asset")) return result;

        product.artifactBytes = 0;
        for (const auto& file : product.files) product.artifactBytes += file.bytes.size();
        const std::vector<std::byte>& recordBytes = record->bytes;

        Sha256Digest digest{};
        if (!ComputeSha256(recordBytes, digest, hashError))
        {
            AddIssue(result, "generation.asset", "SHA-256 계산 실패: " + hashError);
            return result;
        }
        product.manifestEntry.assetId = product.modelAssetId;
        product.manifestEntry.kind = CookedAssetKind::Model;
        product.manifestEntry.formatVersion = 1u; // generation record schemaVersion
        product.manifestEntry.byteSize = recordBytes.size();
        product.manifestEntry.contentSha256 = digest;
        product.manifestEntry.artifactPath = product.recordArtifactPath;

        for (const auto& material : loaded.generation->Materials())
        {
            const auto graphPath = assets::ModelMaterialGraphPath(request.assetRoot, sidecar.assetId, material.materialId);
            if (!assets::ModelMaterialGraphIdentityMatches(graphPath, assets::ModelMaterialGraphId(material.materialId)))
            {
                AddIssue(result, "material.graph", "Model material source graph is missing; author the model first: " + graphPath.string());
                return result;
            }
            product.manifestEntry.dependencies.push_back(AssetId{assets::ModelMaterialGraphId(material.materialId)});
        }

        for (const assets::ModelMaterialAsset& material : loaded.generation->Materials())
        {
            CookedAssetManifestEntry entry = product.manifestEntry;
            entry.assetId = AssetId{ material.materialId };
            entry.kind = CookedAssetKind::Material;
            entry.dependencies = { AssetId{assets::ModelMaterialGraphId(material.materialId)} };
            product.subAssetEntries.push_back(std::move(entry));
        }
        // 메시 subasset — 씬이 MeshRenderer::m_meshAssetId(UUIDv8 MeshId)로 참조한다(MBC7).
        // manifest에 Mesh kind가 없으므로 generation record를 가리키는 Model kind로 둔다:
        // 해석 결과(record 경로)는 모델과 같고, 신원만 다르다.
        for (const assets::ModelMeshAsset& mesh : loaded.generation->Meshes())
        {
            CookedAssetManifestEntry entry = product.manifestEntry;
            entry.assetId = AssetId{ mesh.meshId };
            product.subAssetEntries.push_back(std::move(entry));
        }
        for (const assets::ModelTextureAsset& texture : loaded.generation->Textures())
        {
            const std::filesystem::path textureDirectory = prefix + "textures";
            const std::string textureStem = Uuid::ToString(texture.textureId);
            const auto matchesTexture = [&](const ModelGenerationExportFile& candidate) {
                const std::filesystem::path path(candidate.artifactPath);
                return path.parent_path() == textureDirectory && path.stem().string() == textureStem;
            };
            const auto file = std::ranges::find_if(product.files, matchesTexture);
            if (file == product.files.end())
            {
                AddIssue(result, "generation.textures",
                    "generation에 embedded texture 파일이 없다: " + textureStem);
                return result;
            }
            if (std::ranges::count_if(product.files, matchesTexture) != 1)
            {
                AddIssue(result, "generation.textures", "Embedded texture identity has ambiguous payload files: " + textureStem);
                return result;
            }

            const std::string& textureArtifact = file->artifactPath;
            Sha256Digest textureDigest{};
            if (!ComputeSha256(file->bytes, textureDigest, hashError))
            {
                AddIssue(result, "generation.textures", "SHA-256 계산 실패: " + hashError);
                return result;
            }
            CookedAssetManifestEntry entry;
            entry.assetId = AssetId{ texture.textureId };
            entry.kind = CookedAssetKind::Texture;
            entry.formatVersion = kTextureArtifactVersion;
            entry.byteSize = file->bytes.size();
            entry.contentSha256 = textureDigest;
            entry.artifactPath = textureArtifact;
            product.subAssetEntries.push_back(std::move(entry));
        }

        result.product = std::move(product);
        return result;
    }
}
