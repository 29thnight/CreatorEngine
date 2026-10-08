#include "ModelSourcePreparation.h"
#include "AuthoringParsedDocument.h"
#include "../Experiment/Import/FbxImporter.h"
#include "../Experiment/Import/GltfImporter.h"

#include <algorithm>
#include <set>
#include <utility>

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
        const auto enabled = settings ? settings["buildMeshlets"] : Authoring::ReadNode{};
        if (enabled)
        {
            const auto text = enabled.IsScalar() ? enabled.AsString() : std::string{};
            if (text != "true" && text != "false")
            {
                failure = "importSettings.buildMeshlets: expected true or false";
                return false;
            }
            value.buildMeshlets = text == "true";
        }
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

    bool NormalizeModelStableInputs(std::vector<StableKeyElement>& elements, std::string& failure)
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
        const ModelSidecarV2& sidecar, std::vector<StableKeyAssignment>& out, std::string& failure)
    {
        auto elements = CollectStableKeyElements(scene);
        if (!NormalizeModelStableInputs(elements, failure))
        {
            return false;
        }
        // A source cook cannot mint or retire identities, even temporarily.
        const auto refuseIssuance = [](std::array<std::uint8_t, kAuthoringKeyBytes>&, std::string& error)
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
        std::set<std::pair<SubAssetKind, std::string>> seen;
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
