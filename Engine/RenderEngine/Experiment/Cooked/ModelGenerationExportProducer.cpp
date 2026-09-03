#include "ModelGenerationExportProducer.h"

#include "../../Assets/ModelAssetGeneration.h"
#include "../../Assets/ModelSidecarV2.h"

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

        std::vector<std::byte> recordBytes;
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
            product.artifactBytes += exported.bytes.size();
            if (relative.generic_string().rfind("textures/", 0) == 0)
                product.embeddedTextureBytes += exported.bytes.size();
            if (relative == std::filesystem::path("generation.asset"))
            {
                recordBytes = exported.bytes;
                product.recordArtifactPath = exported.artifactPath;
            }
            product.files.push_back(std::move(exported));
        }
        if (product.recordArtifactPath.empty())
        {
            AddIssue(result, "generation.asset", "generation record가 없다.");
            return result;
        }

        Sha256Digest digest{};
        std::string hashError;
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

        for (const assets::ModelMaterialAsset& material : loaded.generation->Materials())
        {
            CookedAssetManifestEntry entry = product.manifestEntry;
            entry.assetId = AssetId{ material.materialId };
            entry.kind = CookedAssetKind::Material;
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
            const std::string textureArtifact =
                prefix + "textures/" + Uuid::ToString(texture.textureId) + ".png";
            const auto file = std::find_if(product.files.begin(), product.files.end(),
                [&textureArtifact](const ModelGenerationExportFile& candidate)
                { return candidate.artifactPath == textureArtifact; });
            if (file == product.files.end())
            {
                AddIssue(result, "generation.textures",
                    "generation에 embedded texture 파일이 없다: " + textureArtifact);
                return result;
            }
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
