#include "SceneCookProducer.h"
#include "../MaterialAuthoringCodec.h"

#include "CookSupport.h"
#include "TextureCookProducer.h"
#include "../../Assets/AssetIdentityProfile.h" // PHASE 3.75 MBC8: UUIDv8 참조
#include "../../MaterialGraphRuntime.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringParsedDocument.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace experiment::cooked
{
namespace
{
void AddIssue(SceneCookProductResult& result, std::string context, std::string message)
{
    result.issues.push_back({std::move(context), std::move(message)});
}

[[nodiscard]] bool IsNilGuidText(std::string_view text) noexcept
{
    return text == "00000000-0000-0000-0000-000000000000";
}

// legacy 이름 참조 필드 ↔ 대응하는 property 이름.
//
// ★ **"이름 필드가 있다"가 위험한 게 아니다.** 위험한 것은 *GUID 가
//   없어서 이름에 의존하는* 경우다.
//
//   `DataSystem::FinalizeMaterialRuntime` 은 GUID 우선·이름 폴백이고,
//   `SynchronizeLegacyMaterialProperties` 는 **두 방향을 모두 채운다** —
//   이름에서 GUID 를 만들어 property 를 넣고, GUID 에서 이름도 되채운다.
//   그래서 이주가 끝난 재질도 이름 필드를 계속 갖는다. 처음에는 이름
//   필드 존재만으로 셌는데, 그 기준으로는 **이주해도 숫자가 안 줄어든다.**
//
//   그래서 같은 인라인 재질 안에 대응 `m_textureGuid` 가 있으면 세지
//   않는다. 남는 수가 곧 "GUID 로 못 가리키는 텍스처"이고, 그것이
//   D5-c 의 "source path 탐색 없이"가 요구하는 값이다.
struct LegacyTextureKey final
{
    std::string_view nameField;
    std::string_view propertyName;
};
inline constexpr std::array<LegacyTextureKey, 5> kLegacyTextureNameKeys{{
    {"m_baseColorTexName", "baseColorMap"},
    {"m_normalTexName", "normalMap"},
    {"m_ORM_TexName", "ormMap"},
    {"m_AO_TexName", "aoMap"},
    {"m_EmissiveTexName", "emissiveMap"},
}};

// 인라인 재질 매핑에서 해당 property 가 nil 아닌 texture GUID 를 갖는가.
[[nodiscard]] bool HasTextureGuidFor(const Authoring::ReadNode& materialNode, std::string_view propertyName)
{
    if (!materialNode.IsMap())
        return false;
    const Authoring::ReadNode properties = materialNode["m_propertyValues"];
    if (!properties || !properties.IsSequence())
        return false;
    for (const Authoring::ReadNode property : properties)
    {
        if (!property.IsMap())
            continue;
        const Authoring::ReadNode name = property["m_name"];
        if (!name || !name.IsScalar())
            continue;
        if (name.Scalar() != propertyName)
            continue;
        const Authoring::ReadNode guid = property["m_textureGuid"];
        if (!guid || !guid.IsScalar())
            return false;
        return guid.Scalar() != "00000000-0000-0000-0000-000000000000";
    }
    return false;
}

// producer 가 없어서 간선을 그릴 수 없는 GUID 참조.
inline constexpr std::array<std::string_view, 2> kUnproducedGuidKeys{
    "m_BehaviorTreeGuid",
    "m_BlackBoardGuid",
};

        [[nodiscard]] bool IsDecalTextureField(std::string_view key)
        {
            return key == "m_diffusefileName" || key == "m_normalFileName" || key == "m_ormFileName";
        }

        // Preserve the serialized component shape. Only these three existing
        // string fields are lowered to typed identities in the cooked document.
        [[nodiscard]] bool LowerDecalTexture(const Authoring::WriteNode& node,
            const std::filesystem::path& assetRoot, std::vector<AssetId>& references, std::string& error)
        {
            if (!node.Read().IsScalar())
            {
                error = "Decal texture reference must be a scalar";
                return false;
            }
            const auto text = node.Read().AsString();
            if (text.empty() || IsNilGuidText(text))
            {
                node.SetScalar(std::string_view{});
                return true;
            }
            AssetId id;
            if (!TryParseCanonicalAssetId(text, id) && !assets::TryParseCanonicalUuidV8(text, id.value))
            {
                // Match the legacy DecalComponent rule exactly. Do not scan
                // global basenames, stems, or substitute a similarly named image.
                const auto filename = std::filesystem::path(text).filename();
                std::error_code ec;
                const auto source = std::filesystem::weakly_canonical(assetRoot / "Textures" / filename, ec);
                auto extension = source.extension().string();
                std::ranges::transform(extension, extension.begin(), [](unsigned char ch)
                {
                    return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
                });
                if (ec || filename.empty() || !IsContainedPath(assetRoot, source)
                    || !IsSupportedTextureExtension(extension))
                {
                    error = "Cannot resolve decal texture under Assets/Textures: " + text;
                    return false;
                }
                const auto meta = std::filesystem::weakly_canonical(source.string() + ".meta", ec);
                constexpr std::uintmax_t maximumSidecarBytes = 1024u * 1024u;
                if (ec || !IsContainedPath(assetRoot, meta) || !std::filesystem::is_regular_file(meta, ec) || ec
                    || std::filesystem::file_size(meta, ec) > maximumSidecarBytes || ec
                    || !ReadMetaAssetId(meta, id, error) || !IsAssetIdV4(id))
                {
                    error = "Decal texture requires a canonical source sidecar: " + text + ": " + error;
                    return false;
                }
            }
            node.SetScalar(Uuid::ToString(id.value));
            if (std::ranges::find(references, id) == references.end())
            {
                references.push_back(id);
            }
            return true;
        }

        [[nodiscard]] bool LowerDecalTextures(const Authoring::WriteNode& node,
            const std::filesystem::path& assetRoot, std::vector<AssetId>& references,
            std::string& error, std::size_t& visited, std::size_t depth = 0u)
        {
            if (depth > 256u || ++visited > 1024u * 1024u)
            {
                error = "Decal reference traversal exceeds the scene document limit";
                return false;
            }
            if (node.Read().IsSequence())
            {
                for (std::size_t index = 0u; index < node.Size(); ++index)
                {
                    if (!LowerDecalTextures(node.At(index), assetRoot, references, error, visited, depth + 1u))
                    {
                        return false;
                    }
                }
                return true;
            }
            if (!node.Read().IsMap())
            {
                return true;
            }
            for (const auto entry : node.Read().Map())
            {
                const auto key = entry.key.AsString();
                const auto child = node.Child(key);
                if (IsDecalTextureField(key))
                {
                    if (!LowerDecalTexture(child, assetRoot, references, error))
                    {
                        return false;
                    }
                }
                else if (key == "m_valueYaml" && entry.value.IsScalar() && !entry.value.Scalar().empty())
                {
                    auto nested = Authoring::WriteDocument::ParseText(entry.value.AsString(), &error);
                    if (!nested)
                    {
                        return false;
                    }
                    if (IsDecalTextureField(node.Read()["m_propertyName"].Scalar()))
                    {
                        if (!LowerDecalTexture(nested->Root(), assetRoot, references, error))
                        {
                            return false;
                        }
                    }
                    else if (!LowerDecalTextures(nested->Root(), assetRoot, references, error, visited, depth + 1u))
                    {
                        return false;
                    }
                    child.SetScalar(nested->Dump());
                }
                else if (!LowerDecalTextures(child, assetRoot, references, error, visited, depth + 1u))
                {
                    return false;
                }
            }
            return true;
        }

// PrefabOverride::m_valueYaml is an authoring file-format scalar that
// contains a second YAML document. Leaving it unchanged would make a
// later Player prefab instantiate re-enter the text parser even though
// the outer scene is CEDO. Cook that nested document into a strict
// CEDO1/base64 envelope while cloning the rest of the tree verbatim.
[[nodiscard]] bool BuildRuntimeTree(const Authoring::ReadNode& source, const Authoring::WriteNode& destination,
                                    std::size_t& cookedOverrideValues, std::string& error)
{
    if (source.IsNull())
    {
        destination.SetNull();
        return true;
    }
    if (source.IsScalar())
    {
        destination.SetScalar(source.Scalar());
        return true;
    }
    if (source.IsSequence())
    {
        destination.SetSequence();
        for (const Authoring::ReadNode child : source)
        {
            if (!BuildRuntimeTree(child, destination.Append(), cookedOverrideValues, error))
                return false;
        }
        return true;
    }
    if (!source.IsMap())
    {
        error = "지원하지 않는 scene authoring node type이다";
        return false;
    }

    destination.SetMap();
    for (const Authoring::MapEntry entry : source.Map())
    {
        const std::string key = entry.key.AsStringChecked();
        const Authoring::WriteNode child = destination.Child(key);
        if (key == "m_valueYaml")
        {
            if (!entry.value.IsScalar())
            {
                error = "PrefabOverride.m_valueYaml이 scalar가 아니다";
                return false;
            }
            if (entry.value.Scalar().empty())
            {
                child.SetScalar(std::string_view{});
                continue;
            }
            std::string parseError;
            const Authoring::ParsedDocument embedded =
                Authoring::ParsedDocument::ParseText(entry.value.AsString(), parseError);
            if (!embedded)
            {
                error = "PrefabOverride.m_valueYaml parse 실패: " + parseError;
                return false;
            }
            std::string envelope;
            if (!Authoring::EncodeCookedDocumentTextEnvelope(embedded.Root(), envelope, error))
                return false;
            child.SetScalar(envelope);
            ++cookedOverrideValues;
            continue;
        }
        if (!BuildRuntimeTree(entry.value, child, cookedOverrideValues, error))
            return false;
    }
    return true;
}

struct Walk final
{
    SceneCookProduct& product;
    AssetId self{};
    std::vector<AssetId>& dependencies;
    std::string& failureKey;
    std::string& failureValue;
    std::string& failureContext;
    bool failed{};
    bool bootstrap{};
    std::size_t bootstrapNodes{};

    void AddEdge(const AssetId& id, std::size_t& counter, CookedAssetKind kind)
    {
        // ★ 자기 자신은 의존이 아니라 **정체성**이다.
        //   프리팹은 자기 루트 엔티티에 `m_prefabFileGuid` 로 자기 GUID 를
        //   적어 둔다 — "이것은 이 프리팹의 인스턴스다" 라는 뜻이다.
        //   그대로 간선을 그리면 manifest 가 self-dependency 로 거부한다.
        if (id == self)
            return;
        ++counter;
        if (bootstrap)
        {
            const TypedAssetReference reference{{id, {}}, kind};
            if (std::ranges::find(product.bootstrapReferences, reference) == product.bootstrapReferences.end())
                product.bootstrapReferences.push_back(reference);
        }
        if (std::ranges::find(dependencies, id) == dependencies.end())
            dependencies.push_back(id);
    }

    // key 가 GUID 참조면 처리하고 true. 형식이 틀리면 failed 를 세운다.
    [[nodiscard]] bool HandleGuidKey(const std::string& key, const Authoring::ReadNode& value)
    {
        std::size_t* counter = nullptr;
        CookedAssetKind kind{};
        // S2c-1: MeshRenderer가 모델 출처를 자기 m_modelGuid로 갖는다.
        // legacy 씬은 인라인 재질의 m_fileGuid가 모델 GUID를 나른다 —
        // 이주기 씬은 둘 다 실려 카운터는 중복될 수 있지만 dependencies
        // 는 AddEdge가 dedupe하므로 폐포는 정확하다.
        if (key == "m_fileGuid" || key == "m_modelGuid" || (bootstrap && key == "m_Motion"))
        {
            counter = &product.modelEdges; kind = CookedAssetKind::Model;
        }
        else if (bootstrap && key == "m_meshAssetId")
        {
            counter = &product.geometryEdges; kind = CookedAssetKind::Mesh;
        }
        else if (key == "geometryAsset")
        {
            counter = &product.geometryEdges; kind = CookedAssetKind::CollisionGeometry;
        }
        else if (key == "m_prefabFileGuid")
        {
            counter = &product.prefabEdges; kind = CookedAssetKind::Prefab;
        }
        else if (key == "m_textureGuid")
        {
            counter = &product.textureEdges; kind = CookedAssetKind::Texture;
        }
        else if (std::ranges::find(kUnproducedGuidKeys, key) != kUnproducedGuidKeys.end())
        {
            if (value.IsScalar() && !IsNilGuidText(value.Scalar()))
                ++product.unproducedGuidReferences;
            return true;
        }
        else
            return false;

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
        const std::string text = value.AsString();
        if (IsNilGuidText(text) || (key == "geometryAsset" && text.empty()))
            return true;

        AssetId id{};
        // PHASE 3.75 MBC8 — 씬·프리팹의 모델(m_modelGuid·m_Motion)·subasset
        // (m_meshAssetId·embedded texture) 참조는 UUIDv8이다(MBC4 cutover).
        // 비모델 자산은 v4 그대로. 두 canonical 표기 외는 예전처럼 거부한다.
        if (!TryParseCanonicalAssetId(text, id) && !assets::TryParseCanonicalUuidV8(text, id.value))
        {
            failed = true;
            failureKey = key;
            failureValue = text;
            failureContext = "scene.reference";
            return true;
        }
        AddEdge(id, *counter, kind);
        return true;
    }

    void FoliageReference(const Authoring::ReadNode& node, const char* key,
        CookedAssetKind kind, bool required, std::size_t& counter)
    {
        const auto value = node[key];
        if (!required && (!value || (value.IsScalar() && IsNilGuidText(value.Scalar()))))
        {
            return;
        }
        AssetId id;
        if (!value.IsScalar() || (!TryParseCanonicalAssetId(value.Scalar(), id)
            && !assets::TryParseCanonicalUuidV8(value.Scalar(), id.value)))
        {
            failed = true; failureKey = key;
            failureValue = required ? "Source-free Foliage requires a nonnil typed Model identity; source-name fallback is unavailable"
                : "Explicit Foliage identity must be a canonical typed UUID";
            failureContext = "scene.bootstrapFoliageBinding";
            return;
        }
        AddEdge(id, counter, kind);
    }

    void Visit(const Authoring::ReadNode& node, std::size_t depth = 0u,
        bool materialContext = false, bool foliageContext = false)
    {
        if (bootstrap && (++bootstrapNodes > 65536u || depth > 256u))
        {
            failed = true;
            failureKey = "bootstrap"; failureValue = "Document reference traversal limit exceeded";
            failureContext = "scene.bootstrapLimit";
            return;
        }
        if (failed || !node)
            return;

        if (node.IsMap())
        {
            // Named Foliage collections provide explicit context. A standalone
            // inline FoliageType must carry the complete reflected link shape;
            // a coincidental user-data flag alone is not a type declaration.
            const bool foliageShape = node["m_allowLegacySource"] && node["m_modelGuid"]
                && node["m_meshAssetId"] && node["m_materialAssetId"];
            if (bootstrap && (foliageContext || foliageShape))
            {
                const auto allowLegacy = node["m_allowLegacySource"];
                if (allowLegacy && (!allowLegacy.IsScalar()
                    || (allowLegacy.Scalar() != "true" && allowLegacy.Scalar() != "false")))
                {
                    failed = true; failureKey = "m_allowLegacySource";
                    failureValue = "Foliage legacy-source flag must be a boolean";
                    failureContext = "scene.bootstrapFoliageBinding";
                    return;
                }
                FoliageReference(node, "m_modelGuid", CookedAssetKind::Model, true, product.modelEdges);
                if (!failed) FoliageReference(node, "m_meshAssetId", CookedAssetKind::Mesh, false, product.geometryEdges);
                if (!failed) FoliageReference(node, "m_materialAssetId", CookedAssetKind::Material, false, product.bootstrapMaterialEdges);
                return;
            }
            if (bootstrap && materialContext && node["ref"])
            {
                const auto reference = node["ref"];
                AssetId materialId;
                if (!reference.IsScalar()
                    || (!TryParseCanonicalAssetId(reference.Scalar(), materialId)
                        && !assets::TryParseCanonicalUuidV8(reference.Scalar(), materialId.value)))
                {
                    failed = true; failureKey = "m_Material.ref";
                    failureValue = "Base material reference must be a nonnil canonical UUID";
                    failureContext = "scene.bootstrapMaterialReference";
                    return;
                }
                AddEdge(materialId, product.bootstrapMaterialEdges, CookedAssetKind::Material);
                const auto overrides = node["overrides"];
                if (overrides && !overrides.IsSequence())
                {
                    failed = true; failureKey = "m_Material.overrides";
                    failureValue = "Material overrides must be a sequence";
                    failureContext = "scene.bootstrapMaterialReference";
                    return;
                }
                if (overrides)
                {
                    if (overrides.Size() > 65536u)
                    {
                        failed = true; failureKey = "m_Material.overrides";
                        failureValue = "Material override count exceeds the document limit";
                        failureContext = "scene.bootstrapLimit";
                        return;
                    }
                    for (const auto propertyNode : overrides)
                    {
                        experiment::MaterialProperty property;
                        std::string error;
                        const auto name = propertyNode["name"];
                        if (!propertyNode.IsMap() || !name.IsScalar()
                            || !experiment::DeserializeMaterialPropertyValue(propertyNode, name.AsString(), property.value, error))
                        {
                            failed = true; failureKey = "m_Material.overrides";
                            failureValue = error.empty() ? "Material override requires a typed value and scalar name" : error;
                            failureContext = "scene.bootstrapMaterialReference";
                            return;
                        }
                        if (const auto* texture = std::get_if<experiment::TextureReference>(&property.value);
                            texture && texture->assetId.IsValid())
                        {
                            AddEdge(texture->assetId, product.textureEdges, CookedAssetKind::Texture);
                        }
                    }
                }
                return;
            }
            if (bootstrap && node["schema"] && node["shaderAssetId"])
            {
                experiment::Material material;
                std::string error;
                if (!experiment::DeserializeMaterialAuthoring(node, material, error))
                {
                    failed = true; failureKey = "shaderAssetId"; failureValue = error; failureContext = "scene.bootstrapMaterial";
                    return;
                }
                if (!material.assetId.IsValid())
                {
                    failed = true; failureKey = "assetId";
                    failureValue = "Source-free inline authored Material requires a nonnil mounted base Material identity for its Code program";
                    failureContext = "scene.bootstrapMaterialBinding";
                    return;
                }
                AddEdge(material.assetId, product.bootstrapMaterialEdges, CookedAssetKind::Material);
                AddEdge(material.shaderAssetId, product.materialGraphEdges, CookedAssetKind::ShaderMeta);
                for (const auto& property : material.properties)
                {
                    if (const auto* texture = std::get_if<experiment::TextureReference>(&property.value);
                        texture && texture->assetId.IsValid())
                        AddEdge(texture->assetId, product.textureEdges, CookedAssetKind::Texture);
                }
                return;
            }
            if (node["lattice_material"])
            {
                material_graph::InstanceDocument document;
                std::string error;
                if (!material_graph::ReadInstanceDocument(node, document, error))
                {
                    failed = true;
                    failureKey = "lattice_material";
                    failureValue = error;
                    failureContext = "scene.materialGraph";
                    return;
                }
                AddEdge(document.description.graphId, product.materialGraphEdges, CookedAssetKind::MaterialProgram);
                for (const auto& texture : document.description.textures)
                {
                    AddEdge(texture.assetId, product.textureEdges, CookedAssetKind::Texture);
                }
                return;
            }
            for (const Authoring::MapEntry pair : node.Map())
            {
                if (failed)
                    return;
                if (!pair.key.IsScalar())
                {
                    Visit(pair.value, depth + 1u);
                    continue;
                }
                const std::string key = pair.key.AsString();
                if (bootstrap && key == "m_foliageTypes")
                {
                    if (!pair.value.IsSequence())
                    {
                        failed = true; failureKey = key; failureValue = "Foliage types must be a sequence";
                        failureContext = "scene.bootstrapFoliageBinding";
                        return;
                    }
                    Visit(pair.value, depth + 1u, false, true);
                    continue;
                }
                if (bootstrap && key == "m_modelGuid" && node["m_meshAssetId"].IsScalar()
                    && !IsNilGuidText(node["m_meshAssetId"].Scalar())) continue;
                if (bootstrap && key == "m_valueYaml" && pair.value.IsScalar() && !pair.value.Scalar().empty())
                {
                    std::string error;
                    const auto nested = Authoring::ParsedDocument::ParseText(pair.value.AsString(), error);
                    if (!nested)
                    {
                        failed = true; failureKey = key; failureValue = error; failureContext = "scene.bootstrapOverride";
                        return;
                    }
                    const auto propertyName = node["m_propertyName"];
                    Visit(nested.Root(), depth + 1u,
                        propertyName.IsScalar() && propertyName.Scalar() == "m_Material");
                    continue;
                }

                const auto legacy = std::ranges::find(kLegacyTextureNameKeys, key, &LegacyTextureKey::nameField);
                if (legacy != kLegacyTextureNameKeys.end())
                {
                    // 이름이 비었으면 참조가 아니다. 이름이 있어도
                    // 같은 재질에 GUID 가 있으면 폴백에 의존하지 않는다.
                    if (pair.value.IsScalar() && !pair.value.Scalar().empty() &&
                        !HasTextureGuidFor(node, legacy->propertyName))
                    {
                        ++product.legacyTextureNameReferences;
                    }
                    continue;
                }
                if (HandleGuidKey(key, pair.value))
                    continue;
                Visit(pair.value, depth + 1u, key == "m_Material");
            }
            return;
        }
        if (node.IsSequence())
        {
            for (const Authoring::ReadNode element : node)
            {
                if (failed)
                    return;
                if (bootstrap && foliageContext && !element.IsMap())
                {
                    failed = true; failureKey = "Types"; failureValue = "Foliage type must be a map";
                    failureContext = "scene.bootstrapFoliageBinding";
                    return;
                }
                Visit(element, depth + 1u, false, foliageContext);
            }
        }
    }
};
} // namespace

bool CollectFoliageBootstrapReferences(const Authoring::ReadNode& root,
    std::vector<TypedAssetReference>& outReferences, std::string& failure)
{
    failure.clear();
    if (!root.IsMap() || !root["FoliageAsset"].IsMap() || !root["FoliageAsset"]["Types"].IsSequence())
    {
        failure = "Foliage bootstrap document requires FoliageAsset.Types sequence.";
        return false;
    }
    SceneCookProduct product;
    std::vector<AssetId> dependencies;
    std::string key, value, context;
    Walk walk{product, {}, dependencies, key, value, context, false, true};
    walk.Visit(root["FoliageAsset"]["Types"], 0u, false, true);
    if (walk.failed)
    {
        failure = context + ": " + key + ": " + value;
        return false;
    }
    outReferences = std::move(product.bootstrapReferences);
    return true;
}

SceneCookProductResult BuildSceneCookProduct(const SceneCookProductRequest& request)
{
    SceneCookProductResult result;
    std::error_code error;

    const std::filesystem::path assetRoot = std::filesystem::weakly_canonical(request.assetRoot, error);
    if (error || assetRoot.empty() || !std::filesystem::is_directory(assetRoot, error))
    {
        AddIssue(result, "request.assetRoot", "asset root가 유효한 디렉터리가 아니다.");
        return result;
    }

    error.clear();
    const std::filesystem::path source = std::filesystem::weakly_canonical(request.sourcePath, error);
    if (error || source.empty() || !std::filesystem::is_regular_file(source, error))
    {
        AddIssue(result, "request.sourcePath", "source scene/prefab이 유효한 파일이 아니다.");
        return result;
    }
    if (!IsContainedPath(assetRoot, source))
    {
        AddIssue(result, "request.sourcePath", "source scene/prefab이 asset root 밖에 있다.");
        return result;
    }

    const std::string extension = source.extension().string();
    CookedAssetKind kind{};
    if (extension == ".creator")
        kind = CookedAssetKind::Scene;
    else if (extension == ".prefab")
        kind = CookedAssetKind::Prefab;
    else
    {
        AddIssue(result, "scene.extension", "확장자가 .creator/.prefab이 아니다: " + extension);
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
        AddIssue(result, "scene.meta", "scene/prefab meta GUID가 canonical UUIDv4가 아니다.");
        return result;
    }

    std::string text;
    if (!ReadTextFile(source, text))
    {
        AddIssue(result, "scene.read", "source scene/prefab을 읽을 수 없다: " + source.string());
        return result;
    }
    if (text.empty())
    {
        AddIssue(result, "scene.read", "source scene/prefab이 비어 있다: " + source.string());
        return result;
    }

    std::string parseError;
    const Authoring::ParsedDocument document = Authoring::ParsedDocument::ParseText(text, parseError);
    if (!document)
    {
        AddIssue(result, "scene.yaml", "YAML을 파싱할 수 없다: " + parseError);
        return result;
    }
    const Authoring::ReadNode root = document.Root();
    if (!root || !(root.IsMap() || root.IsSequence()))
    {
        AddIssue(result, "scene.yaml", "scene/prefab 문서가 매핑도 시퀀스도 아니다.");
        return result;
    }

    const auto textureIdentityRoot = request.textureIdentityRoot.empty() ? assetRoot
        : std::filesystem::weakly_canonical(request.textureIdentityRoot, error);
    if (error || !std::filesystem::is_directory(textureIdentityRoot, error) || error)
    {
        AddIssue(result, "request.textureIdentityRoot", "Texture identity root must be an existing asset directory");
        return result;
    }
    Authoring::WriteDocument loweredDocument;
    loweredDocument.Root().Assign(root);
    std::vector<AssetId> decalTextures;
    std::size_t visited{};
    std::string loweringError;
    if (!LowerDecalTextures(loweredDocument.Root(), textureIdentityRoot, decalTextures, loweringError, visited))
    {
        AddIssue(result, "scene.decalTexture", std::move(loweringError));
        return result;
    }
    const auto loweredRoot = loweredDocument.Root().Read();

    SceneCookProduct product;
    product.sceneAssetId = sceneAssetId;
    product.kind = kind;

    std::vector<AssetId> dependencies;
    std::string failureKey;
    std::string failureValue;
    std::string failureContext;
    Walk walk{product, sceneAssetId, dependencies, failureKey, failureValue, failureContext, false, request.bootstrapReferences};
    walk.Visit(loweredRoot);
    for (const auto& texture : decalTextures)
    {
        walk.AddEdge(texture, product.textureEdges, CookedAssetKind::Texture);
    }
    if (walk.failed)
    {
        AddIssue(result, failureContext, failureKey + "가 canonical UUIDv4가 아니다: " + failureValue);
        return result;
    }

    product.artifactPath = kind == CookedAssetKind::Scene ? MakeDerivedSceneArtifactPath(sceneAssetId)
                                                          : MakeDerivedPrefabArtifactPath(sceneAssetId);
    if (product.artifactPath.empty())
    {
        AddIssue(result, "scene.artifactPath", "scene/prefab GUID가 Derived 경로를 만들지 못했다.");
        return result;
    }

    Authoring::WriteDocument runtimeDocument;
    std::string encodeError;
    if (!BuildRuntimeTree(loweredRoot, runtimeDocument.Root(), product.cookedOverrideValues, encodeError))
    {
        AddIssue(result, "scene.runtimeDocument", std::move(encodeError));
        return result;
    }
    if (!Authoring::EncodeCookedDocument(runtimeDocument.Root().Read(), product.artifactBytes, encodeError))
    {
        AddIssue(result, "scene.cookedDocument", std::move(encodeError));
        return result;
    }

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
} // namespace experiment::cooked
