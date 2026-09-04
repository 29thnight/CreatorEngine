#include "ModelAssetAuthoringTransaction.h"

#include "ModelSidecarV2.h"
#include "../Experiment/Cooked/CookSupport.h"
#include "../Experiment/Cooked/CookedModelCodec.h"
#include "../Experiment/Cooked/TextureCookProducer.h"
#include "../Experiment/Import/FbxImporter.h"
#include "../Experiment/Import/GltfImporter.h"
#include "../Experiment/Import/SceneToModelDraft.h"

#include "AuthoringParsedDocument.h"
#include "AuthoringWriteNode.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <process.h>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace assets
{
    namespace
    {
        namespace im = experiment::importer;
        namespace ck = experiment::cooked;

        struct Mbc3PreparedTexture final
        {
            Uuid::Uuid16 assetId{};
            std::filesystem::path relativePath{};
            std::vector<std::byte> bytes{};
            std::string fingerprint{};
        };

        struct Mbc3Cleanup final
        {
            std::filesystem::path stage{};
            std::filesystem::path sidecarTemporary{};

            ~Mbc3Cleanup()
            {
                std::error_code ignored;
                if (!stage.empty()) std::filesystem::remove_all(stage, ignored);
                if (!sidecarTemporary.empty())
                    std::filesystem::remove(sidecarTemporary, ignored);
            }

            void Release() noexcept
            {
                stage.clear();
                sidecarTemporary.clear();
            }
        };

        void Mbc3AddIssue(ModelAssetAuthoringResult& result,
            std::string stage, std::string message)
        {
            result.issues.push_back({ std::move(stage), std::move(message) });
        }

        [[nodiscard]] bool Mbc3ReadText(const std::filesystem::path& path,
            std::string& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size < 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(size));
            if (!out.empty())
                stream.read(out.data(), static_cast<std::streamsize>(out.size()));
            return stream.good() || stream.eof();
        }

        [[nodiscard]] bool Mbc3ReadBytes(const std::filesystem::path& path,
            std::vector<std::byte>& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size < 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(size));
            if (!out.empty())
                stream.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(out.size()));
            return stream.good() || stream.eof();
        }

        [[nodiscard]] bool Mbc3WriteText(const std::filesystem::path& path,
            std::string_view text, std::string& failure)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                failure = "parent directory를 만들 수 없다: " + error.message();
                return false;
            }
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                failure = "파일을 열 수 없다: " + path.string();
                return false;
            }
            if (!text.empty())
                stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.flush();
            if (!stream.good())
            {
                failure = "파일 쓰기가 실패했다: " + path.string();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool Mbc3WriteBytes(const std::filesystem::path& path,
            std::span<const std::byte> bytes, std::string& failure)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                failure = "parent directory를 만들 수 없다: " + error.message();
                return false;
            }
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                failure = "파일을 열 수 없다: " + path.string();
                return false;
            }
            if (!bytes.empty())
                stream.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            stream.flush();
            if (!stream.good())
            {
                failure = "파일 쓰기가 실패했다: " + path.string();
                return false;
            }
            return true;
        }

        [[nodiscard]] std::string Mbc3Fingerprint(
            std::span<const std::byte> bytes)
        {
            return MakeSourceFingerprint(std::span<const std::uint8_t>{
                reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() });
        }

        [[nodiscard]] std::string Mbc3Fingerprint(std::string_view text)
        {
            return MakeSourceFingerprint(std::span<const std::uint8_t>{
                reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
        }

        class Mbc3ProjectWriterLock final
        {
        public:
            ~Mbc3ProjectWriterLock()
            {
                if (nullptr == handle_) return;
                if (owned_) ::ReleaseMutex(handle_);
                ::CloseHandle(handle_);
            }

            [[nodiscard]] bool Acquire(const std::filesystem::path& assetRoot,
                std::string& failure)
            {
                const std::wstring material = assetRoot.native();
                const std::span<const std::byte> bytes{
                    reinterpret_cast<const std::byte*>(material.data()),
                    material.size() * sizeof(wchar_t) };
                const std::string fingerprint = Mbc3Fingerprint(bytes);
                const std::string suffix = fingerprint.starts_with("sha256:")
                    ? fingerprint.substr(7u) : fingerprint;
                const std::wstring mutexName = L"Local\\CreatorEngine.ModelAuthoring."
                    + std::wstring(suffix.begin(), suffix.end());
                handle_ = ::CreateMutexW(nullptr, FALSE, mutexName.c_str());
                if (nullptr == handle_)
                {
                    failure = "project model writer mutex를 만들 수 없다 (Win32 "
                        + std::to_string(::GetLastError()) + ").";
                    return false;
                }
                const DWORD wait = ::WaitForSingleObject(handle_, 30000u);
                if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
                {
                    failure = wait == WAIT_TIMEOUT
                        ? "다른 model authoring transaction이 30초 넘게 writer를 점유했다."
                        : "project model writer mutex를 획득할 수 없다 (Win32 "
                            + std::to_string(::GetLastError()) + ").";
                    return false;
                }
                owned_ = true;
                return true;
            }

        private:
            HANDLE handle_{};
            bool owned_{};
        };

        [[nodiscard]] std::filesystem::path Mbc3TemporarySibling(
            const std::filesystem::path& path, std::string_view purpose)
        {
            const auto ticks = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            std::filesystem::path result = path;
            result += "." + std::string(purpose) + "-"
                + std::to_string(_getpid()) + "-" + std::to_string(ticks);
            return result;
        }

        [[nodiscard]] bool Mbc3ReplaceFile(const std::filesystem::path& from,
            const std::filesystem::path& to)
        {
            return 0 != ::MoveFileExW(from.c_str(), to.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }

        [[nodiscard]] bool Mbc3PublishDirectory(const std::filesystem::path& from,
            const std::filesystem::path& to)
        {
            return 0 != ::MoveFileExW(from.c_str(), to.c_str(),
                MOVEFILE_WRITE_THROUGH);
        }

        [[nodiscard]] bool Mbc3Inject(const ModelAssetAuthoringRequest& request,
            ModelAuthoringFailurePoint point, ModelAssetAuthoringResult& result,
            std::string_view stage)
        {
            if (request.failurePoint != point) return false;
            Mbc3AddIssue(result, std::string(stage),
                "회귀 검사용 실패가 주입됐다.");
            return true;
        }

        [[nodiscard]] bool Mbc3IsLegacyModelSidecar(std::string_view text)
        {
            std::string parseError;
            const Authoring::ParsedDocument document =
                Authoring::ParsedDocument::ParseText(std::string{ text }, parseError);
            if (!document || !document.Root().IsMap()) return false;
            const Authoring::ReadNode root = document.Root();
            if (root["schemaVersion"] || root["assetId"] || root["identityProfile"])
                return false;
            const Authoring::ReadNode guid = root["guid"];
            if (!guid || !guid.IsScalar()) return false;
            const Authoring::ReadNode subAssets = root["subAssets"];
            if (!subAssets) return true;
            if (!subAssets.IsMap()) return false;
            const Authoring::ReadNode schema = subAssets["schemaVersion"];
            return schema && schema.IsScalar() && schema.As(0u) == 1u;
        }

        [[nodiscard]] bool Mbc3DeclaresSchemaV2(std::string_view text) noexcept
        {
            while (!text.empty())
            {
                const std::size_t newline = text.find('\n');
                std::string_view line = text.substr(0u, newline);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1u);
                constexpr std::string_view prefix = "schemaVersion:";
                if (line.starts_with(prefix))
                {
                    line.remove_prefix(prefix.size());
                    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                        line.remove_prefix(1u);
                    while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
                        line.remove_suffix(1u);
                    return line == "2";
                }
                if (newline == std::string_view::npos) break;
                text.remove_prefix(newline + 1u);
            }
            return false;
        }

        [[nodiscard]] std::unique_ptr<im::IAssetImporter> Mbc3CreateImporter(
            const std::filesystem::path& source)
        {
            auto gltf = std::make_unique<im::GltfImporter>();
            if (gltf->CanImport(source)) return gltf;
            auto fbx = std::make_unique<im::FbxImporter>();
            if (fbx->CanImport(source)) return fbx;
            return {};
        }

        [[nodiscard]] bool Mbc3NormalizeStableInputs(
            std::vector<StableKeyElement>& elements, std::string& failure)
        {
            for (StableKeyElement& element : elements)
            {
                for (std::string* value : { &element.persistentId, &element.name })
                {
                    if (value->empty()) continue;
                    std::string normalized;
                    if (!NormalizeUtf8Nfc(*value, normalized, failure)) return false;
                    *value = std::move(normalized);
                }
            }
            return true;
        }

        [[nodiscard]] bool Mbc3ReadExternalAssetId(
            const std::filesystem::path& assetRoot,
            const std::filesystem::path& source,
            experiment::AssetId& out, std::string& failure)
        {
            std::error_code error;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(source, error);
            if (error || canonical.empty()
                || !ck::IsContainedPath(assetRoot, canonical))
            {
                failure = "외부 texture가 asset root 밖이거나 없다: "
                    + source.string();
                return false;
            }
            std::filesystem::path meta = canonical;
            meta += ".meta";
            return ck::ReadMetaAssetId(meta, out, failure);
        }

        // ── corpus sidecar 파싱 캐시 (MBC11 §8.4 B1) ──
        //
        // 아래 충돌 검사가 저작마다 asset root 전체를 다시 순회하며 schema v2
        // sidecar를 **전부 재파싱**하고 있었다 — Prim 하나를 저작하는 데
        // `corpus-collision-scan` 6.6 ms(사본 345파일 순회 + sidecar 14개 파싱)로,
        // 소스 디코드가 1 ms인 자산에서는 이 고정분이 B1 예산 초과의 본체였다
        // (기준선 §4.5).
        //
        // 순회 자체는 남긴다 — 새로 생긴 sidecar를 봐야 충돌을 잡는다. 캐시하는
        // 것은 파싱 결과뿐이고 지문은 (크기, 마지막 쓰기 시각)이다. 다른 프로세스가
        // 고쳐도 지문이 달라 다시 읽는다. 폐포 검증은 캐시된 문서에도 매번 다시
        // 돈다 — `ValidateModelSidecarV2Closure`는 epoch header를 인자로 받으므로
        // 캐시가 그 판정을 대신하면 header가 바뀐 뒤에도 옛 판정을 재사용하게 된다.
        // (재검증은 짧은 입력 SHA-256 수십 건이라 I/O가 아니다.)
        struct Mbc3CorpusCacheEntry final
        {
            std::uintmax_t size{};
            std::filesystem::file_time_type writeTime{};
            bool schemaV2{ false };
            ModelSidecarV2 document{};
        };

        [[nodiscard]] std::mutex& Mbc3CorpusCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        // 프로젝트(asset root)마다 한 벌. 저작 transaction은 프로젝트 writer
        // mutex를 들고 있으므로 같은 프로젝트끼리는 이미 직렬화돼 있지만, asset
        // root가 다른 두 프로젝트는 서로 다른 mutex를 들고 이 표를 동시에 만진다.
        [[nodiscard]] std::map<std::string, Mbc3CorpusCacheEntry>& Mbc3CorpusCacheFor(
            const std::filesystem::path& assetRoot)
        {
            static std::map<std::string,
                std::map<std::string, Mbc3CorpusCacheEntry>> caches;
            return caches[assetRoot.lexically_normal().generic_string()];
        }

        [[nodiscard]] bool Mbc3ValidateCorpusCollision(
            const std::filesystem::path& assetRoot,
            const std::filesystem::path& candidateSidecarPath,
            const IdentityEpochHeader& header, const ModelSidecarV2& candidate,
            std::string& failure)
        {
            std::map<Uuid::Uuid16, std::string> identities;
            const auto addIdentity = [&](const Uuid::Uuid16& id,
                std::string context) -> bool
                {
                    const auto [found, inserted] = identities.emplace(id, context);
                    if (inserted) return true;
                    failure = "UUIDv8 corpus collision: " + Uuid::ToString(id)
                        + " (" + context + " vs " + found->second + ")";
                    return false;
                };
            const auto addDocument = [&](const ModelSidecarV2& document,
                const std::filesystem::path& path) -> bool
                {
                    const std::string base = path.generic_string();
                    if (!addIdentity(document.assetId, base + "#model")) return false;
                    for (const ModelSubAssetRecord& record : document.subAssets)
                    {
                        if (!addIdentity(record.assetId, base + "#"
                            + std::string(ToKindName(record.kind)) + "/"
                            + record.stableKey)) return false;
                    }
                    return true;
                };

            const std::lock_guard<std::mutex> cacheGuard(Mbc3CorpusCacheMutex());
            std::map<std::string, Mbc3CorpusCacheEntry>& cache =
                Mbc3CorpusCacheFor(assetRoot);
            std::set<std::string> seen;

            std::error_code error;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                assetRoot, std::filesystem::directory_options::skip_permission_denied,
                error))
            {
                if (error)
                {
                    failure = "model sidecar corpus를 순회하지 못했다: "
                        + error.message();
                    return false;
                }
                if (!entry.is_regular_file(error) || error
                    || entry.path().extension() != ".meta"
                    || entry.path() == candidateSidecarPath)
                {
                    error.clear();
                    continue;
                }
                std::filesystem::path target = entry.path();
                target.replace_extension();
                if (!IsModelAuthoringSource(target)) continue;

                const std::string cacheKey = entry.path().generic_string();
                seen.insert(cacheKey);
                std::error_code stat;
                const std::uintmax_t size = entry.file_size(stat);
                const std::filesystem::file_time_type writeTime = stat
                    ? std::filesystem::file_time_type{} : entry.last_write_time(stat);
                const bool fingerprinted = !stat;
                error.clear();

                const auto found = cache.find(cacheKey);
                const Mbc3CorpusCacheEntry* cached =
                    (fingerprinted && found != cache.end()
                        && found->second.size == size
                        && found->second.writeTime == writeTime)
                    ? &found->second : nullptr;

                Mbc3CorpusCacheEntry parsedEntry;
                if (nullptr == cached)
                {
                    std::string text;
                    // 읽기 실패는 캐시하지 않는다 — 다음 저작이 다시 시도한다.
                    if (!Mbc3ReadText(entry.path(), text)) continue;
                    std::string parseError;
                    const Authoring::ParsedDocument parsed =
                        Authoring::ParsedDocument::ParseText(text, parseError);
                    if (!parsed || !parsed.Root().IsMap())
                    {
                        if (Mbc3DeclaresSchemaV2(text))
                        {
                            failure = "schema v2 model sidecar corpus를 파싱하지 못했다: "
                                + entry.path().string() + " (" + parseError + ")";
                            return false;
                        }
                    }
                    else if (parsed.Root()["schemaVersion"].As(0u) == 2u)
                    {
                        std::vector<SidecarIssue> issues;
                        if (!ReadModelSidecarV2(text, parsedEntry.document, issues))
                        {
                            failure = "schema v2 model sidecar corpus가 유효하지 않다: "
                                + entry.path().string();
                            if (!issues.empty())
                                failure += " (" + issues.front().message + ")";
                            return false;
                        }
                        parsedEntry.schemaV2 = true;
                    }
                    parsedEntry.size = size;
                    parsedEntry.writeTime = writeTime;
                    if (fingerprinted) cached = &(cache[cacheKey] = std::move(parsedEntry));
                    else cached = &parsedEntry;
                }
                if (!cached->schemaV2) continue;

                std::vector<SidecarIssue> issues;
                if (!ValidateModelSidecarV2Closure(cached->document, header, issues))
                {
                    failure = "schema v2 model sidecar corpus가 유효하지 않다: "
                        + entry.path().string();
                    if (!issues.empty()) failure += " (" + issues.front().message + ")";
                    return false;
                }
                if (!addDocument(cached->document, entry.path())) return false;
            }
            // 사라진 sidecar의 항목은 걷는다 — 표가 프로젝트 수명 동안 자란다.
            std::erase_if(cache, [&seen](const auto& pair)
                { return !seen.contains(pair.first); });
            if (error)
            {
                failure = "model sidecar corpus를 순회하지 못했다: " + error.message();
                return false;
            }
            return addDocument(candidate, candidateSidecarPath);
        }

        [[nodiscard]] bool Mbc3BuildDraft(
            const std::filesystem::path& assetRoot,
            const std::filesystem::path& logicalSource,
            const im::ImportedScene& scene, const ModelSidecarV2& sidecar,
            experiment::ModelDraft& outDraft,
            std::vector<Mbc3PreparedTexture>& outTextures,
            std::string& failure)
        {
            std::vector<experiment::AssetId> materialIds(scene.materials.size());
            std::vector<experiment::AssetId> textureIds(scene.textures.size());
            for (const ModelSubAssetRecord& record : sidecar.subAssets)
            {
                if (record.kind == SubAssetKind::Material)
                {
                    const auto found = std::ranges::find_if(scene.materials,
                        [&](const im::ImportedMaterial& value)
                        {
                            return value.sourceKey == record.binding;
                        });
                    if (found == scene.materials.end())
                    {
                        failure = "material binding을 import 결과에서 찾지 못했다: "
                            + record.binding;
                        return false;
                    }
                    materialIds[static_cast<std::size_t>(
                        std::distance(scene.materials.begin(), found))] =
                        experiment::AssetId{ record.assetId };
                }
                else if (record.kind == SubAssetKind::Texture)
                {
                    const auto found = std::ranges::find_if(scene.textures,
                        [&](const im::ImportedTexture& value)
                        {
                            return value.sourceKey == record.binding;
                        });
                    if (found == scene.textures.end() || !found->IsEmbedded())
                    {
                        failure = "embedded texture binding을 import 결과에서 찾지 못했다: "
                            + record.binding;
                        return false;
                    }
                    textureIds[static_cast<std::size_t>(
                        std::distance(scene.textures.begin(), found))] =
                        experiment::AssetId{ record.assetId };
                }
            }
            if (std::ranges::any_of(materialIds,
                [](const experiment::AssetId& id) { return !id.IsValid(); }))
            {
                failure = "material UUIDv8 closure가 완전하지 않다.";
                return false;
            }

            experiment::AssetId gbufferShader{};
            experiment::AssetId forwardShader{};
            if (!ck::ReadMetaAssetId(assetRoot /
                "Shaders/DefaultPassShader/GBuffer.shadermeta.meta",
                gbufferShader, failure))
            {
                return false;
            }
            if (!ck::ReadMetaAssetId(assetRoot /
                "Shaders/DefaultPassShader/Forward.shadermeta.meta",
                forwardShader, failure))
            {
                return false;
            }

            std::vector<std::string> resolutionFailures;
            im::ConversionOptions options{};
            options.modelAssetId = experiment::AssetId{ sidecar.assetId };
            options.modelName = logicalSource.stem().string();
            options.resolveShaderAsset =
                [&](const im::ImportedMaterial& material, std::size_t)
                {
                    return material.alphaMode == im::AlphaMode::Blend
                        ? forwardShader : gbufferShader;
                };
            options.resolveMaterialAsset =
                [&](const im::ImportedMaterial&, std::size_t index)
                {
                    return index < materialIds.size()
                        ? materialIds[index] : experiment::AssetId{};
                };
            options.resolveTextureAsset =
                [&](const im::ImportedTexture& texture)
                {
                    const im::ImportedTexture* begin = scene.textures.data();
                    const im::ImportedTexture* end = begin + scene.textures.size();
                    if (&texture < begin || &texture >= end)
                    {
                        resolutionFailures.push_back(
                            "texture resolver가 import scene 밖의 객체를 받았다.");
                        return experiment::AssetId{};
                    }
                    const std::size_t index = static_cast<std::size_t>(&texture - begin);
                    if (texture.IsEmbedded())
                    {
                        if (index >= textureIds.size() || !textureIds[index].IsValid())
                        {
                            resolutionFailures.push_back(
                                "embedded texture UUIDv8 closure가 비었다: "
                                + texture.sourceKey);
                            return experiment::AssetId{};
                        }
                        return textureIds[index];
                    }
                    experiment::AssetId external{};
                    std::string externalFailure;
                    if (!Mbc3ReadExternalAssetId(assetRoot, texture.sourcePath,
                        external, externalFailure))
                    {
                        resolutionFailures.push_back(std::move(externalFailure));
                    }
                    return external;
                };

            im::ConversionResult converted =
                im::ConvertToModelDraft(scene, options);
            if (!converted.Succeeded())
            {
                failure = "ImportedScene을 cooked ModelDraft로 바꾸지 못했다.";
                if (!converted.notes.empty())
                    failure += " (" + converted.notes.front().message + ")";
                return false;
            }
            if (!resolutionFailures.empty())
            {
                failure = resolutionFailures.front();
                return false;
            }

            const auto sidecarCount = [&](SubAssetKind kind)
            {
                return static_cast<std::size_t>(std::ranges::count(
                    sidecar.subAssets, kind, &ModelSubAssetRecord::kind));
            };
            const std::size_t draftSkeletonCount = converted.draft->skeleton ? 1u : 0u;
            const std::size_t draftAnimationCount = converted.draft->skeleton
                ? converted.draft->skeleton->clips.size() : 0u;
            if (sidecarCount(SubAssetKind::Mesh) != converted.draft->meshes.size()
                || sidecarCount(SubAssetKind::Material) != converted.draft->materials.size()
                || sidecarCount(SubAssetKind::Skeleton) != draftSkeletonCount
                || sidecarCount(SubAssetKind::Animation) != draftAnimationCount)
            {
                failure = "sidecar subasset inventory와 cooked ModelDraft의 "
                    "mesh/material/skeleton/animation 폐포가 다르다.";
                return false;
            }

            outDraft = std::move(*converted.draft);
            outDraft.metadata.sourcePath = logicalSource;
            outDraft.metadata.cookedPath.clear();
            outDraft.metadata.sourceWriteTime = {};

            for (std::size_t index = 0; index < scene.textures.size(); ++index)
            {
                const im::ImportedTexture& texture = scene.textures[index];
                if (!texture.IsEmbedded()) continue;
                if (index >= textureIds.size() || !textureIds[index].IsValid())
                {
                    failure = "embedded texture UUIDv8이 없다: " + texture.sourceKey;
                    return false;
                }
                const std::string_view extension =
                    ck::SniffTextureExtension(texture.embeddedBytes);
                if (extension.empty() || !ck::IsSupportedTextureExtension(extension))
                {
                    failure = "embedded texture 포맷을 판별하지 못했다: "
                        + texture.sourceKey;
                    return false;
                }
                Mbc3PreparedTexture prepared;
                prepared.assetId = textureIds[index].value;
                prepared.relativePath = std::filesystem::path("textures") /
                    (Uuid::ToString(prepared.assetId) + std::string(extension));
                prepared.bytes = texture.embeddedBytes;
                prepared.fingerprint = Mbc3Fingerprint(prepared.bytes);
                outTextures.push_back(std::move(prepared));
            }
            return true;
        }

        [[nodiscard]] std::string Mbc3WriteGenerationRecord(
            const ModelSidecarV2& sidecar, std::string_view sidecarFingerprint,
            std::string_view modelFingerprint,
            const std::vector<Mbc3PreparedTexture>& textures)
        {
            Authoring::WriteDocument document;
            const Authoring::WriteNode root = document.Root();
            root.SetMap();
            root.Child("schemaVersion").SetScalar(1u);
            root.Child("identityProfile").SetScalar(sidecar.identityProfile);
            root.Child("identityEpoch").SetScalar(sidecar.identityEpoch);
            root.Child("assetId").SetScalar(Uuid::ToString(sidecar.assetId));
            root.Child("generation").SetScalar(sidecar.generation);
            root.Child("sourceFingerprint").SetScalar(sidecar.sourceFingerprint);
            root.Child("sidecarFingerprint").SetScalar(sidecarFingerprint);
            const Authoring::WriteNode model = root.Child("modelArtifact");
            model.SetMap();
            model.Child("path").SetScalar("model.cemc");
            model.Child("fingerprint").SetScalar(modelFingerprint);

            const Authoring::WriteNode entries = root.Child("subAssets");
            entries.SetSequence();
            for (const ModelSubAssetRecord& record : sidecar.subAssets)
            {
                const Authoring::WriteNode entry = entries.Append();
                entry.SetMap();
                entry.Child("kind").SetScalar(ToKindName(record.kind));
                entry.Child("stableKey").SetScalar(record.stableKey);
                entry.Child("assetId").SetScalar(Uuid::ToString(record.assetId));
                if (record.kind == SubAssetKind::Texture)
                {
                    const auto found = std::ranges::find(
                        textures, record.assetId, &Mbc3PreparedTexture::assetId);
                    if (found != textures.end())
                    {
                        entry.Child("artifactPath").SetScalar(
                            found->relativePath.generic_string());
                        entry.Child("artifactFingerprint").SetScalar(
                            found->fingerprint);
                    }
                }
            }
            std::string out = document.Dump();
            if (out.empty() || out.back() != '\n') out.push_back('\n');
            return out;
        }

        [[nodiscard]] bool Mbc3VerifyGenerationRecord(
            std::string_view text, const ModelSidecarV2& sidecar,
            std::string_view sidecarFingerprint,
            std::string_view modelFingerprint, std::string& failure)
        {
            std::string parseError;
            const Authoring::ParsedDocument document =
                Authoring::ParsedDocument::ParseText(std::string{ text }, parseError);
            if (!document || !document.Root().IsMap())
            {
                failure = "generation record를 재파싱하지 못했다: " + parseError;
                return false;
            }
            const Authoring::ReadNode root = document.Root();
            const Authoring::ReadNode model = root["modelArtifact"];
            const bool valid = root["schemaVersion"].As(0u) == 1u
                && root["identityProfile"].AsString() == sidecar.identityProfile
                && root["identityEpoch"].AsString() == sidecar.identityEpoch
                && root["assetId"].AsString() == Uuid::ToString(sidecar.assetId)
                && root["generation"].As(std::uint64_t{ 0 }) == sidecar.generation
                && root["sourceFingerprint"].AsString() == sidecar.sourceFingerprint
                && root["sidecarFingerprint"].AsString() == sidecarFingerprint
                && model && model.IsMap()
                && model["path"].AsString() == "model.cemc"
                && model["fingerprint"].AsString() == modelFingerprint
                && root["subAssets"].IsSequence()
                && root["subAssets"].Size() == sidecar.subAssets.size();
            if (!valid)
                failure = "generation record가 sidecar/cooked artifact와 일치하지 않는다.";
            return valid;
        }
    }

    bool IsModelAuthoringSource(const std::filesystem::path& sourcePath) noexcept
    {
        std::string extension = sourcePath.extension().string();
        std::ranges::transform(extension, extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".fbx" || extension == ".gltf"
            || extension == ".glb" || extension == ".obj";
    }

    ModelAssetAuthoringResult AuthorModelAsset(
        const ModelAssetAuthoringRequest& request)
    {
        ModelAssetAuthoringResult result;
        auto phaseClock = std::chrono::steady_clock::now();
        const auto markPhase = [&result, &phaseClock](const char* phase)
            {
                const auto now = std::chrono::steady_clock::now();
                result.phases.push_back({ phase,
                    std::chrono::duration<double, std::milli>(now - phaseClock).count() });
                phaseClock = now;
            };
        std::error_code error;
        const std::filesystem::path assetRoot =
            std::filesystem::weakly_canonical(request.assetRoot, error);
        if (error || assetRoot.empty()
            || !std::filesystem::is_directory(assetRoot, error))
        {
            Mbc3AddIssue(result, "request.assetRoot",
                "asset root가 유효한 디렉터리가 아니다.");
            return result;
        }

        error.clear();
        const std::filesystem::path source =
            std::filesystem::weakly_canonical(request.sourcePath, error);
        if (error || source.empty()
            || !std::filesystem::is_regular_file(source, error)
            || !ck::IsContainedPath(assetRoot, source)
            || !IsModelAuthoringSource(source))
        {
            Mbc3AddIssue(result, "request.sourcePath",
                "지원 model source가 asset root 안의 파일이 아니다.");
            return result;
        }

        error.clear();
        const std::filesystem::path generationRoot = std::filesystem::absolute(
            request.generationRoot, error).lexically_normal();
        if (error || generationRoot.empty()
            || ck::IsContainedPath(assetRoot, generationRoot))
        {
            Mbc3AddIssue(result, "request.generationRoot",
                "generation root는 source Assets tree 밖이어야 한다.");
            return result;
        }

        std::string failure;
        Mbc3ProjectWriterLock writerLock;
        if (!writerLock.Acquire(assetRoot, failure))
        {
            Mbc3AddIssue(result, "writer.lock", std::move(failure));
            return result;
        }

        std::vector<std::byte> headerBytes;
        if (!Mbc3ReadBytes(request.identityHeaderPath, headerBytes))
        {
            Mbc3AddIssue(result, "identity.header",
                "identity epoch header를 읽을 수 없다: "
                + request.identityHeaderPath.string());
            return result;
        }
        const std::string headerText{
            reinterpret_cast<const char*>(headerBytes.data()), headerBytes.size() };
        IdentityEpochHeader header;
        std::vector<EpochHeaderIssue> headerIssues;
        if (!ReadIdentityEpochHeader(headerText, header, headerIssues))
        {
            Mbc3AddIssue(result, "identity.header", headerIssues.empty()
                ? "identity epoch header가 유효하지 않다."
                : headerIssues.front().message);
            return result;
        }

        std::filesystem::path sidecarPath = source;
        sidecarPath += ".meta";
        error.clear();
        const bool hadSidecar = std::filesystem::is_regular_file(sidecarPath, error)
            && !error;
        std::string originalSidecar;
        if (hadSidecar && !Mbc3ReadText(sidecarPath, originalSidecar))
        {
            Mbc3AddIssue(result, "sidecar.read",
                "기존 model sidecar를 읽을 수 없다.");
            return result;
        }

        ModelSidecarV2 prior;
        bool hasPriorV2 = false;
        if (hadSidecar)
        {
            std::vector<SidecarIssue> priorIssues;
            if (ReadModelSidecarV2(originalSidecar, prior, priorIssues))
            {
                priorIssues.clear();
                if (!ValidateModelSidecarV2Closure(prior, header, priorIssues))
                {
                    Mbc3AddIssue(result, "sidecar.prior",
                        priorIssues.empty() ? "기존 v2 closure가 유효하지 않다."
                            : priorIssues.front().message);
                    return result;
                }
                hasPriorV2 = true;
            }
            else if (!Mbc3IsLegacyModelSidecar(originalSidecar))
            {
                Mbc3AddIssue(result, "sidecar.prior",
                    priorIssues.empty() ? "기존 sidecar가 legacy v1 또는 schema v2가 아니다."
                        : priorIssues.front().message);
                return result;
            }
        }

        markPhase("lock+header+sidecar");
        std::vector<std::byte> sourceBytes;
        if (!Mbc3ReadBytes(source, sourceBytes) || sourceBytes.empty())
        {
            Mbc3AddIssue(result, "source.read", "model source가 비었거나 읽을 수 없다.");
            return result;
        }
        const std::string sourceFingerprint = Mbc3Fingerprint(sourceBytes);
        // 게시 직전의 source race 검사가 쓸 지문. 아래 publish 단계 주석 참고.
        std::error_code sourceStat;
        const std::uintmax_t sourceSizeAtRead =
            std::filesystem::file_size(source, sourceStat);
        const std::filesystem::file_time_type sourceTimeAtRead = sourceStat
            ? std::filesystem::file_time_type{}
            : std::filesystem::last_write_time(source, sourceStat);
        const bool sourceStamped = !sourceStat;

        std::unique_ptr<im::IAssetImporter> importer = Mbc3CreateImporter(source);
        if (!importer)
        {
            Mbc3AddIssue(result, "source.decode",
                "이 model 확장자를 처리하는 정식 importer가 없다: "
                + source.extension().string());
            return result;
        }
        im::ImportRequest importRequest;
        importRequest.sourcePath = source;
        const im::ImportResult imported = importer->Import(importRequest);
        if (!imported.Succeeded())
        {
            Mbc3AddIssue(result, "source.decode", imported.notes.empty()
                ? "model import가 실패했다." : imported.notes.front().message);
            return result;
        }
        markPhase("source-read+decode");
        if (Mbc3Inject(request, ModelAuthoringFailurePoint::AfterDecode,
            result, "inject.after-decode")) return result;

        std::vector<StableKeyElement> elements =
            CollectStableKeyElements(*imported.scene);
        if (!Mbc3NormalizeStableInputs(elements, failure))
        {
            Mbc3AddIssue(result, "identity.normalize", std::move(failure));
            return result;
        }
        const StableKeyResult keys = DeriveModelStableKeys(elements,
            hasPriorV2 ? std::span<const ModelSubAssetRecord>{ prior.subAssets }
                : std::span<const ModelSubAssetRecord>{});
        if (!keys.Succeeded())
        {
            Mbc3AddIssue(result, "identity.stable-key", keys.issues.empty()
                ? "stable key derivation이 실패했다." : keys.issues.front().message);
            return result;
        }

        std::string modelAuthoringKey = hasPriorV2 ? prior.authoringKey
            : CreateModelAuthoringKey(failure);
        if (modelAuthoringKey.empty())
        {
            Mbc3AddIssue(result, "identity.model-key", std::move(failure));
            return result;
        }
        if (hasPriorV2
            && prior.generation == (std::numeric_limits<std::uint64_t>::max)())
        {
            Mbc3AddIssue(result, "identity.generation", "generation이 overflow한다.");
            return result;
        }
        const std::uint64_t generation = hasPriorV2
            ? prior.generation + 1u : 1u;
        ModelSidecarV2 sidecar;
        std::vector<SidecarIssue> sidecarIssues;
        if (!BuildModelSidecarV2(header, modelAuthoringKey, generation,
            sourceFingerprint, keys.assignments, sidecar, sidecarIssues))
        {
            Mbc3AddIssue(result, "identity.sidecar", sidecarIssues.empty()
                ? "schema v2 sidecar를 만들지 못했다."
                : sidecarIssues.front().message);
            return result;
        }
        markPhase("stable-keys+identity");
        if (!Mbc3ValidateCorpusCollision(assetRoot, sidecarPath,
            header, sidecar, failure))
        {
            Mbc3AddIssue(result, "identity.corpus", std::move(failure));
            return result;
        }
        markPhase("corpus-collision-scan");
        if (Mbc3Inject(request, ModelAuthoringFailurePoint::AfterIdentity,
            result, "inject.after-identity")) return result;

        std::string sidecarText = WriteModelSidecarV2(
            sidecar, originalSidecar, sidecarIssues);
        if (sidecarText.empty())
        {
            Mbc3AddIssue(result, "identity.sidecar-write", sidecarIssues.empty()
                ? "schema v2 sidecar를 직렬화하지 못했다."
                : sidecarIssues.front().message);
            return result;
        }

        error.clear();
        const std::filesystem::path logicalSource =
            std::filesystem::relative(source, assetRoot, error).lexically_normal();
        if (error || logicalSource.empty() || logicalSource.is_absolute())
        {
            Mbc3AddIssue(result, "source.logical-path",
                "asset-relative source path를 만들지 못했다.");
            return result;
        }

        experiment::ModelDraft draft;
        std::vector<Mbc3PreparedTexture> textures;
        if (!Mbc3BuildDraft(assetRoot, logicalSource, *imported.scene,
            sidecar, draft, textures, failure))
        {
            Mbc3AddIssue(result, "cooked.build", std::move(failure));
            return result;
        }
        markPhase("sidecar+draft");
        const ck::CookedWriteResult cooked = ck::Write(draft);
        if (!cooked.Succeeded())
        {
            Mbc3AddIssue(result, "cooked.write", cooked.issues.empty()
                ? "CEMC writer가 artifact를 만들지 못했다."
                : cooked.issues.front().message);
            return result;
        }
        const std::string modelFingerprint = Mbc3Fingerprint(cooked.bytes);
        const std::string sidecarFingerprint = Mbc3Fingerprint(sidecarText);
        const std::string generationRecord = Mbc3WriteGenerationRecord(
            sidecar, sidecarFingerprint, modelFingerprint, textures);

        const std::string modelIdText = Uuid::ToString(sidecar.assetId);
        const std::filesystem::path finalParent = generationRoot / modelIdText;
        const std::filesystem::path finalGeneration =
            finalParent / std::to_string(generation);
        error.clear();
        if (std::filesystem::exists(finalGeneration, error) || error)
        {
            Mbc3AddIssue(result, "publish.preflight",
                "같은 model generation이 이미 존재한다: "
                + finalGeneration.string());
            return result;
        }
        std::filesystem::create_directories(finalParent, error);
        if (error)
        {
            Mbc3AddIssue(result, "publish.preflight",
                "generation parent를 만들 수 없다: " + error.message());
            return result;
        }

        Mbc3Cleanup cleanup;
        cleanup.stage = Mbc3TemporarySibling(finalGeneration, "staging");
        cleanup.sidecarTemporary = Mbc3TemporarySibling(
            sidecarPath, "model-authoring");
        if (std::filesystem::exists(cleanup.stage, error)
            || std::filesystem::exists(cleanup.sidecarTemporary, error))
        {
            Mbc3AddIssue(result, "publish.preflight",
                "transaction temporary path가 이미 존재한다.");
            return result;
        }
        std::filesystem::create_directories(cleanup.stage, error);
        if (error)
        {
            Mbc3AddIssue(result, "stage.create", error.message());
            return result;
        }

        markPhase("cook+textures+record");
        if (!Mbc3WriteText(cleanup.stage / "sidecar.meta", sidecarText, failure)
            || !Mbc3WriteBytes(cleanup.stage / "model.cemc", cooked.bytes, failure)
            || !Mbc3WriteText(cleanup.stage / "generation.asset",
                generationRecord, failure))
        {
            Mbc3AddIssue(result, "stage.write", std::move(failure));
            return result;
        }
        for (const Mbc3PreparedTexture& texture : textures)
        {
            if (!Mbc3WriteBytes(cleanup.stage / texture.relativePath,
                texture.bytes, failure))
            {
                Mbc3AddIssue(result, "stage.texture", std::move(failure));
                return result;
            }
        }
        if (!Mbc3WriteText(cleanup.sidecarTemporary, sidecarText, failure))
        {
            Mbc3AddIssue(result, "stage.sidecar", std::move(failure));
            return result;
        }
        markPhase("stage-write");
        if (Mbc3Inject(request, ModelAuthoringFailurePoint::AfterStageWrite,
            result, "inject.after-stage-write")) return result;

        std::string stagedSidecar;
        std::string stagedCommitSidecar;
        ModelSidecarV2 reparsedSidecar;
        sidecarIssues.clear();
        if (!Mbc3ReadText(cleanup.stage / "sidecar.meta", stagedSidecar)
            || !Mbc3ReadText(cleanup.sidecarTemporary, stagedCommitSidecar)
            || stagedSidecar != sidecarText
            || stagedCommitSidecar != sidecarText
            || Mbc3Fingerprint(stagedSidecar) != sidecarFingerprint
            || !ReadModelSidecarV2(stagedSidecar, reparsedSidecar, sidecarIssues)
            || !ValidateModelSidecarV2Closure(
                reparsedSidecar, header, sidecarIssues)
            || reparsedSidecar.assetId != sidecar.assetId
            || reparsedSidecar.generation != generation)
        {
            Mbc3AddIssue(result, "stage.verify-sidecar", sidecarIssues.empty()
                ? "staged sidecar 재검증이 실패했다."
                : sidecarIssues.front().message);
            return result;
        }
        std::vector<std::byte> stagedModel;
        experiment::ModelDraft restoredDraft;
        std::vector<experiment::ModelLoadIssue> modelIssues;
        if (!Mbc3ReadBytes(cleanup.stage / "model.cemc", stagedModel)
            || Mbc3Fingerprint(stagedModel) != modelFingerprint
            || !ck::Read(stagedModel, restoredDraft, modelIssues)
            || restoredDraft.metadata.assetId.value != sidecar.assetId
            || restoredDraft.meshes.size() != draft.meshes.size()
            || restoredDraft.materials.size() != draft.materials.size()
            || restoredDraft.skeleton.has_value() != draft.skeleton.has_value()
            || (restoredDraft.skeleton && draft.skeleton
                && restoredDraft.skeleton->clips.size()
                    != draft.skeleton->clips.size()))
        {
            Mbc3AddIssue(result, "stage.verify-cooked",
                modelIssues.empty() ? "staged CEMC 재검증이 실패했다."
                    : modelIssues.front().message);
            return result;
        }
        for (const Mbc3PreparedTexture& texture : textures)
        {
            std::vector<std::byte> stagedTexture;
            if (!Mbc3ReadBytes(cleanup.stage / texture.relativePath, stagedTexture)
                || stagedTexture != texture.bytes
                || Mbc3Fingerprint(stagedTexture) != texture.fingerprint)
            {
                Mbc3AddIssue(result, "stage.verify-texture",
                    "staged embedded texture 재검증이 실패했다: "
                    + texture.relativePath.string());
                return result;
            }
        }
        std::string stagedRecord;
        if (!Mbc3ReadText(cleanup.stage / "generation.asset", stagedRecord)
            || !Mbc3VerifyGenerationRecord(stagedRecord, sidecar,
                sidecarFingerprint, modelFingerprint, failure))
        {
            Mbc3AddIssue(result, "stage.verify-record", std::move(failure));
            return result;
        }
        markPhase("stage-verify");
        if (Mbc3Inject(request, ModelAuthoringFailurePoint::AfterStageValidation,
            result, "inject.after-stage-validation")) return result;

        // source race 검사 (MBC11 §8.4 B1).
        //
        // 여기서 source 전체를 다시 읽어 SHA-256을 다시 계산하고 있었다 — 35 MB인
        // scene.glb 하나가 `publish` 단계 213 ms의 대부분이었다(기준선 §4.5).
        // 검사의 뜻은 "transaction 도는 동안 소스가 바뀌었나"이지 신원 유도가
        // 아니다(신원의 sourceFingerprint는 처음 읽은 바이트에서 이미 나왔다).
        // 그래서 먼저 (크기, 마지막 쓰기 시각)을 대조하고, 그 둘이 처음과 다를
        // 때만 다시 읽어 해시한다 — 내용은 그대로인데 touch만 된 경우를 거짓
        // 거부하지 않기 위해서다.
        //
        // ★ 약해지는 지점을 적어 둔다: 크기와 mtime을 **둘 다 보존하며** 내용만
        //   바꾼 쓰기는 이 검사를 통과한다. 파일 시스템이 쓰기에 mtime을 올리므로
        //   실수로 그렇게 되는 경로는 없고, 그렇게 만들려면 의도적으로 mtime을
        //   되돌려야 한다. stat을 못 얻으면(sourceStamped=false) 옛 방식대로
        //   전량 재해시로 떨어진다.
        bool sourceUnchanged = false;
        if (sourceStamped)
        {
            std::error_code stat;
            const std::uintmax_t size = std::filesystem::file_size(source, stat);
            const std::filesystem::file_time_type time = stat
                ? std::filesystem::file_time_type{}
                : std::filesystem::last_write_time(source, stat);
            sourceUnchanged = !stat && size == sourceSizeAtRead
                && time == sourceTimeAtRead;
        }
        if (!sourceUnchanged)
        {
            std::vector<std::byte> currentSource;
            if (!Mbc3ReadBytes(source, currentSource)
                || Mbc3Fingerprint(currentSource) != sourceFingerprint)
            {
                Mbc3AddIssue(result, "publish.source-race",
                    "transaction 중 source가 바뀌어 게시를 거부했다.");
                return result;
            }
        }
        std::string currentSidecar;
        error.clear();
        const bool sidecarStillExists =
            std::filesystem::is_regular_file(sidecarPath, error) && !error;
        if (sidecarStillExists != hadSidecar
            || (hadSidecar && (!Mbc3ReadText(sidecarPath, currentSidecar)
                || currentSidecar != originalSidecar)))
        {
            Mbc3AddIssue(result, "publish.sidecar-race",
                "transaction 중 canonical sidecar가 바뀌어 게시를 거부했다.");
            return result;
        }

        if (!Mbc3PublishDirectory(cleanup.stage, finalGeneration))
        {
            Mbc3AddIssue(result, "publish.generation",
                "generation directory 원자 게시가 실패했다 (Win32 "
                + std::to_string(::GetLastError()) + ").");
            return result;
        }
        cleanup.stage.clear();
        if (Mbc3Inject(request, ModelAuthoringFailurePoint::AfterGenerationPublish,
            result, "inject.after-generation-publish"))
        {
            std::filesystem::remove_all(finalGeneration, error);
            return result;
        }
        if (!Mbc3ReplaceFile(cleanup.sidecarTemporary, sidecarPath))
        {
            const DWORD replaceError = ::GetLastError();
            std::filesystem::remove_all(finalGeneration, error);
            Mbc3AddIssue(result, "publish.sidecar",
                "canonical sidecar commit이 실패했다 (Win32 "
                + std::to_string(replaceError) + ").");
            return result;
        }
        cleanup.sidecarTemporary.clear();
        cleanup.Release();

        markPhase("publish");
        result.modelAssetId = sidecar.assetId;
        result.generation = generation;
        result.sidecarPath = sidecarPath;
        result.generationPath = finalGeneration;
        result.subAssetCount = sidecar.subAssets.size();
        result.materialCount = std::ranges::count(sidecar.subAssets,
            SubAssetKind::Material, &ModelSubAssetRecord::kind);
        result.embeddedTextureCount = std::ranges::count(sidecar.subAssets,
            SubAssetKind::Texture, &ModelSubAssetRecord::kind);
        return result;
    }
}
