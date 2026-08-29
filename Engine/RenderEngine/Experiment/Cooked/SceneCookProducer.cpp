#include "SceneCookProducer.h"

#include "CookSupport.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        void AddIssue(SceneCookProductResult& result,
            std::string context, std::string message)
        {
            result.issues.push_back({ std::move(context), std::move(message) });
        }

        [[nodiscard]] bool IsNilGuidText(std::string_view text) noexcept
        {
            return text == "00000000-0000-0000-0000-000000000000";
        }

        // legacy 이름 참조 필드. 값이 비어 있지 않으면 "GUID 없이 이름으로
        // 가리키는 텍스처"가 하나 있다는 뜻이다.
        inline constexpr std::array<std::string_view, 5> kLegacyTextureNameKeys{
            "m_baseColorTexName", "m_normalTexName", "m_ORM_TexName",
            "m_AO_TexName", "m_EmissiveTexName",
        };

        // producer 가 없어서 간선을 그릴 수 없는 GUID 참조.
        inline constexpr std::array<std::string_view, 2> kUnproducedGuidKeys{
            "m_BehaviorTreeGuid", "m_BlackBoardGuid",
        };

        struct Walk final
        {
            SceneCookProduct& product;
            AssetId self{};
            std::vector<AssetId>& dependencies;
            std::string& failureKey;
            std::string& failureValue;
            std::string& failureContext;
            bool failed{};

            void AddEdge(const AssetId& id, std::size_t& counter)
            {
                // ★ 자기 자신은 의존이 아니라 **정체성**이다.
                //   프리팹은 자기 루트 엔티티에 `m_prefabFileGuid` 로 자기 GUID 를
                //   적어 둔다 — "이것은 이 프리팹의 인스턴스다" 라는 뜻이다.
                //   그대로 간선을 그리면 manifest 가 self-dependency 로 거부한다.
                if (id == self) return;
                ++counter;
                if (std::ranges::find(dependencies, id) == dependencies.end())
                    dependencies.push_back(id);
            }

            // key 가 GUID 참조면 처리하고 true. 형식이 틀리면 failed 를 세운다.
            [[nodiscard]] bool HandleGuidKey(const std::string& key,
                const YAML::Node& value)
            {
                std::size_t* counter = nullptr;
                if (key == "m_fileGuid") counter = &product.modelEdges;
                else if (key == "m_prefabFileGuid") counter = &product.prefabEdges;
                else if (key == "m_textureGuid") counter = &product.textureEdges;
                else if (std::ranges::find(kUnproducedGuidKeys, key)
                    != kUnproducedGuidKeys.end())
                {
                    if (value.IsScalar() && !IsNilGuidText(value.Scalar()))
                        ++product.unproducedGuidReferences;
                    return true;
                }
                else return false;

                // ★ 있는데 스칼라가 아니면 실패다. b2c-3 에서 brace 표기가
                //   YAML flow mapping 으로 읽혀 간선이 조용히 사라진 전례가 있다.
                if (!value.IsScalar())
                {
                    // ★ 사유를 따로 둔다. 이걸 아래 파싱 실패와 같은
                    //   context 로 두었더니 **이 guard 를 지워도 게이트가
                    //   초록이었다** — 비스칼라 노드의 `Scalar()` 가 빈
                    //   문자열을 돌려줘 그다음 파싱이 대신 거부했기 때문이다.
                    //   거부 자체는 맞았지만 진단이 달라진다 — brace 표기는
                    //   저작자가 실제로 저지르는 실수라 메시지가 중요하다.
                    failed = true;
                    failureKey = key;
                    failureValue = "(스칼라가 아님 — brace 표기는 YAML 매핑으로 읽힌다)";
                    failureContext = "scene.reference.kind";
                    return true;
                }
                const std::string& text = value.Scalar();
                if (IsNilGuidText(text)) return true;

                AssetId id{};
                if (!TryParseCanonicalAssetId(text, id))
                {
                    failed = true;
                    failureKey = key;
                    failureValue = text;
                    failureContext = "scene.reference";
                    return true;
                }
                AddEdge(id, *counter);
                return true;
            }

            void Visit(const YAML::Node& node)
            {
                if (failed || !node) return;

                if (node.IsMap())
                {
                    for (const auto& pair : node)
                    {
                        if (failed) return;
                        if (!pair.first.IsScalar()) { Visit(pair.second); continue; }
                        const std::string key = pair.first.Scalar();

                        if (std::ranges::find(kLegacyTextureNameKeys, key)
                            != kLegacyTextureNameKeys.end())
                        {
                            if (pair.second.IsScalar()
                                && !pair.second.Scalar().empty())
                            {
                                ++product.legacyTextureNameReferences;
                            }
                            continue;
                        }
                        if (HandleGuidKey(key, pair.second)) continue;
                        Visit(pair.second);
                    }
                    return;
                }
                if (node.IsSequence())
                {
                    for (const YAML::Node& element : node)
                    {
                        if (failed) return;
                        Visit(element);
                    }
                }
            }
        };
    }

    SceneCookProductResult BuildSceneCookProduct(
        const SceneCookProductRequest& request)
    {
        SceneCookProductResult result;
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
                "source scene/prefab이 유효한 파일이 아니다.");
            return result;
        }
        if (!IsContainedPath(assetRoot, source))
        {
            AddIssue(result, "request.sourcePath",
                "source scene/prefab이 asset root 밖에 있다.");
            return result;
        }

        const std::string extension = source.extension().string();
        CookedAssetKind kind{};
        if (extension == ".creator") kind = CookedAssetKind::Scene;
        else if (extension == ".prefab") kind = CookedAssetKind::Prefab;
        else
        {
            AddIssue(result, "scene.extension",
                "확장자가 .creator/.prefab이 아니다: " + extension);
            return result;
        }

        std::filesystem::path metaPath = source;
        metaPath += ".meta";
        AssetId sceneAssetId{};
        std::string metaFailure;
        if (!ReadMetaAssetId(metaPath, sceneAssetId, metaFailure))
        {
            AddIssue(result, "scene.meta", std::move(metaFailure));
            return result;
        }
        if (!IsAssetIdV4(sceneAssetId))
        {
            AddIssue(result, "scene.meta",
                "scene/prefab meta GUID가 canonical UUIDv4가 아니다.");
            return result;
        }

        std::string text;
        if (!ReadTextFile(source, text))
        {
            AddIssue(result, "scene.read",
                "source scene/prefab을 읽을 수 없다: " + source.string());
            return result;
        }
        if (text.empty())
        {
            AddIssue(result, "scene.read",
                "source scene/prefab이 비어 있다: " + source.string());
            return result;
        }

        YAML::Node root;
        try
        {
            root = YAML::Load(text);
        }
        catch (const YAML::Exception& exception)
        {
            AddIssue(result, "scene.yaml",
                std::string("YAML을 파싱할 수 없다: ") + exception.what());
            return result;
        }
        if (!root || !(root.IsMap() || root.IsSequence()))
        {
            AddIssue(result, "scene.yaml",
                "scene/prefab 문서가 매핑도 시퀀스도 아니다.");
            return result;
        }

        SceneCookProduct product;
        product.sceneAssetId = sceneAssetId;
        product.kind = kind;

        std::vector<AssetId> dependencies;
        std::string failureKey;
        std::string failureValue;
        std::string failureContext;
        Walk walk{ product, sceneAssetId, dependencies,
            failureKey, failureValue, failureContext };
        walk.Visit(root);
        if (walk.failed)
        {
            AddIssue(result, failureContext,
                failureKey + "가 canonical UUIDv4가 아니다: " + failureValue);
            return result;
        }

        product.artifactPath = kind == CookedAssetKind::Scene
            ? MakeDerivedSceneArtifactPath(sceneAssetId)
            : MakeDerivedPrefabArtifactPath(sceneAssetId);
        if (product.artifactPath.empty())
        {
            AddIssue(result, "scene.artifactPath",
                "scene/prefab GUID가 Derived 경로를 만들지 못했다.");
            return result;
        }

        product.artifactBytes.resize(text.size());
        for (std::size_t index = 0u; index < text.size(); ++index)
            product.artifactBytes[index] = static_cast<std::byte>(text[index]);

        Sha256Digest digest{};
        std::string hashError;
        if (!ComputeSha256(product.artifactBytes, digest, hashError))
        {
            AddIssue(result, "scene.sha256", std::move(hashError));
            return result;
        }

        CookedAssetManifestEntry entry;
        entry.assetId = sceneAssetId;
        entry.kind = kind;
        entry.formatVersion = kSceneArtifactVersion;
        entry.byteSize = product.artifactBytes.size();
        entry.contentSha256 = digest;
        entry.artifactPath = product.artifactPath;
        entry.dependencies = std::move(dependencies);
        product.manifestEntry = std::move(entry);

        result.product = std::move(product);
        return result;
    }
}
