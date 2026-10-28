#include "../../Utility_Framework/Foundation/EngineOrderedSet.h"
#include "../../Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "ModelSourcePreparation.h"
#include "../../Utility_Framework/Authoring/AuthoringParsedDocument.h"
#include "../Experiment/Import/FbxImporter.h"
#include "../Experiment/Import/GltfImporter.h"

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets
{
    bool ReadModelGeometryImportSettings(std::string_view sidecarText,
        ModelGeometryImportSettings& out, std::string& failure)
    {
        const auto document = Authoring::ParsedDocument::ParseText(std::string(sidecarText), failure);
        if (!document)
        {
            return false;
        }
        ModelGeometryImportSettings value;
        const auto settings = document.Root()["importSettings"];
        if (settings && !settings.IsMap())
        {
            failure = "importSettings: expected a map";
            return false;
        }
        // Source metadata may predate mandatory METEOR topology. Reimport always
        // authors the current policy; old cooked payloads remain rejected.
        value.buildMeshlets = true;
        const auto levels = settings ? settings["lodLevels"] : Authoring::ReadNode{};
        if (levels)
        {
            const auto text = levels.IsScalar() ? levels.AsString() : std::string{};
            if (text.size() != 1u || text[0] < '0' || text[0] > '7')
            {
                failure = "importSettings.lodLevels: expected an integer from 0 to 7";
                return false;
            }
            value.lodLevels = static_cast<std::uint32_t>(text[0] - '0');
        }
        const auto importer = document.Root()["ModelImporter"];
        if (importer && !importer.IsMap())
        {
            failure = "ModelImporter: expected a map";
            return false;
        }
        const auto collider = importer ? importer["CreateMeshCollider"] : Authoring::ReadNode{};
        if (collider)
        {
            const auto text = collider.IsScalar() ? collider.AsString() : std::string{};
            if (text != "true" && text != "false")
            {
                failure = "ModelImporter.CreateMeshCollider: expected true or false";
                return false;
            }
            value.createMeshCollider = text == "true";
        }
        out = value;
        failure.clear();
        return true;
    }

    own::unique_owner<experiment::importer::IAssetImporter> CreateModelSourceImporter(
        const std::filesystem::path& source)
    {
        auto gltf = own::make_unique<experiment::importer::GltfImporter>();
        if (gltf->CanImport(source))
        {
            return gltf;
        }
        auto fbx = own::make_unique<experiment::importer::FbxImporter>();
        if (fbx->CanImport(source))
        {
            return fbx;
        }
        return {};
    }

    bool NormalizeModelStableInputs(ce::dynamic_array<StableKeyElement>& elements, std::string& failure)
    {
        for (auto& element : elements)
        {
            for (std::string* value : { &element.persistentId, &element.name })
            {
                if (value->empty())
                {
                    continue;
                }
                std::string normalized;
                if (!NormalizeUtf8Nfc(*value, normalized, failure))
                {
                    return false;
                }
                *value = std::move(normalized);
            }
        }
        return true;
    }

    bool ReconcileAuthoredModelBindings(const experiment::importer::ImportedScene& scene,
        const ModelSidecarV2& sidecar, ce::dynamic_array<StableKeyAssignment>& out, std::string& failure)
    {
        auto elements = CollectStableKeyElements(scene);
        if (!NormalizeModelStableInputs(elements, failure))
        {
            return false;
        }
        // A source cook cannot mint or retire identities, even temporarily.
        const auto refuseIssuance = [](ce::fixed_array<std::uint8_t, kAuthoringKeyBytes>&, std::string& error)
        {
            error = "New stable identity required; run model authoring reconciliation before BuildAssetSet";
            return false;
        };
        auto keys = DeriveModelStableKeys(elements, sidecar.subAssets, refuseIssuance);
        if (!keys.Succeeded() || keys.assignments.size() != sidecar.subAssets.size())
        {
            failure = "Source inventory needs model authoring reconciliation; BuildAssetSet never changes .meta identities";
            if (!keys.issues.empty())
            {
                failure += ": " + keys.issues.front().message;
            }
            return false;
        }
        ce::engine_ordered_set<ce::pair<SubAssetKind, std::string>> seen;
        for (const auto& assignment : keys.assignments)
        {
            const auto prior = std::ranges::find_if(sidecar.subAssets, [&](const auto& record)
            {
                return record.kind == assignment.kind && record.stableKey == assignment.stableKey;
            });
            if (prior == sidecar.subAssets.end() || !seen.emplace(assignment.kind, assignment.stableKey).second)
            {
                failure = "Source stable keys changed; reconcile the model authoring sidecar before BuildAssetSet";
                return false;
            }
        }
        out = std::move(keys.assignments);
        return true;
    }
}
