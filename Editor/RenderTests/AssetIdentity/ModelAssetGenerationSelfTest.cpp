#include "AssetIdentity/ModelAssetGenerationSelfTest.h"

#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelSidecarV2.h"
#include "DataSystem.h"
#include "MeshRenderer.h"
#include "RHI/IRenderDeviceServices.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <ranges>
#include <string_view>
#include <vector>

namespace RenderTest
{
    namespace
    {
        struct GenerationChecker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& label)
            {
                if (condition)
                {
                    ++passed;
                    return;
                }
                ++failed;
                log += "    [실패] " + label + "\n";
            }
        };

        struct TemporaryTree final
        {
            std::filesystem::path path{};
            ~TemporaryTree()
            {
                std::error_code ignored;
                if (!path.empty()) std::filesystem::remove_all(path, ignored);
            }
        };

        [[nodiscard]] bool HasIssue(
            const assets::ModelAssetGenerationLoadResult& result,
            assets::ModelAssetGenerationIssueCode code)
        {
            return std::ranges::any_of(result.issues,
                [code](const assets::ModelAssetGenerationIssue& issue)
                { return issue.code == code; });
        }

        [[nodiscard]] assets::ModelAssetGenerationLoadResult Load(
            const std::filesystem::path& header,
            const std::filesystem::path& generationPath,
            const Uuid::Uuid16& modelId, std::uint64_t generation,
            const std::filesystem::path& canonical = {})
        {
            assets::ModelAssetGenerationLoadRequest request;
            request.identityHeaderPath = header;
            request.generationPath = generationPath;
            request.canonicalSidecarPath = canonical;
            request.expectedModelId = modelId;
            request.expectedGeneration = generation;
            return assets::LoadModelAssetGeneration(request);
        }

        [[nodiscard]] bool FlipLastByte(const std::filesystem::path& path)
        {
            std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size <= 0) return false;
            stream.seekg(size - 1);
            char value{};
            stream.read(&value, 1);
            if (!stream) return false;
            value ^= 0x01;
            stream.seekp(size - 1);
            stream.write(&value, 1);
            stream.flush();
            return stream.good();
        }

        [[nodiscard]] bool ReplaceFirst(const std::filesystem::path& path,
            std::string_view before, std::string_view after)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return false;
            std::string text{ std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>() };
            const std::size_t position = text.find(before);
            if (position == std::string::npos) return false;
            text.replace(position, before.size(), after);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.flush();
            return output.good();
        }

        // Exercise the production change boundary without changing the source fixture.
        // Only this unloaded probe's catalog path points at a temporary sidecar;
        // its immutable generation files stay in the project's normal Library.
        void VerifyRuntimeReload(GenerationChecker& check,
            const std::filesystem::path& onePath,
            const std::filesystem::path& twoPath, const Uuid::Uuid16& modelId,
            ModelGenerationReport& report)
        {
            const auto currents = DataSystems->SnapshotCurrentModelAssetGenerations();
            const bool unused = std::ranges::none_of(currents, [&](const auto& generation)
                { return generation->Identity().modelId == modelId; });
            check.Check(unused, "runtime fixture is not already used by the active scene");
            if (!unused) return;

            TemporaryTree sandbox;
            sandbox.path = std::filesystem::temp_directory_path()
                / ("creator-runtime-reload-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directory(sandbox.path);
            const FileGuid guid(modelId);
            const auto originalPath = DataSystems->GetFilePath(guid);
            const auto source = sandbox.path / "RuntimeReloadProbe.glb";
            const auto meta = std::filesystem::path(source.string() + ".meta");
            std::ofstream(source, std::ios::binary).put('\0');
            struct CatalogGuard
            {
                FileGuid guid;
                std::filesystem::path source, original;
                ~CatalogGuard()
                {
                    DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
                        RuntimeAssetType::Model, guid, source });
                    if (!original.empty())
                        DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::CatalogUpsert,
                            RuntimeAssetType::Model, guid, original });
                }
            } catalogGuard{ guid, source, originalPath };
            // Registration rejects one ID at two paths. Detach only the unloaded
            // probe's catalog entry; the guard restores it even on failure.
            if (!originalPath.empty())
                DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
                    RuntimeAssetType::Model, guid, originalPath });
            const auto select = [&](const std::filesystem::path& generation)
            {
                std::filesystem::copy_file(generation / "sidecar.meta", meta,
                    std::filesystem::copy_options::overwrite_existing);
            };
            const auto reload = [&]
            {
                DataSystems->QueueAssetChange({ RuntimeAssetChangeKind::ContentReload,
                    RuntimeAssetType::Model, guid, source });
                DataSystems->DrainQueuedAssetChanges();
            };
            select(onePath);
            DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::CatalogUpsert,
                RuntimeAssetType::Model, guid, source });
            const auto initial = DataSystems->LoadModelAssetGeneration(guid);
            const bool coldReady = initial && !initial->Textures().empty()
                && std::to_string(initial->Handle().generation) == onePath.filename().string();
            check.Check(coldReady, "runtime probe cold loads the selected older generation with textures");
            if (!coldReady) return;

            MeshRenderer instance;
            const auto verifyFailure = [&](const char* name, auto tamper)
            {
                const auto before = DataSystems->LoadModelAssetGeneration(guid);
                if (!before) { check.Check(false, "runtime failure probe lost setup generation"); return; }
                check.Check(instance.BindModelGeneration(before, 0), "existing instance bound");
                const auto instanceHandle = instance.GetModelMeshHandle();
                std::vector<std::shared_ptr<Texture>> owners;
                for (const auto& texture : before->Textures())
                    owners.push_back(DataSystems->ResolveModelGenerationTexture(*before, texture.textureId));
                check.Check(std::ranges::all_of(owners, [](const auto& owner) { return !!owner; }),
                    "runtime probe texture owners exist");
                const auto cacheBefore = DataSystems->SnapshotModelAssetGenerations();
                const auto texturesBefore = DataSystems->SnapshotModelGenerationTextures();
                const auto sourcesBefore = DataSystems->SnapshotModelGenerationSources();
                tamper();
                ++report.runtimeCases;
                check.Check(!DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::ContentReload,
                    RuntimeAssetType::Model, guid, source }), std::string(name) + " reports reload rejection to caller");
                reload();
                const auto after = DataSystems->LoadModelAssetGeneration(guid);
                const auto cacheAfter = DataSystems->SnapshotModelAssetGenerations();
                const bool rejected = DataSystems->SnapshotModelGenerationSources().failed > sourcesBefore.failed;
                const bool held = after == before
                    && DataSystems->ResolveModelAssetGeneration(before->Handle()) == before
                    && cacheAfter.retires == cacheBefore.retires
                    && cacheAfter.replacements == cacheBefore.replacements;
                bool texturesHeld = DataSystems->SnapshotModelGenerationTextures().retired == texturesBefore.retired;
                for (std::size_t i = 0; i < owners.size(); ++i)
                    texturesHeld &= owners[i] == DataSystems->ResolveModelGenerationTexture(
                        *before, before->Textures()[i].textureId);
                RHIModelMeshView view;
                const bool instanceHeld = instance.m_modelGeneration == before
                    && instance.GetModelMeshHandle() == instanceHandle
                    && BuildRHIModelMeshView(*instance.m_modelGeneration, 0, view) && view.IsComplete();
                check.Check(rejected, std::string(name) + " reload failure observed");
                check.Check(held, std::string(name) + " current lookup and handle preserved");
                check.Check(texturesHeld, std::string(name) + " texture owners preserved");
                check.Check(instanceHeld, std::string(name) + " existing instance remains renderable");
                report.runtimeRejected += rejected;
                report.runtimeCurrentHeld += held;
                report.runtimeTexturesHeld += texturesHeld;
                report.runtimeInstanceHeld += instanceHeld;
            };

            verifyFailure("missing-sidecar", [&] { std::filesystem::remove(meta); });
            select(onePath); reload();
            verifyFailure("malformed-sidecar", [&]
                { std::ofstream(meta, std::ios::trunc) << "schemaVersion: invalid\n"; });
            select(onePath); reload();
            verifyFailure("candidate-mismatch", [&]
                { select(twoPath); std::ofstream(meta, std::ios::app) << "\n# different canonical bytes\n"; });
            select(onePath); reload();

            const auto before = DataSystems->LoadModelAssetGeneration(guid);
            if (!before) { check.Check(false, "runtime recovery setup"); return; }
            instance.BindModelGeneration(before, 0);
            std::vector<std::shared_ptr<Texture>> oldOwners;
            for (const auto& texture : before->Textures())
                oldOwners.push_back(DataSystems->ResolveModelGenerationTexture(*before, texture.textureId));
            const auto cacheBefore = DataSystems->SnapshotModelAssetGenerations();
            const auto texturesBefore = DataSystems->SnapshotModelGenerationTextures();
            select(twoPath); reload();
            const auto after = DataSystems->LoadModelAssetGeneration(guid);
            const auto cacheAfter = DataSystems->SnapshotModelAssetGenerations();
            bool newOwners = !!after;
            if (after)
                for (std::size_t i = 0; i < after->Textures().size(); ++i)
                {
                    const auto owner = DataSystems->ResolveModelGenerationTexture(*after, after->Textures()[i].textureId);
                    newOwners &= owner && std::ranges::find(oldOwners, owner) == oldOwners.end();
                }
            MeshRenderer fresh;
            RHIModelMeshView oldView;
            report.runtimeRecovered = after && after->Handle().generation > before->Handle().generation
                && cacheAfter.replacements == cacheBefore.replacements + 1
                && !DataSystems->ResolveModelAssetGeneration(before->Handle()) && newOwners
                && DataSystems->SnapshotModelGenerationTextures().retired == texturesBefore.retired + oldOwners.size()
                && instance.m_modelGeneration == before
                && BuildRHIModelMeshView(*before, 0, oldView) && oldView.IsComplete()
                && fresh.BindModelGeneration(after, 0) && fresh.GetModelMeshHandle().generation == after->Handle().generation;
            check.Check(report.runtimeRecovered, "valid candidate atomically replaces current; old instance lives, fresh uses new");
            if (!after) return;

            // A valid but older candidate must not roll current back either.
            verifyFailure("stale-generation", [&] { select(onePath); });
            select(twoPath); reload();
            const auto stable = DataSystems->LoadModelAssetGeneration(guid);
            const auto duplicateCache = DataSystems->SnapshotModelAssetGenerations();
            const auto duplicateTextures = DataSystems->SnapshotModelGenerationTextures();
            reload();
            report.runtimeDuplicateStable = stable && DataSystems->LoadModelAssetGeneration(guid) == stable
                && DataSystems->SnapshotModelAssetGenerations().retires == duplicateCache.retires
                && DataSystems->SnapshotModelGenerationTextures().retired == duplicateTextures.retired;
            check.Check(report.runtimeDuplicateStable, "duplicate current notification does not recreate aggregate or textures");
            DataSystems->ApplyAssetChange({ RuntimeAssetChangeKind::Removed,
                RuntimeAssetType::Model, guid, source });
            report.runtimeRemoved = stable && !DataSystems->ResolveModelAssetGeneration(stable->Handle())
                && !DataSystems->LoadModelAssetGeneration(guid) && !stable->Meshes().empty();
            check.Check(report.runtimeRemoved, "explicit removal clears lookup while held snapshot lives");
        }
    }

    bool RunModelAssetGenerationSelfTest(const std::string& projectRoot,
        const std::string& modelIdText, std::string& outLog,
        ModelGenerationReport* report)
    {
        if (report) *report = {};
        GenerationChecker check{ outLog };
        outLog += "[assets.generation] MBC5 immutable aggregate·atomic cache 검사\n";

        // PBR-W8 — 게이트가 읽는 수. 세 return 지점이 모두 채운다(이른 반환에서
        // 비워 두면 fixture 가 안 선 회차가 "0 건 실패" 로 읽혀 초록이 된다).
        std::uint64_t tamperCases = 0, tamperRejected = 0, tamperCurrentHeld = 0;
        bool fixtureResolved = false;
        const auto publishReport = [&]
        {
            if (!report) return;
            report->passed = check.passed;
            report->failed = check.failed;
            report->tamperCases = tamperCases;
            report->tamperRejected = tamperRejected;
            report->tamperCurrentHeld = tamperCurrentHeld;
            report->fixtureResolved = fixtureResolved;
        };

        const std::filesystem::path project(projectRoot);
        const std::filesystem::path header =
            project / "ProjectSetting" / "AssetIdentity.asset";
        const std::filesystem::path generationRoot =
            project / "Library" / "ModelAssetGenerations";

        // ★ 예전에는 `Prim_Cube.glb` 를 박아 두고 ModelId 디렉터리가 **정확히 1 개**
        //   이기를 요구했다. 살아 있는 프로젝트에는 103 개가 있어 그 전제가 설 수
        //   없었고, 그래서 이 검사는 호출자 0 으로 죽어 있었다. 재는 것은 번호가
        //   아니라 원자성이므로 대상 모델을 인자로 받는다.
        Uuid::Uuid16 modelId{};
        check.Check(assets::TryParseCanonicalUuidV8(modelIdText, modelId),
            "인자 modelId 가 canonical UUIDv8");
        const std::filesystem::path modelDirectory = generationRoot / modelIdText;
        check.Check(std::filesystem::is_directory(modelDirectory),
            "대상 ModelId generation 디렉터리 존재");
        check.Check(std::filesystem::is_regular_file(header), "fixture epoch header 존재");
        check.Check(std::filesystem::is_directory(generationRoot), "fixture generation root 존재");

        // canonical sidecar — 원본 자산 옆의 `.meta`. 이름은 모르므로 자산 트리에서
        // 이 ModelId 를 지목하는 것을 찾는다.
        std::error_code error;
        std::filesystem::path canonical;
        std::uint64_t canonicalGeneration = 0;
        {
            const std::filesystem::path models = project / "Assets" / "Models";
            if (std::filesystem::is_directory(models, error) && !error)
            {
                for (const std::filesystem::directory_entry& entry
                    : std::filesystem::recursive_directory_iterator(models, error))
                {
                    if (error) break;
                    if (!entry.is_regular_file() || entry.path().extension() != ".meta") continue;
                    std::ifstream probe(entry.path());
                    std::string line;
                    bool matched = false;
                    std::uint64_t generation = 0;
                    while (std::getline(probe, line))
                    {
                        if (line.rfind("assetId: ", 0) == 0
                            && line.substr(9, modelIdText.size()) == modelIdText)
                        {
                            matched = true;
                        }
                        else if (line.rfind("generation: ", 0) == 0)
                        {
                            generation = std::strtoull(line.c_str() + 12, nullptr, 10);
                        }
                    }
                    if (matched && 0 != generation)
                    {
                        canonical = entry.path();
                        canonicalGeneration = generation;
                        break;
                    }
                }
            }
        }
        check.Check(!canonical.empty() && std::filesystem::is_regular_file(canonical),
            "대상 ModelId 의 canonical sidecar 존재");

        // ★ 번호는 **실재하는 것에서 고른다.** 뒤엣것은 canonical sidecar 가 지목하는
        //   generation 이어야 "canonical sidecar load" 단정이 자기 트리와 맞다.
        std::uint64_t generationTwo = canonicalGeneration;
        std::uint64_t generationOne = 0;
        if (std::filesystem::is_directory(modelDirectory, error) && !error)
        {
            for (const std::filesystem::directory_entry& entry
                : std::filesystem::directory_iterator(modelDirectory, error))
            {
                if (error || !entry.is_directory()) continue;
                const std::uint64_t value =
                    std::strtoull(entry.path().filename().string().c_str(), nullptr, 10);
                if (0 != value && value < generationTwo && value > generationOne)
                    generationOne = value;
            }
        }
        check.Check(0 != generationOne && generationOne < generationTwo,
            "generation 두 벌(이전·current)이 디스크에 있다");
        if (canonical.empty() || 0 == generationOne || generationOne >= generationTwo)
        {
            outLog += "  fixture 해석 실패 model=" + modelIdText
                + " gen=" + std::to_string(generationOne) + "->"
                + std::to_string(generationTwo) + "\n";
            outLog += "  단정 " + std::to_string(check.passed + check.failed)
                + "건 중 통과 " + std::to_string(check.passed) + " · 실패 "
                + std::to_string(check.failed) + "\n";
            outLog += "  assertions total="
                + std::to_string(check.passed + check.failed)
                + " passed=" + std::to_string(check.passed)
                + " failed=" + std::to_string(check.failed) + "\n";
            publishReport();
            return false;
        }

        const std::filesystem::path onePath =
            modelDirectory / std::to_string(generationOne);
        const std::filesystem::path twoPath =
            modelDirectory / std::to_string(generationTwo);
        check.Check(std::filesystem::is_directory(onePath), "이전 generation 디렉터리 존재");
        check.Check(std::filesystem::is_directory(twoPath), "current generation 디렉터리 존재");

        const assets::ModelAssetGenerationLoadResult one =
            Load(header, onePath, modelId, generationOne);
        const assets::ModelAssetGenerationLoadResult two =
            Load(header, twoPath, modelId, generationTwo, canonical);
        check.Check(one.Succeeded(), "이전 generation 전체 closure load");
        check.Check(two.Succeeded(), "current generation + canonical sidecar load");
        if (!one.Succeeded() || !two.Succeeded())
        {
            for (const auto& issue : one.issues)
                outLog += "    gen1 " + issue.context + ": " + issue.message + "\n";
            for (const auto& issue : two.issues)
                outLog += "    gen2 " + issue.context + ": " + issue.message + "\n";
            outLog += "  단정 " + std::to_string(check.passed + check.failed)
                + "건 중 통과 " + std::to_string(check.passed) + " · 실패 "
                + std::to_string(check.failed) + "\n";
            outLog += "  assertions total="
                + std::to_string(check.passed + check.failed)
                + " passed=" + std::to_string(check.passed)
                + " failed=" + std::to_string(check.failed) + "\n";
            publishReport();
            return false;
        }
        // 여기까지 왔으면 fixture 전제(ModelId 1개 · generation 1·2 적재)가 섰다.
        fixtureResolved = true;

        ModelGenerationReport runtime;
        VerifyRuntimeReload(check, onePath, twoPath, modelId, runtime);
        if (report)
        {
            report->runtimeCases = runtime.runtimeCases;
            report->runtimeRejected = runtime.runtimeRejected;
            report->runtimeCurrentHeld = runtime.runtimeCurrentHeld;
            report->runtimeTexturesHeld = runtime.runtimeTexturesHeld;
            report->runtimeInstanceHeld = runtime.runtimeInstanceHeld;
            report->runtimeRecovered = runtime.runtimeRecovered;
            report->runtimeDuplicateStable = runtime.runtimeDuplicateStable;
            report->runtimeRemoved = runtime.runtimeRemoved;
        }

        const auto first = one.generation;
        const auto second = two.generation;
        // ★ 해석한 번호와 적재된 신원을 **둘 다** 찍는다. 둘이 어긋나면 단정만
        //   붉어지고 이유는 안 보인다 — 그 자리를 로그가 메운다.
        outLog += "  resolved gen=" + std::to_string(generationOne) + "->"
            + std::to_string(generationTwo)
            + " identity=" + std::to_string(first->Identity().generation) + "->"
            + std::to_string(second->Identity().generation) + "\n";
        check.Check(first->Identity().modelId == second->Identity().modelId
            && first->Identity().generation == generationOne
            && second->Identity().generation == generationTwo
            && generationOne < generationTwo,
            "동일 ModelId·단조 generation identity");
        check.Check(first->Identity().sourceFingerprint
            == second->Identity().sourceFingerprint,
            "무변경 reimport source fingerprint 보존");
        check.Check(!second->Meshes().empty() && !second->Nodes().empty(),
            "mesh/node CPU storage 게시");
        check.Check(second->GpuDescriptors().size()
            == second->Meshes().size() * 2u + second->Textures().size(),
            "mesh vertex/index + texture upload descriptor 폐포");
        check.Check(!second->Textures().empty(),
            "embedded texture가 generation closure에 포함됨");
        for (const assets::ModelMeshAsset& mesh : second->Meshes())
        {
            check.Check(assets::IsUuidV8(mesh.meshId) && !mesh.vertexBytes.empty()
                && !mesh.indices.empty() && mesh.vertexStride != 0u
                && mesh.vertexLayoutHash != 0u,
                "mesh UUIDv8/storage/layout " + mesh.name);
            check.Check(second->FindMesh(mesh.meshId) == &mesh,
                "MeshId 직접 조회 " + mesh.name);
        }
        for (const assets::ModelMaterialAsset& material : second->Materials())
        {
            check.Check(assets::IsUuidV8(material.materialId),
                "material UUIDv8 " + material.name);
            check.Check(second->FindMaterial(material.materialId) == &material,
                "MaterialId 직접 조회 " + material.name);
            for (const assets::ModelMaterialProperty& property : material.properties)
            {
                const auto* texture = std::get_if<assets::ModelMaterialTexture>(
                    &property.value);
                if (!texture || texture->handle.generation == 0u) continue;
                check.Check(texture->handle.generation == second->Identity().generation
                    && second->FindTexture(texture->handle.textureId) != nullptr,
                    "embedded texture handle가 같은 generation을 가리킴 "
                        + property.name);
            }
        }
        for (const assets::ModelTextureAsset& texture : second->Textures())
        {
            check.Check(assets::IsUuidV8(texture.textureId)
                && texture.format != RHIFormat::Unknown && !texture.pixels.empty()
                && !texture.subresources.empty(),
                "texture decode/upload descriptor " + texture.name);
            check.Check(texture.mipLevels > 1u || (texture.width == 1u && texture.height == 1u),
                "embedded texture has generated mips " + texture.name);
            check.Check(texture.subresources.size() == texture.mipLevels * texture.arraySize,
                "embedded mip/array closure " + texture.name);
        }
        if (const assets::ModelSkeletonAsset* skeleton = second->Skeleton())
        {
            check.Check(assets::IsUuidV8(skeleton->skeletonId)
                && !skeleton->bones.empty(), "skeleton UUIDv8/hierarchy 게시");
            for (const assets::ModelAnimationAsset& animation : second->Animations())
                check.Check(assets::IsUuidV8(animation.animationId),
                    "animation UUIDv8 " + animation.name);
        }

        assets::ModelAssetGenerationCache cache;
        const assets::ModelAssetPublishResult publishOne = cache.Publish(first);
        check.Check(publishOne.outcome == assets::ModelAssetPublishOutcome::Published
            && publishOne.current == first, "generation 1 최초 원자 게시");
        check.Check(cache.ResolveCurrent(modelId) == first, "current generation 1 resolve");
        const assets::ModelMeshHandle oldMeshHandle{ modelId,
            first->Meshes().front().meshId, generationOne };
        assets::ModelAssetGeneration::Shared oldMeshOwner;
        check.Check(cache.ResolveMesh(oldMeshHandle, oldMeshOwner) != nullptr
            && oldMeshOwner == first, "{ModelId,MeshId,generation} mesh binding");

        const assets::ModelAssetPublishResult publishTwo = cache.Publish(second);
        check.Check(publishTwo.outcome == assets::ModelAssetPublishOutcome::Replaced
            && publishTwo.current == second && publishTwo.retired == first,
            "generation 2가 generation 1 전체를 교체");
        check.Check(cache.ResolveCurrent(modelId) == second,
            "교체 뒤 current generation 2 resolve");
        check.Check(!cache.Resolve(first->Handle()),
            "교체 뒤 이전 generation handle은 cache에서 해석되지 않음");
        check.Check(first->Identity().generation == generationOne
            && !first->Meshes().empty(),
            "외부 owner가 잡은 retired generation은 수명 안전");
        check.Check(cache.Publish(first).outcome
            == assets::ModelAssetPublishOutcome::RejectedStale,
            "stale generation 재게시 거부");
        check.Check(cache.Publish(second).outcome
            == assets::ModelAssetPublishOutcome::AlreadyCurrent,
            "동일 generation 멱등 게시");

        TemporaryTree temporary;
        temporary.path = std::filesystem::temp_directory_path()
            / ("CreatorMbc5Generation-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temporary.path, error);
        check.Check(!error, "tamper fixture 임시 경로 생성");

        const auto verifyTamperDoesNotPublish = [&](std::string_view name,
            const auto& mutate, assets::ModelAssetGenerationIssueCode expected)
        {
            const std::filesystem::path bad = temporary.path / name;
            error.clear();
            std::filesystem::copy(twoPath, bad,
                std::filesystem::copy_options::recursive, error);
            const bool mutated = !error && mutate(bad);
            const auto rejected = mutated
                ? Load(header, bad, modelId, generationTwo)
                : assets::ModelAssetGenerationLoadResult{};
            ++tamperCases;
            const bool wasRejected = mutated && !rejected.Succeeded()
                && HasIssue(rejected, expected);
            check.Check(wasRejected, std::string(name) + " 게시 전 거부");
            if (wasRejected) ++tamperRejected;
            // ★ W8 계약이 걸린 자리는 여기다. "거부됐다" 와 "거부된 뒤에도 current 가
            //   그대로다" 는 다른 단정이다 — 합쳐 세면 거부는 되는데 current 가
            //   날아가는 회귀를 못 본다.
            const bool currentHeld = cache.ResolveCurrent(modelId) == second;
            check.Check(currentHeld, std::string(name) + " 실패 뒤 current generation 불변");
            if (currentHeld) ++tamperCurrentHeld;
        };

        verifyTamperDoesNotPublish("bad-model",
            [](const std::filesystem::path& root)
            { return FlipLastByte(root / "model.cemc"); },
            assets::ModelAssetGenerationIssueCode::FingerprintMismatch);
        verifyTamperDoesNotPublish("bad-sidecar",
            [](const std::filesystem::path& root)
            {
                return ReplaceFirst(root / "sidecar.meta",
                    "sourceFingerprint: sha256:",
                    "sourceFingerprint: sha256:0");
            }, assets::ModelAssetGenerationIssueCode::InvalidSidecar);
        verifyTamperDoesNotPublish("bad-record",
            [](const std::filesystem::path& root)
            {
                return ReplaceFirst(root / "generation.asset",
                    "identityProfile: ce.uuidv8.sha256.v1",
                    "identityProfile: ce.uuidv8.sha256.v0");
            }, assets::ModelAssetGenerationIssueCode::InvalidGenerationRecord);
        verifyTamperDoesNotPublish("bad-texture",
            [](const std::filesystem::path& root)
            {
                const std::filesystem::path textures = root / "textures";
                std::error_code localError;
                for (const std::filesystem::directory_entry& entry
                    : std::filesystem::directory_iterator(textures, localError))
                {
                    if (!localError && entry.is_regular_file())
                        return FlipLastByte(entry.path());
                }
                return false;
            }, assets::ModelAssetGenerationIssueCode::FingerprintMismatch);

        const assets::ModelAssetGeneration::Shared retired = cache.Retire(modelId);
        check.Check(retired == second && !cache.ResolveCurrent(modelId),
            "retire가 model/subasset/descriptor generation 전체를 cache에서 분리");
        check.Check(second->Identity().generation == generationTwo
            && !second->GpuDescriptors().empty(),
            "retire 뒤 외부 snapshot 수명 안전");
        const assets::ModelAssetGenerationCacheSnapshot snapshot = cache.Snapshot();
        check.Check(snapshot.currentAssets == 0u
            && snapshot.addressableGenerations == 0u
            && snapshot.publishes == 2u && snapshot.replacements == 1u
            && snapshot.retires == 2u && snapshot.hits >= 1u
            && snapshot.misses >= 1u,
            "read-only cache snapshot 계수");

        outLog += "  generation model=" + Uuid::ToString(modelId)
            + " meshes=" + std::to_string(second->Meshes().size())
            + " materials=" + std::to_string(second->Materials().size())
            + " textures=" + std::to_string(second->Textures().size())
            + " descriptors=" + std::to_string(second->GpuDescriptors().size())
            // ★ 리터럴 `tamper=4` 였다. 게이트의 출력은 **변수**에서 나와야 한다 —
            //   주입 하나를 지우는 변이 아래서도 4 라고 말하면, 진단이 한 일이
            //   아니라 적어 둔 의도를 말하는 것이다.
            + " tamper=" + std::to_string(tamperCases)
            + " tamperRejected=" + std::to_string(tamperRejected)
            + " tamperCurrentHeld=" + std::to_string(tamperCurrentHeld)
            + " replacements=" + std::to_string(snapshot.replacements)
            + " retires=" + std::to_string(snapshot.retires) + "\n";
        outLog += "  단정 " + std::to_string(check.passed + check.failed)
            + "건 중 통과 " + std::to_string(check.passed) + " · 실패 "
            + std::to_string(check.failed) + "\n";
        outLog += "  assertions total="
            + std::to_string(check.passed + check.failed)
            + " passed=" + std::to_string(check.passed)
            + " failed=" + std::to_string(check.failed) + "\n";
        publishReport();
        return check.failed == 0u;
    }

    bool RunModelAssetGenerationCorpusSelfTest(
        const std::string& runtimeContentRoot, std::string& outLog)
    {
        GenerationChecker check{ outLog };
        outLog += "[assets.generationcorpus] MBC4 corpus cold-load closure 검사\n";

        const std::filesystem::path content(runtimeContentRoot);
        const std::filesystem::path assetsRoot = content / "Assets";
        const std::filesystem::path header =
            content / "ProjectSetting" / "AssetIdentity.asset";
        const std::filesystem::path generationRoot =
            content / "Library" / "ModelAssetGenerations";
        check.Check(std::filesystem::is_regular_file(header), "corpus epoch header 존재");
        check.Check(std::filesystem::is_directory(generationRoot),
            "corpus generation root 존재");

        std::vector<std::filesystem::path> sources;
        std::error_code error;
        if (std::filesystem::is_directory(assetsRoot, error) && !error)
        {
            for (std::filesystem::recursive_directory_iterator iterator(
                    assetsRoot, std::filesystem::directory_options::skip_permission_denied,
                    error), end;
                iterator != end; iterator.increment(error))
            {
                if (error)
                {
                    error.clear();
                    continue;
                }
                if (!iterator->is_regular_file()) continue;
                std::string extension = iterator->path().extension().string();
                std::ranges::transform(extension, extension.begin(),
                    [](unsigned char value)
                    { return static_cast<char>(std::tolower(value)); });
                if (extension == ".fbx" || extension == ".gltf"
                    || extension == ".glb" || extension == ".obj")
                {
                    sources.push_back(iterator->path());
                }
            }
        }
        std::ranges::sort(sources);
        check.Check(sources.size() >= 14u, "MBC4 model corpus 14개 이상 발견");

        assets::ModelAssetGenerationCache cache;
        std::set<Uuid::Uuid16> modelIds;
        std::size_t loadedCount = 0u;
        std::size_t meshCount = 0u;
        std::size_t materialCount = 0u;
        std::size_t textureCount = 0u;
        std::size_t skeletonCount = 0u;
        std::size_t animationCount = 0u;
        std::size_t descriptorCount = 0u;
        bool sawGunner = false;
        bool sawSu = false;
        std::string suRenderSummary;
        for (const std::filesystem::path& source : sources)
        {
            std::filesystem::path sidecar = source;
            sidecar += ".meta";
            assets::ModelAssetGenerationLoadRequest request;
            request.identityHeaderPath = header;
            request.generationRoot = generationRoot;
            request.canonicalSidecarPath = sidecar;
            const assets::ModelAssetGenerationLoadResult loaded =
                assets::LoadModelAssetGeneration(request);
            check.Check(loaded.Succeeded(), "cold-load " + source.filename().string());
            if (!loaded.Succeeded())
            {
                for (const auto& issue : loaded.issues)
                    outLog += "    " + source.filename().string() + " "
                        + issue.context + ": " + issue.message + "\n";
                continue;
            }
            ++loadedCount;
            const auto generation = loaded.generation;
            check.Check(modelIds.insert(generation->Identity().modelId).second,
                "ModelId corpus 중복 0 " + source.filename().string());
            check.Check(cache.Publish(generation).outcome
                == assets::ModelAssetPublishOutcome::Published,
                "corpus cache publish " + source.filename().string());
            check.Check(generation->GpuDescriptors().size()
                == generation->Meshes().size() * 2u + generation->Textures().size(),
                "corpus upload closure " + source.filename().string());
            for (const assets::ModelMaterialAsset& material : generation->Materials())
            {
                for (const assets::ModelMaterialProperty& property : material.properties)
                {
                    const auto* texture = std::get_if<assets::ModelMaterialTexture>(
                        &property.value);
                    if (!texture || texture->handle.generation == 0u) continue;
                    check.Check(texture->handle.generation == generation->Identity().generation
                        && generation->FindTexture(texture->handle.textureId) != nullptr,
                        "corpus embedded texture handle " + source.filename().string()
                            + "/" + property.name);
                }
            }

            meshCount += generation->Meshes().size();
            materialCount += generation->Materials().size();
            for (const auto& texture : generation->Textures())
            {
                check.Check(texture.mipLevels > 1u || (texture.width == 1u && texture.height == 1u),
                    "corpus embedded mip chain " + source.filename().string() + ":" + texture.name);
                check.Check(texture.subresources.size() == texture.mipLevels * texture.arraySize,
                    "corpus embedded mip/array closure " + texture.name);
            }
            textureCount += generation->Textures().size();
            skeletonCount += generation->Skeleton() ? 1u : 0u;
            animationCount += generation->Animations().size();
            descriptorCount += generation->GpuDescriptors().size();

            if (source.filename() == "Gunner_F_Mythic.glb")
            {
                sawGunner = true;
                check.Check(generation->Materials().size() == 2u
                    && generation->Textures().size() == 6u
                    && generation->Skeleton() != nullptr
                    && generation->Animations().size() == 10u,
                    "Gunner material 2/embedded texture 6/skeleton/animation 10 closure");
            }
            if (source.filename() == "SU_Mythic.glb")
            {
                sawSu = true;
                check.Check(generation->Meshes().size() == 1u
                    && generation->Materials().size() == 1u
                    && generation->Textures().size() == 3u
                    && generation->Skeleton() != nullptr
                    && generation->Animations().size() == 14u,
                    "SU mesh/material/texture 3/skeleton/animation 14 closure");
                if (generation->Meshes().size() == 1u)
                {
                    const assets::ModelMeshAsset& mesh = generation->Meshes().front();
                    check.Check(mesh.vertexAttributeMask
                        == assets::kCoreColorSkinVertexAttributes,
                        "SU full core|color|skin mask 보존");
                    check.Check(mesh.vertexStride
                            == assets::StrideOf(mesh.vertexAttributeMask)
                        && mesh.vertexStride == 84u,
                        "SU stride 84를 기술표에서 유도");
                    check.Check(assets::OffsetOf(mesh.vertexAttributeMask,
                            assets::VertexAttribute::BoneIndices) == 64u
                        && assets::OffsetOf(mesh.vertexAttributeMask,
                            assets::VertexAttribute::BoneWeights) == 68u,
                        "SU bone offset 64/68을 기술표에서 유도");
                    RHIModelMeshView view{};
                    check.Check(BuildRHIModelMeshView(*generation, 0u, view)
                        && view.IsComplete() && view.handle.modelId
                            == generation->Identity().modelId
                        && view.handle.meshId == mesh.meshId
                        && view.handle.generation
                            == generation->Identity().generation,
                        "SU generation descriptor에서 typed RHI view 직접 생성");
                    suRenderSummary = "  su mask="
                        + std::to_string(mesh.vertexAttributeMask)
                        + " stride=" + std::to_string(mesh.vertexStride)
                        + " boneIndices=" + std::to_string(assets::OffsetOf(
                            mesh.vertexAttributeMask,
                            assets::VertexAttribute::BoneIndices))
                        + " boneWeights=" + std::to_string(assets::OffsetOf(
                            mesh.vertexAttributeMask,
                            assets::VertexAttribute::BoneWeights))
                        + " rhiView=" + (view.IsComplete() ? "1" : "0") + "\n";
                }
            }
        }
        check.Check(sawGunner, "Gunner_F_Mythic corpus 포함");
        check.Check(sawSu, "SU_Mythic corpus 포함");
        const assets::ModelAssetGenerationCacheSnapshot snapshot = cache.Snapshot();
        check.Check(snapshot.currentAssets == loadedCount
            && snapshot.addressableGenerations == loadedCount
            && snapshot.publishes == loadedCount,
            "corpus cache current/addressable/publish 계수 일치");

        outLog += "  corpus models=" + std::to_string(sources.size())
            + " loaded=" + std::to_string(loadedCount)
            + " unique=" + std::to_string(modelIds.size())
            + " meshes=" + std::to_string(meshCount)
            + " materials=" + std::to_string(materialCount)
            + " textures=" + std::to_string(textureCount)
            + " skeletons=" + std::to_string(skeletonCount)
            + " animations=" + std::to_string(animationCount)
            + " descriptors=" + std::to_string(descriptorCount) + "\n";
        outLog += suRenderSummary;
        outLog += "  단정 " + std::to_string(check.passed + check.failed)
            + "건 중 통과 " + std::to_string(check.passed) + " · 실패 "
            + std::to_string(check.failed) + "\n";
        outLog += "  corpus-assertions total="
            + std::to_string(check.passed + check.failed)
            + " passed=" + std::to_string(check.passed)
            + " failed=" + std::to_string(check.failed) + "\n";
        return check.failed == 0u && loadedCount == sources.size();
    }
}
