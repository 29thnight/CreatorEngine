#include "MaterialAssetSetProducer.h"

#include "CookedMaterialProgram.h"
#include "../../Assets/AssetIdentityProfile.h"
#include "AuthoringParsedDocument.h"

#include <exception>
#include <set>
#include <utility>

namespace experiment::cooked
{
    namespace material_asset_set_producer_detail
    {
        bool Fail(std::string& failure, std::string message)
        {
            failure = std::move(message);
            return false;
        }

        std::string Text(std::span<const std::byte> bytes)
        {
            return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
        }

        bool ValidateMeta(std::span<const std::byte> bytes, const AssetId& expected, std::string& failure)
        {
            if ((!IsAssetIdV4(expected) && !assets::IsUuidV8(expected.value)) ||
                bytes.empty() || bytes.size() > kMaterialAssetSetMetaMaxBytes)
            {
                return Fail(failure, "Material recipe requires a UUIDv4/v8 identity and a bounded captured sidecar.");
            }
            const auto document = Authoring::ParsedDocument::ParseText(Text(bytes), failure);
            if (!document)
            {
                return false;
            }
            const auto root = document.Root();
            if (!root.IsMap())
            {
                return Fail(failure, "Material sidecar root must be a mapping.");
            }
            std::set<std::string> keys;
            for (const auto entry : root.Map())
            {
                if (!entry.key.IsScalar() || !keys.insert(entry.key.AsString()).second)
                {
                    return Fail(failure, "Material sidecar contains duplicate or nonscalar keys.");
                }
            }
            const auto guid = root["guid"];
            AssetId parsed;
            if (!guid.IsScalar() ||
                (!TryParseCanonicalAssetId(guid.Scalar(), parsed) &&
                    !assets::TryParseCanonicalUuidV8(guid.Scalar(), parsed.value)) || parsed != expected)
            {
                return Fail(failure, "Material sidecar GUID must be canonical and match the requested UUIDv4/v8.");
            }
            return true;
        }

        void AppendSize(std::vector<std::byte>& bytes, std::uint64_t value)
        {
            for (unsigned shift = 0; shift < 64; shift += 8)
            {
                bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
            }
        }

        void AppendText(std::vector<std::byte>& bytes, std::string_view value)
        {
            AppendSize(bytes, value.size());
            for (const char character : value)
            {
                bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
            }
        }

        bool AppendInput(std::vector<std::byte>& inventory, std::string_view role,
            std::span<const std::byte> bytes, std::string& failure)
        {
            Sha256Digest digest;
            if (!ComputeSha256(bytes, digest, failure))
            {
                return false;
            }
            AppendText(inventory, role);
            AppendSize(inventory, bytes.size());
            for (const auto value : digest)
            {
                inventory.push_back(static_cast<std::byte>(value));
            }
            return true;
        }

        bool InputDigest(std::string_view domain, std::span<const std::byte> source,
            std::span<const std::byte> meta, std::span<const std::byte> verified,
            Sha256Digest& out, std::string& failure)
        {
            std::vector<std::byte> inventory;
            AppendText(inventory, domain);
            AppendSize(inventory, verified.empty() ? 2u : 3u);
            if (!AppendInput(inventory, "source", source, failure) ||
                !AppendInput(inventory, "meta", meta, failure))
            {
                return false;
            }
            if (!verified.empty() && !AppendInput(inventory, "verified-program", verified, failure))
            {
                return false;
            }
            return ComputeSha256(inventory, out, failure);
        }
    }

    MaterialAssetSetCookResult BuildMaterialAssetSetProduct(const MaterialAssetSetCookRequest& request)
    {
        MaterialAssetSetCookResult result;
        try
        {
            if (request.sourceBytes.empty() || request.sourceBytes.size() > kMaterialDocumentMaxBytes)
            {
                result.failure = "Material source is empty or exceeds its byte budget.";
                return result;
            }
            if (!material_asset_set_producer_detail::ValidateMeta(request.metaBytes, request.materialAssetId,
                result.failure))
            {
                return result;
            }
            const auto source = Authoring::ParsedDocument::ParseText(
                material_asset_set_producer_detail::Text(request.sourceBytes), result.failure);
            if (!source)
            {
                return result;
            }
            if (!source.Root().IsMap() || !source.Root()["lattice_material"])
            {
                result.failure = "Unsupported Material source shape: this recipe requires a Lattice InstanceDocument; "
                    "legacy reflected Material and experiment::Material sources need their own typed recipes.";
                return result;
            }
            material_graph::InstanceDocument document;
            if (!material_graph::ReadInstanceDocument(source.Root(), document, result.failure))
            {
                return result;
            }
            if (document.materialId != request.materialAssetId)
            {
                result.failure = "Material document identity differs from its captured sidecar.";
                return result;
            }
            MaterialAssetSetProduct product;
            product.asset = { { request.materialAssetId, {} }, CookedAssetKind::Material };
            if (!EncodeMaterialAssetSetDocument(document, product.artifactBytes, product.dependencies, result.failure))
            {
                return result;
            }
            product.representation = kMaterialDocumentRepresentation;
            product.schemaVersion = kMaterialArtifactVersion;
            product.extension = ".asset";
            if (!material_asset_set_producer_detail::InputDigest("material-asset-set-source-v1", request.sourceBytes,
                request.metaBytes, {}, result.sourceInputsSha256, result.failure))
            {
                return result;
            }
            result.product = std::move(product);
            return result;
        }
        catch (const std::exception& exception)
        {
            result.failure = exception.what();
            return result;
        }
    }

    MaterialAssetSetCookResult BuildMaterialProgramAssetSetProduct(const MaterialProgramAssetSetCookRequest& request)
    {
        MaterialAssetSetCookResult result;
        try
        {
            if (request.sourceBytes.empty() || request.sourceBytes.size() > kMaterialGraphSourceMaxBytes ||
                request.verifiedProgramBytes.empty() ||
                request.verifiedProgramBytes.size() > kMaterialProgramAssetSetMaxBytes)
            {
                result.failure = "MaterialProgram needs bounded graph source and explicit verified program bytes.";
                return result;
            }
            if (!material_asset_set_producer_detail::ValidateMeta(request.metaBytes, request.programAssetId,
                result.failure))
            {
                return result;
            }
            const auto graph = LX::LXMaterialArchive::Read(
                material_asset_set_producer_detail::Text(request.sourceBytes),
                LX::CreateMaterialDefinitions(), &result.failure);
            if (!graph)
            {
                if (result.failure.empty())
                {
                    result.failure = "Cannot decode the current material graph source.";
                }
                return result;
            }
            material_graph::CookedProgram verified;
            if (!material_graph::ReadCookedProgram(
                { reinterpret_cast<const std::uint8_t*>(request.verifiedProgramBytes.data()),
                    request.verifiedProgramBytes.size() }, request.budget, verified, result.failure))
            {
                return result;
            }
            MaterialAssetSetProduct product;
            product.asset = { { request.programAssetId, {} }, CookedAssetKind::MaterialProgram };
            if (!CollectMaterialProgramAssetSetDependencies(verified.product, request.programAssetId,
                product.dependencies, result.failure))
            {
                return result;
            }
            // Reuse the canonical graph regeneration/equality check and byte
            // writer only. Its legacy untyped manifest edges are never used.
            MaterialProgramCookProduct regenerated;
            if (!BuildMaterialProgramCookProduct(request.programAssetId, *graph, verified.product, request.budget,
                regenerated, result.failure))
            {
                return result;
            }
            if (regenerated.artifactBytes.empty() || regenerated.artifactBytes.size() > kMaterialProgramAssetSetMaxBytes)
            {
                result.failure = "MaterialProgram output exceeds its byte budget.";
                return result;
            }
            product.artifactBytes = std::move(regenerated.artifactBytes);
            product.representation = kMaterialProgramRepresentation;
            product.schemaVersion = kMaterialProgramArtifactVersion;
            product.extension = ".lxmaterial";
            if (!material_asset_set_producer_detail::InputDigest("material-program-asset-set-source-v1",
                request.sourceBytes, request.metaBytes, request.verifiedProgramBytes,
                result.sourceInputsSha256, result.failure))
            {
                return result;
            }
            result.product = std::move(product);
            return result;
        }
        catch (const std::exception& exception)
        {
            result.failure = exception.what();
            return result;
        }
    }
}
