#include "ModelIdentityRefresher.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/Cooked/ModelCookIdentity.h"
#include "Experiment/Import/ImporterModelDecoder.h"
#include "TypeTrait.h"

#include <Windows.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <process.h>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>

namespace asset_cooker
{
    namespace
    {
        namespace ck = experiment::cooked;
        namespace im = experiment::importer;

        struct PendingSidecar final
        {
            std::filesystem::path source{};
            std::filesystem::path path{};
            std::filesystem::path temporaryPath{};
            std::string original{};
            std::string refreshed{};
            experiment::AssetId modelAssetId{};
            std::size_t materialCount{};
            std::size_t embeddedTextureCount{};
        };

        [[nodiscard]] bool IsContainedPath(const std::filesystem::path& root,
            const std::filesystem::path& child)
        {
            std::error_code error;
            const std::filesystem::path relative =
                std::filesystem::relative(child, root, error);
            if (error || relative.empty() || relative.is_absolute()) return false;
            for (const std::filesystem::path& part : relative)
            {
                if (part == "..") return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadTextFile(const std::filesystem::path& path,
            std::string& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff bytes = stream.tellg();
            if (bytes < 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(bytes));
            if (!out.empty())
                stream.read(out.data(), static_cast<std::streamsize>(out.size()));
            return stream.good() || stream.eof();
        }

        [[nodiscard]] bool WriteTextFile(const std::filesystem::path& path,
            std::string_view text)
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            if (!text.empty())
                stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.flush();
            return stream.good();
        }

        [[nodiscard]] bool ReserveIdentity(const experiment::AssetId& id,
            std::string context, std::vector<experiment::AssetId>& usedIds,
            std::vector<std::string>& usedContexts, std::string& failure)
        {
            const auto found = std::ranges::find(usedIds, id);
            if (found != usedIds.end())
            {
                const std::size_t index = static_cast<std::size_t>(
                    std::distance(usedIds.begin(), found));
                failure = "asset root UUIDv4가 중복됐다: "
                    + Uuid::ToString(id.value) + " (" + usedContexts[index]
                    + ", " + context + ")";
                return false;
            }
            usedIds.push_back(id);
            usedContexts.push_back(std::move(context));
            return true;
        }

        [[nodiscard]] bool ReserveExistingAssetIds(
            const std::filesystem::path& assetRoot,
            std::vector<experiment::AssetId>& usedIds, std::string& failure)
        {
            std::vector<std::string> usedContexts;
            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(
                assetRoot, std::filesystem::directory_options::none, error);
            const std::filesystem::recursive_directory_iterator end;
            if (error)
            {
                failure = "asset root meta를 열거할 수 없다: " + error.message();
                return false;
            }

            for (; iterator != end; iterator.increment(error))
            {
                if (error)
                {
                    failure = "asset root meta 열거 중 실패했다: " + error.message();
                    return false;
                }
                if (!iterator->is_regular_file(error) || error
                    || iterator->path().extension() != ".meta")
                {
                    if (error)
                    {
                        failure = "asset root entry를 검사할 수 없다: "
                            + error.message();
                        return false;
                    }
                    continue;
                }

                std::string text;
                if (!ReadTextFile(iterator->path(), text))
                {
                    failure = "asset sidecar를 읽을 수 없다: "
                        + iterator->path().string();
                    return false;
                }

                experiment::AssetId topLevel{};
                std::vector<ck::ModelIdentityIssue> issues;
                if (!ck::ReadAssetIdFromMeta(text, topLevel, issues))
                {
                    failure = "asset sidecar 최상위 UUIDv4를 읽을 수 없다: "
                        + iterator->path().string();
                    if (!issues.empty()) failure += " (" + issues.front().message + ")";
                    return false;
                }
                if (!ReserveIdentity(topLevel, iterator->path().string(),
                    usedIds, usedContexts, failure))
                {
                    return false;
                }

                try
                {
                    const YAML::Node root = YAML::Load(text);
                    if (!root["subAssets"]) continue;
                }
                catch (const YAML::Exception& exception)
                {
                    failure = iterator->path().string() + ": " + exception.what();
                    return false;
                }

                ck::ModelCookIdentity modelIdentity;
                issues.clear();
                if (!ck::ReadModelCookIdentity(text, modelIdentity, issues))
                {
                    failure = "기존 model subasset identity가 손상됐다: "
                        + iterator->path().string();
                    if (!issues.empty()) failure += " (" + issues.front().message + ")";
                    return false;
                }
                const auto reserveSubAssets = [&](
                    const std::vector<ck::ModelSubAssetIdentity>& identities,
                    std::string_view kind)
                {
                    for (const ck::ModelSubAssetIdentity& identity : identities)
                    {
                        if (!ReserveIdentity(identity.assetId,
                            iterator->path().string() + ":" + std::string(kind)
                                + ":" + identity.sourceKey,
                            usedIds, usedContexts, failure))
                        {
                            return false;
                        }
                    }
                    return true;
                };
                if (!reserveSubAssets(modelIdentity.materials, "material")
                    || !reserveSubAssets(
                        modelIdentity.embeddedTextures, "embeddedTexture"))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] experiment::AssetId CreateUniqueAssetId(
            std::vector<experiment::AssetId>& usedIds)
        {
            for (;;)
            {
                const FileGuid generated = FileGuid::CreateRandomV4();
                const experiment::AssetId candidate{ generated.m_guid };
                if (std::ranges::find(usedIds, candidate) == usedIds.end())
                {
                    usedIds.push_back(candidate);
                    return candidate;
                }
            }
        }

        [[nodiscard]] ck::ModelSubAssetIdentity& AddIdentity(
            std::vector<ck::ModelSubAssetIdentity>& target,
            std::string_view sourceKey, std::string_view name,
            std::vector<experiment::AssetId>& usedIds)
        {
            const auto found = std::ranges::find(target, sourceKey,
                &ck::ModelSubAssetIdentity::sourceKey);
            if (found != target.end()) return *found;

            ck::ModelSubAssetIdentity identity;
            identity.sourceKey = sourceKey;
            identity.name = name;
            identity.assetId = CreateUniqueAssetId(usedIds);
            target.push_back(std::move(identity));
            return target.back();
        }

        [[nodiscard]] bool DiscoverIdentity(const std::filesystem::path& source,
            const experiment::AssetId& modelAssetId,
            std::vector<experiment::AssetId>& usedIds,
            ck::ModelCookIdentity& outIdentity, std::string& failure)
        {
            ck::ModelCookIdentity discovered;
            discovered.modelAssetId = modelAssetId;

            im::ImporterDecoderOptions options{};
            options.conversion.modelAssetId = modelAssetId;
            options.conversion.resolveShaderAsset =
                [modelAssetId](const im::ImportedMaterial&, std::size_t)
                {
                    return modelAssetId;
                };
            options.conversion.resolveMaterialAsset =
                [&](const im::ImportedMaterial& material, std::size_t)
                {
                    return AddIdentity(discovered.materials,
                        material.sourceKey, material.name, usedIds).assetId;
                };
            options.conversion.resolveTextureAsset =
                [&](const im::ImportedTexture& texture)
                {
                    if (!texture.IsEmbedded()) return modelAssetId;
                    return AddIdentity(discovered.embeddedTextures,
                        texture.sourceKey, texture.name, usedIds).assetId;
                };

            im::ImporterModelDecoder decoder(std::move(options));
            experiment::ModelLoadRequest request{};
            request.sourcePath = source;
            request.sourcePreference = experiment::ModelSourcePreference::SourceOnly;
            experiment::ModelDecodeResult decoded = decoder.Decode(request);
            if (!decoded.draft.has_value())
            {
                failure = "model import가 실패했다: " + source.string();
                if (!decoded.issues.empty())
                {
                    failure += " (" + decoded.issues.front().context + ": "
                        + decoded.issues.front().message + ")";
                }
                return false;
            }

            outIdentity = std::move(discovered);
            return true;
        }

        [[nodiscard]] YAML::Node MakeSubAssetSequence(
            const std::vector<ck::ModelSubAssetIdentity>& identities)
        {
            YAML::Node sequence(YAML::NodeType::Sequence);
            for (const ck::ModelSubAssetIdentity& identity : identities)
            {
                YAML::Node entry(YAML::NodeType::Map);
                entry["key"] = identity.sourceKey;
                if (!identity.name.empty()) entry["name"] = identity.name;
                entry["guid"] = Uuid::ToString(identity.assetId.value);
                sequence.push_back(entry);
            }
            return sequence;
        }

        [[nodiscard]] bool BuildRefreshedSidecar(std::string_view original,
            const ck::ModelCookIdentity& identity,
            std::string& out, std::string& failure)
        {
            try
            {
                YAML::Node root = YAML::Load(std::string(original));
                if (!root.IsMap())
                {
                    failure = "model sidecar root가 map이 아니다.";
                    return false;
                }

                YAML::Node subAssets(YAML::NodeType::Map);
                subAssets["schemaVersion"] = 1u;
                subAssets["materials"] = MakeSubAssetSequence(identity.materials);
                subAssets["embeddedTextures"] =
                    MakeSubAssetSequence(identity.embeddedTextures);
                root["subAssets"] = subAssets;

                YAML::Emitter emitter;
                emitter.SetIndent(2);
                emitter << root;
                if (!emitter.good())
                {
                    failure = "model sidecar YAML emit이 실패했다.";
                    return false;
                }
                out.assign(emitter.c_str(), emitter.size());
                out.push_back('\n');
            }
            catch (const YAML::Exception& exception)
            {
                failure = exception.what();
                return false;
            }

            ck::ModelCookIdentity reparsed;
            std::vector<ck::ModelIdentityIssue> issues;
            if (!ck::ReadModelCookIdentity(out, reparsed, issues)
                || reparsed.modelAssetId != identity.modelAssetId
                || reparsed.materials.size() != identity.materials.size()
                || reparsed.embeddedTextures.size()
                    != identity.embeddedTextures.size())
            {
                failure = "재생성한 model sidecar 자기 검증이 실패했다.";
                if (!issues.empty()) failure += " (" + issues.front().message + ")";
                return false;
            }
            return true;
        }

        [[nodiscard]] std::filesystem::path MakeTemporaryPath(
            const std::filesystem::path& sidecar, std::size_t index,
            std::string_view purpose)
        {
            const auto ticks = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            std::filesystem::path temporary = sidecar;
            temporary += "." + std::string(purpose) + "-"
                + std::to_string(_getpid()) + "-" + std::to_string(ticks)
                + "-" + std::to_string(index);
            return temporary;
        }

        void RemoveTemporaryFiles(const std::vector<PendingSidecar>& pending)
        {
            for (const PendingSidecar& sidecar : pending)
            {
                std::error_code ignored;
                std::filesystem::remove(sidecar.temporaryPath, ignored);
            }
        }

        [[nodiscard]] bool ReplaceFile(const std::filesystem::path& source,
            const std::filesystem::path& destination)
        {
            return 0 != ::MoveFileExW(source.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }

        [[nodiscard]] bool RollBack(
            const std::vector<PendingSidecar>& pending, std::size_t committed)
        {
            bool restored = true;
            for (std::size_t index = 0; index < committed; ++index)
            {
                const std::filesystem::path rollback = MakeTemporaryPath(
                    pending[index].path, index, "rollback");
                if (!WriteTextFile(rollback, pending[index].original)
                    || !ReplaceFile(rollback, pending[index].path))
                {
                    restored = false;
                    std::error_code ignored;
                    std::filesystem::remove(rollback, ignored);
                }
            }
            return restored;
        }
    }

    bool RefreshModelIdentities(const std::filesystem::path& requestedAssetRoot,
        const std::vector<std::filesystem::path>& models,
        ModelIdentityRefreshSummary& outSummary, std::string& outFailure)
    {
        std::error_code error;
        const std::filesystem::path assetRoot =
            std::filesystem::weakly_canonical(requestedAssetRoot, error);
        if (error || assetRoot.empty()
            || !std::filesystem::is_directory(assetRoot, error))
        {
            outFailure = "asset root가 유효한 디렉터리가 아니다.";
            return false;
        }

        std::vector<PendingSidecar> pending;
        pending.reserve(models.size());
        std::set<std::filesystem::path> uniqueModels;
        std::vector<experiment::AssetId> usedIds;
        if (!ReserveExistingAssetIds(assetRoot, usedIds, outFailure)) return false;

        for (const std::filesystem::path& requestedModel : models)
        {
            error.clear();
            const std::filesystem::path source =
                std::filesystem::weakly_canonical(requestedModel, error);
            if (error || source.empty()
                || !std::filesystem::is_regular_file(source, error)
                || !IsContainedPath(assetRoot, source))
            {
                outFailure = "model source가 asset root 안의 파일이 아니다: "
                    + requestedModel.string();
                return false;
            }
            if (!uniqueModels.insert(source).second)
            {
                outFailure = "중복 model source다: " + source.string();
                return false;
            }

            PendingSidecar item;
            item.source = source;
            item.path = item.source;
            item.path += ".meta";
            if (!ReadTextFile(item.path, item.original))
            {
                outFailure = "model sidecar를 읽을 수 없다: " + item.path.string();
                return false;
            }

            std::vector<ck::ModelIdentityIssue> identityIssues;
            if (!ck::ReadAssetIdFromMeta(
                item.original, item.modelAssetId, identityIssues))
            {
                outFailure = "model 최상위 UUIDv4를 읽을 수 없다: "
                    + item.path.string();
                if (!identityIssues.empty())
                    outFailure += " (" + identityIssues.front().message + ")";
                return false;
            }
            pending.push_back(std::move(item));
        }

        // 모든 기존 model identity를 먼저 예약한 뒤 subasset UUIDv4를 발급한다.
        // 따라서 이 batch에서 새 ID가 뒤쪽 model의 상위 ID와 충돌할 수도 없다.
        for (PendingSidecar& item : pending)
        {
            ck::ModelCookIdentity identity;
            if (!DiscoverIdentity(item.source, item.modelAssetId,
                usedIds, identity, outFailure))
                return false;
            if (!BuildRefreshedSidecar(
                item.original, identity, item.refreshed, outFailure))
            {
                outFailure = item.path.string() + ": " + outFailure;
                return false;
            }
            item.materialCount = identity.materials.size();
            item.embeddedTextureCount = identity.embeddedTextures.size();
        }

        // 모든 import/YAML 검증이 끝난 다음에야 source tree에 손댄다.
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            pending[index].temporaryPath = MakeTemporaryPath(
                pending[index].path, index, "identity-refresh");
            if (std::filesystem::exists(pending[index].temporaryPath, error)
                || !WriteTextFile(pending[index].temporaryPath,
                    pending[index].refreshed))
            {
                RemoveTemporaryFiles(pending);
                outFailure = "임시 sidecar를 쓸 수 없다: "
                    + pending[index].temporaryPath.string();
                return false;
            }
        }

        std::size_t committed = 0u;
        for (; committed < pending.size(); ++committed)
        {
            if (!ReplaceFile(pending[committed].temporaryPath,
                pending[committed].path))
            {
                const DWORD replaceError = ::GetLastError();
                const bool rolledBack = RollBack(pending, committed);
                RemoveTemporaryFiles(pending);
                outFailure = "sidecar transaction 게시가 실패했다 (Win32 "
                    + std::to_string(replaceError) + ")";
                if (!rolledBack)
                    outFailure += "; 이미 게시된 sidecar rollback도 실패했다.";
                return false;
            }
        }

        ModelIdentityRefreshSummary summary;
        summary.models = pending.size();
        for (const PendingSidecar& item : pending)
        {
            summary.materials += item.materialCount;
            summary.embeddedTextures += item.embeddedTextureCount;
        }
        outSummary = summary;
        return true;
    }
}
