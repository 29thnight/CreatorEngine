#include "ModelCookProducer.h"

#include "CookSupport.h"
#include "CookedModelCodec.h"
#include "CookedModelFormat.h"
#include "ModelCookIdentity.h"
#include "../Import/ImporterModelDecoder.h"

#include <algorithm>
#include <fstream>
#include <ranges>
#include <string_view>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        namespace im = experiment::importer;

        void AddIssue(ModelCookProductResult& result,
            std::string context, std::string message)
        {
            result.issues.push_back({ std::move(context), std::move(message) });
        }

        // ★ ReadTextFile·IsContainedPath·ReadMetaAssetId 는 CookSupport 로
        //   옮겼다. texture/ShaderMeta/scene producer 가 같은 것을 필요로 하는데,
        //   TU 마다 익명 namespace 에 복제하면 MSVC 유니티 빌드가 그 익명
        //   namespace 들을 합치면서 곧바로 재정의가 된다.

        [[nodiscard]] std::vector<std::string> IdentityKeys(
            const std::vector<ModelSubAssetIdentity>& identities)
        {
            std::vector<std::string> keys;
            keys.reserve(identities.size());
            for (const ModelSubAssetIdentity& identity : identities)
                keys.push_back(identity.sourceKey);
            std::ranges::sort(keys);
            return keys;
        }

        void AddUniqueKey(std::vector<std::string>& keys, std::string_view key)
        {
            if (std::ranges::find(keys, key) == keys.end())
                keys.emplace_back(key);
        }

        [[nodiscard]] bool SameKeys(std::vector<std::string> imported,
            const std::vector<ModelSubAssetIdentity>& identities)
        {
            std::ranges::sort(imported);
            return imported == IdentityKeys(identities);
        }
    }

    ModelCookProductResult BuildModelCookProduct(
        const ModelCookProductRequest& request)
    {
        ModelCookProductResult result;
        std::error_code error;

        const std::filesystem::path assetRoot =
            std::filesystem::weakly_canonical(request.assetRoot, error);
        if (error || assetRoot.empty()
            || !std::filesystem::is_directory(assetRoot, error))
        {
            AddIssue(result, "request.assetRoot", "asset root가 유효한 디렉터리가 아니다.");
            return result;
        }

        error.clear();
        const std::filesystem::path source =
            std::filesystem::weakly_canonical(request.sourcePath, error);
        if (error || source.empty()
            || !std::filesystem::is_regular_file(source, error))
        {
            AddIssue(result, "request.sourcePath", "source model이 유효한 파일이 아니다.");
            return result;
        }
        if (!IsContainedPath(assetRoot, source))
        {
            AddIssue(result, "request.sourcePath", "source model이 asset root 밖에 있다.");
            return result;
        }

        error.clear();
        const std::filesystem::path logicalSourcePath =
            std::filesystem::relative(source, assetRoot, error).lexically_normal();
        if (error || logicalSourcePath.empty() || logicalSourcePath.is_absolute())
        {
            AddIssue(result, "request.sourcePath",
                "source model의 asset-relative 논리 경로를 만들지 못했다.");
            return result;
        }
        for (const std::filesystem::path& part : logicalSourcePath)
        {
            if (part == "..")
            {
                AddIssue(result, "request.sourcePath",
                    "source model의 논리 경로가 asset root를 벗어난다.");
                return result;
            }
        }

        std::filesystem::path modelMetaPath = source;
        modelMetaPath += ".meta";
        std::string modelMetaYaml;
        if (!ReadTextFile(modelMetaPath, modelMetaYaml))
        {
            AddIssue(result, "model.meta", "model sidecar를 읽을 수 없다: "
                + modelMetaPath.string());
            return result;
        }

        ModelCookIdentity modelIdentity;
        std::vector<ModelIdentityIssue> identityIssues;
        if (!ReadModelCookIdentity(modelMetaYaml, modelIdentity, identityIssues))
        {
            for (const ModelIdentityIssue& issue : identityIssues)
                AddIssue(result, issue.context, issue.message);
            if (identityIssues.empty())
                AddIssue(result, "model.meta", "model identity를 읽지 못했다.");
            return result;
        }

        AssetId gbufferShaderId{};
        AssetId forwardShaderId{};
        std::string identityFailure;
        if (!ReadMetaAssetId(assetRoot /
            "Shaders/DefaultPassShader/GBuffer.shadermeta.meta",
            gbufferShaderId, identityFailure))
        {
            AddIssue(result, "shader.gbuffer", std::move(identityFailure));
            return result;
        }
        if (!ReadMetaAssetId(assetRoot /
            "Shaders/DefaultPassShader/Forward.shadermeta.meta",
            forwardShaderId, identityFailure))
        {
            AddIssue(result, "shader.forward", std::move(identityFailure));
            return result;
        }

        std::vector<std::string> materialSourceKeys;
        std::vector<std::string> embeddedTextureSourceKeys;
        std::vector<std::string> resolutionFailures;
        std::size_t textureReferences = 0u;

        im::ImporterDecoderOptions decoderOptions{};
        decoderOptions.conversion.modelAssetId = modelIdentity.modelAssetId;
        decoderOptions.conversion.resolveMaterialAsset =
            [&](const im::ImportedMaterial& material, std::size_t)
            {
                AddUniqueKey(materialSourceKeys, material.sourceKey);
                return modelIdentity.FindMaterial(material.sourceKey);
            };
        decoderOptions.conversion.resolveShaderAsset =
            [&](const im::ImportedMaterial& material, std::size_t)
            {
                return material.alphaMode == im::AlphaMode::Blend
                    ? forwardShaderId : gbufferShaderId;
            };
        decoderOptions.conversion.resolveTextureAsset =
            [&](const im::ImportedTexture& texture)
            {
                ++textureReferences;
                if (texture.IsEmbedded())
                {
                    AddUniqueKey(embeddedTextureSourceKeys, texture.sourceKey);
                    return modelIdentity.FindEmbeddedTexture(texture.sourceKey);
                }

                std::error_code textureError;
                const std::filesystem::path textureSource =
                    std::filesystem::weakly_canonical(texture.sourcePath,
                        textureError);
                if (textureError || textureSource.empty()
                    || !IsContainedPath(assetRoot, textureSource))
                {
                    resolutionFailures.push_back(
                        "외부 texture가 asset root 밖에 있다: "
                        + texture.sourcePath.string());
                    return AssetId{};
                }

                std::filesystem::path textureMetaPath = textureSource;
                textureMetaPath += ".meta";
                AssetId id{};
                std::string failure;
                if (!ReadMetaAssetId(textureMetaPath, id, failure))
                    resolutionFailures.push_back(std::move(failure));
                return id;
            };

        im::ImporterModelDecoder decoder(std::move(decoderOptions));
        ModelLoadRequest loadRequest{};
        loadRequest.sourcePath = source;
        loadRequest.sourcePreference = ModelSourcePreference::SourceOnly;
        ModelDecodeResult decoded = decoder.Decode(loadRequest);
        if (!decoded.draft.has_value())
        {
            for (const ModelLoadIssue& issue : decoded.issues)
                AddIssue(result, issue.context, issue.message);
            if (decoded.issues.empty())
                AddIssue(result, "model.import", "source model import가 실패했다.");
            return result;
        }

        for (std::string& failure : resolutionFailures)
            AddIssue(result, "texture.meta", std::move(failure));
        if (!SameKeys(materialSourceKeys, modelIdentity.materials))
        {
            AddIssue(result, "model.meta.subAssets.materials",
                "import 결과와 material source key 집합이 정확히 일치하지 않는다.");
        }
        if (!SameKeys(embeddedTextureSourceKeys,
            modelIdentity.embeddedTextures))
        {
            AddIssue(result, "model.meta.subAssets.embeddedTextures",
                "import 결과와 embedded texture source key 집합이 정확히 일치하지 않는다.");
        }
        if (!result.issues.empty()) return result;

        ModelDraft draft = std::move(*decoded.draft);
        // Production cook artifact에는 입력의 물리 위치와 mtime을 넣지 않는다.
        // 같은 Assets tree를 어느 staging 경로에 놓아도 동일한 CEMC가
        // 나와야 하므로 provenance는 asset-relative 논리 경로로만 보존한다.
        draft.metadata.sourcePath = logicalSourcePath;
        draft.metadata.cookedPath.clear();
        draft.metadata.sourceWriteTime = {};
        const CookedWriteResult write = Write(draft);
        if (!write.Succeeded())
        {
            for (const ModelLoadIssue& issue : write.issues)
                AddIssue(result, issue.context, issue.message);
            if (write.issues.empty())
                AddIssue(result, "model.write", "checked CEMC writer가 산출물을 만들지 못했다.");
            return result;
        }

        Sha256Digest digest{};
        std::string hashError;
        if (!ComputeSha256(write.bytes, digest, hashError))
        {
            AddIssue(result, "model.sha256", std::move(hashError));
            return result;
        }

        ModelCookProduct product;
        product.modelAssetId = modelIdentity.modelAssetId;
        product.artifactPath = MakeDerivedModelArtifactPath(product.modelAssetId);
        product.artifactBytes = write.bytes;
        product.materialCount = draft.materials.size();
        product.embeddedTextureCount = embeddedTextureSourceKeys.size();
        product.textureReferenceCount = textureReferences;
        if (product.artifactPath.empty())
        {
            AddIssue(result, "model.artifactPath", "model GUID가 Derived 경로를 만들지 못했다.");
            return result;
        }

        CookedAssetManifestEntry modelEntry;
        modelEntry.assetId = product.modelAssetId;
        modelEntry.kind = CookedAssetKind::Model;
        modelEntry.formatVersion = kFormatVersion;
        modelEntry.byteSize = product.artifactBytes.size();
        modelEntry.contentSha256 = digest;
        modelEntry.artifactPath = product.artifactPath;
        for (const Material& material : draft.materials)
            modelEntry.dependencies.push_back(material.assetId);
        product.manifestEntries.push_back(std::move(modelEntry));

        // D5-b2a에서 material은 model CEMC 안의 subasset이다. shader/texture
        // producer entry가 생기기 전에는 해소 불가능한 dependency를 쓰지 않는다.
        for (const Material& material : draft.materials)
        {
            CookedAssetManifestEntry materialEntry;
            materialEntry.assetId = material.assetId;
            materialEntry.kind = CookedAssetKind::Material;
            materialEntry.formatVersion = kFormatVersion;
            materialEntry.byteSize = product.artifactBytes.size();
            materialEntry.contentSha256 = digest;
            materialEntry.artifactPath = product.artifactPath;
            product.manifestEntries.push_back(std::move(materialEntry));
        }

        result.product = std::move(product);
        return result;
    }
}
