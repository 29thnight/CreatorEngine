#include "MaterialCookProducer.h"

#include "CookSupport.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringParsedDocument.h"

#include <algorithm>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        void AddIssue(MaterialCookProductResult& result,
            std::string context, std::string message)
        {
            result.issues.push_back({ std::move(context), std::move(message) });
        }

        // nil 은 "텍스처 없음"이라는 저작 표현이다. 간선을 만들지 않는다.
        [[nodiscard]] bool IsNilGuidText(std::string_view text) noexcept
        {
            return text == "00000000-0000-0000-0000-000000000000";
        }
    }

    MaterialCookProductResult BuildMaterialCookProduct(
        const MaterialCookProductRequest& request)
    {
        MaterialCookProductResult result;
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
                "source material이 유효한 파일이 아니다.");
            return result;
        }
        if (!IsContainedPath(assetRoot, source))
        {
            AddIssue(result, "request.sourcePath",
                "source material이 asset root 밖에 있다.");
            return result;
        }
        if (source.extension() != ".asset")
        {
            AddIssue(result, "material.extension",
                "확장자가 .asset이 아니다: " + source.extension().string());
            return result;
        }

        std::filesystem::path metaPath = source;
        metaPath += ".meta";
        AssetId materialAssetId{};
        std::string metaFailure;
        if (!ReadMetaAssetId(metaPath, materialAssetId, metaFailure))
        {
            AddIssue(result, "material.meta", std::move(metaFailure));
            return result;
        }
        if (!IsAssetIdV4(materialAssetId))
        {
            AddIssue(result, "material.meta",
                "material meta GUID가 canonical UUIDv4가 아니다.");
            return result;
        }

        std::string text;
        if (!ReadTextFile(source, text))
        {
            AddIssue(result, "material.read",
                "source material을 읽을 수 없다: " + source.string());
            return result;
        }
        if (text.empty())
        {
            AddIssue(result, "material.read",
                "source material이 비어 있다: " + source.string());
            return result;
        }

		std::string parseError;
		const Authoring::ParsedDocument document =
			Authoring::ParsedDocument::ParseText(text, parseError);
		if (!document)
		{
			AddIssue(result, "material.yaml",
				"YAML을 파싱할 수 없다: " + parseError);
			return result;
		}
		const Authoring::ReadNode root = document.Root();
        if (!root || !root.IsMap())
        {
            AddIssue(result, "material.yaml",
                "material 문서가 매핑이 아니다.");
            return result;
        }

        // ── shader 의존 ────────────────────────────────────────────────
		const Authoring::ReadNode shaderNode = root["m_shaderMetaGuid"];
        if (!shaderNode || !shaderNode.IsScalar())
        {
            // ★ 없으면 실패다. 조용히 넘어가면 "간선이 없는 재질"과
            //   "필드 이름이 바뀐 재질"이 같은 모습이 된다.
            AddIssue(result, "material.shaderMetaGuid",
                "m_shaderMetaGuid 필드가 없다(스키마가 바뀌었을 수 있다).");
            return result;
        }
        AssetId shaderMetaAssetId{};
		if (!TryParseCanonicalAssetId(shaderNode.Scalar(), shaderMetaAssetId))
		{
			AddIssue(result, "material.shaderMetaGuid",
				"m_shaderMetaGuid가 canonical UUIDv4가 아니다: "
				+ shaderNode.AsString());
            return result;
        }

        std::vector<AssetId> dependencies;
        dependencies.push_back(shaderMetaAssetId);

        // ── texture 의존 ───────────────────────────────────────────────
        std::size_t texturePropertyCount = 0u;
		const Authoring::ReadNode properties = root["m_propertyValues"];
        if (properties && properties.IsSequence())
        {
			for (const Authoring::ReadNode property : properties)
			{
				if (!property.IsMap()) continue;
				const Authoring::ReadNode textureNode = property["m_textureGuid"];
                // 키가 없으면 "텍스처 슬롯이 아니다" — 정상이다.
                if (!textureNode) continue;

                // ★ 있는데 스칼라가 아니면 **실패**다. 조용히 건너뛰면 안 된다.
                //   게이트가 이걸 잡았다: 검사용으로 brace 표기 GUID
                //   `{4444...}` 를 넣었더니 YAML 이 그것을 flow mapping 으로
                //   읽었고, 스칼라가 아니라는 이유로 간선이 소리 없이
                //   사라졌다. 잘못 적힌 GUID 가 "텍스처 없음"과 같은 모습이
                //   되는 것이 정확히 폐포가 못 잡는 형태다.
                if (!textureNode.IsScalar())
                {
                    AddIssue(result, "material.textureGuid",
                        "m_textureGuid가 스칼라가 아니다"
                        " (brace 표기는 YAML 매핑으로 읽힌다).");
                    return result;
                }

				const std::string guidText = textureNode.AsString();
                if (IsNilGuidText(guidText)) continue;

                ++texturePropertyCount;
                AssetId textureAssetId{};
                if (!TryParseCanonicalAssetId(guidText, textureAssetId))
                {
                    AddIssue(result, "material.textureGuid",
                        "m_textureGuid가 canonical UUIDv4가 아니다: " + guidText);
                    return result;
                }
                if (std::ranges::find(dependencies, textureAssetId)
                    == dependencies.end())
                {
                    dependencies.push_back(textureAssetId);
                }
            }
        }

        const std::string artifactPath =
            MakeDerivedMaterialArtifactPath(materialAssetId);
        if (artifactPath.empty())
        {
            AddIssue(result, "material.artifactPath",
                "material GUID가 Derived 경로를 만들지 못했다.");
            return result;
        }

        std::vector<std::byte> bytes;
        std::string encodeError;
        if (!Authoring::EncodeCookedDocument(root, bytes, encodeError))
        {
            AddIssue(result, "material.cookedDocument", std::move(encodeError));
            return result;
        }

        Sha256Digest digest{};
        std::string hashError;
        if (!ComputeSha256(bytes, digest, hashError))
        {
            AddIssue(result, "material.sha256", std::move(hashError));
            return result;
        }

        MaterialCookProduct product;
        product.materialAssetId = materialAssetId;
        product.shaderMetaAssetId = shaderMetaAssetId;
        product.artifactPath = artifactPath;
        product.artifactBytes = std::move(bytes);
        product.texturePropertyCount = texturePropertyCount;
        // shader 하나를 뺀 나머지가 서로 다른 texture 수다.
        product.distinctTextureCount = dependencies.size() - 1u;
		if (const Authoring::ReadNode nameNode = root["m_name"];
			nameNode && nameNode.IsScalar())
		{
			product.name = nameNode.AsString();
        }

        CookedAssetManifestEntry entry;
        entry.assetId = materialAssetId;
        entry.kind = CookedAssetKind::Material;
        entry.formatVersion = kMaterialArtifactVersion;
        entry.byteSize = product.artifactBytes.size();
        entry.contentSha256 = digest;
        entry.artifactPath = artifactPath;
        entry.dependencies = std::move(dependencies);
        product.manifestEntry = std::move(entry);

        result.product = std::move(product);
        return result;
    }
}
