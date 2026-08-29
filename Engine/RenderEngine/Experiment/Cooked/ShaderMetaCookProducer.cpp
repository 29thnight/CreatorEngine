#include "ShaderMetaCookProducer.h"

#include "CookSupport.h"
#include "../../ShaderMeta.h"

#include <utility>

namespace experiment::cooked
{
    namespace
    {
        void AddIssue(ShaderMetaCookProductResult& result,
            std::string context, std::string message)
        {
            result.issues.push_back({ std::move(context), std::move(message) });
        }
    }

    ShaderMetaCookProductResult BuildShaderMetaCookProduct(
        const ShaderMetaCookProductRequest& request)
    {
        ShaderMetaCookProductResult result;
        std::error_code error;

        const std::filesystem::path assetRoot =
            std::filesystem::weakly_canonical(request.assetRoot, error);
        if (error || assetRoot.empty()
            || !std::filesystem::is_directory(assetRoot, error))
        {
            AddIssue(result, "request.assetRoot",
                "asset root가 유효한 디렉터리가 아니다.");
            return result;
        }

        error.clear();
        const std::filesystem::path source =
            std::filesystem::weakly_canonical(request.sourcePath, error);
        if (error || source.empty()
            || !std::filesystem::is_regular_file(source, error))
        {
            AddIssue(result, "request.sourcePath",
                "source .shadermeta가 유효한 파일이 아니다.");
            return result;
        }
        if (!IsContainedPath(assetRoot, source))
        {
            AddIssue(result, "request.sourcePath",
                "source .shadermeta가 asset root 밖에 있다.");
            return result;
        }
        if (source.extension() != ".shadermeta")
        {
            AddIssue(result, "shadermeta.extension",
                "확장자가 .shadermeta가 아니다: " + source.extension().string());
            return result;
        }

        std::filesystem::path metaPath = source;
        metaPath += ".meta";
        AssetId shaderMetaAssetId{};
        std::string metaFailure;
        if (!ReadMetaAssetId(metaPath, shaderMetaAssetId, metaFailure))
        {
            AddIssue(result, "shadermeta.meta", std::move(metaFailure));
            return result;
        }
        if (!IsAssetIdV4(shaderMetaAssetId))
        {
            AddIssue(result, "shadermeta.meta",
                ".shadermeta meta GUID가 canonical UUIDv4가 아니다.");
            return result;
        }

        std::string text;
        if (!ReadTextFile(source, text))
        {
            AddIssue(result, "shadermeta.read",
                "source .shadermeta를 읽을 수 없다: " + source.string());
            return result;
        }
        if (text.empty())
        {
            AddIssue(result, "shadermeta.read",
                "source .shadermeta가 비어 있다: " + source.string());
            return result;
        }

        // ★ 정본 검증기. 여기서 schema 를 다시 해석하지 않는다.
        ShaderMeta parsed;
        std::string parseError;
        if (!ShaderMetaLoader::Parse(text, source,
            FileGuid{ shaderMetaAssetId.value }, parsed, parseError))
        {
            AddIssue(result, "shadermeta.schema", std::move(parseError));
            return result;
        }

        // source HLSL — 간선을 그리지는 않지만 sidecar GUID 로 해소 가능한지는
        // 증명한다.
        //
        // ★ 여기서 존재·상위이동·절대경로를 **다시 검사하지 않는다.**
        //   처음에는 검사했는데, 게이트에 "어느 guard 가 걸었는가"를 넣자마자
        //   그 둘이 **한 번도 도달하지 않는 죽은 코드**임이 드러났다 —
        //   `ShaderMetaLoader::Parse` 가 `IsSafeRelativeSource` 로 `..`·절대경로를
        //   막고 `is_regular_file` 로 존재를 확인한 뒤에야 성공을 돌려준다.
        //   `.shadermeta` 가 이미 asset root 안임을 위에서 확인했고 source 는
        //   상위 이동이 없는 상대 경로이므로, 해소 결과도 asset root 안이다.
        //
        //   "혹시 모르니 한 번 더"로 두면 변이가 그 줄을 지워도 게이트가
        //   초록이다. 실제로 그랬다.
        error.clear();
        const std::filesystem::path shaderSource =
            std::filesystem::weakly_canonical(parsed.ResolveSource(source), error);

        std::filesystem::path shaderMetaSidecar = shaderSource;
        shaderMetaSidecar += ".meta";
        AssetId sourceShaderAssetId{};
        if (!ReadMetaAssetId(shaderMetaSidecar, sourceShaderAssetId, metaFailure))
        {
            AddIssue(result, "shadermeta.source.meta", std::move(metaFailure));
            return result;
        }
        if (!IsAssetIdV4(sourceShaderAssetId))
        {
            AddIssue(result, "shadermeta.source.meta",
                "source 셰이더 meta GUID가 canonical UUIDv4가 아니다.");
            return result;
        }

        const std::string artifactPath =
            MakeDerivedShaderMetaArtifactPath(shaderMetaAssetId);
        if (artifactPath.empty())
        {
            AddIssue(result, "shadermeta.artifactPath",
                ".shadermeta GUID가 Derived 경로를 만들지 못했다.");
            return result;
        }

        std::vector<std::byte> bytes(text.size());
        for (std::size_t index = 0u; index < text.size(); ++index)
            bytes[index] = static_cast<std::byte>(text[index]);

        Sha256Digest digest{};
        std::string hashError;
        if (!ComputeSha256(bytes, digest, hashError))
        {
            AddIssue(result, "shadermeta.sha256", std::move(hashError));
            return result;
        }

        error.clear();
        const std::filesystem::path relativeShader =
            std::filesystem::relative(shaderSource, assetRoot, error);

        ShaderMetaCookProduct product;
        product.shaderMetaAssetId = shaderMetaAssetId;
        product.sourceShaderAssetId = sourceShaderAssetId;
        product.artifactPath = artifactPath;
        product.artifactBytes = std::move(bytes);
        product.name = parsed.name;
        product.sourceRelativePath = error ? std::string{}
            : relativeShader.generic_string();
        product.propertyCount = parsed.properties.size();
        product.keywordAxisCount = parsed.keywords.size();
        product.passCount = parsed.passes.size();

        CookedAssetManifestEntry entry;
        entry.assetId = shaderMetaAssetId;
        entry.kind = CookedAssetKind::ShaderMeta;
        // ★ schema 정본에서 유도한다. 손으로 올리는 숫자가 아니다.
        entry.formatVersion = ShaderMeta::kSchemaVersion;
        entry.byteSize = product.artifactBytes.size();
        entry.contentSha256 = digest;
        entry.artifactPath = artifactPath;
        // dependency 는 비어 있다 — 헤더의 이유 참조(HLSL 은 B2/B3 소유).
        product.manifestEntry = std::move(entry);

        result.product = std::move(product);
        return result;
    }
}
