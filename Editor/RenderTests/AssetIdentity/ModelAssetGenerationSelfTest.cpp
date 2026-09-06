#include "AssetIdentity/ModelAssetGenerationSelfTest.h"

#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelSidecarV2.h"
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
    }

    bool RunModelAssetGenerationSelfTest(const std::string& projectRoot,
        std::string& outLog)
    {
        GenerationChecker check{ outLog };
        outLog += "[assets.generation] MBC5 immutable aggregate·atomic cache 검사\n";

        const std::filesystem::path project(projectRoot);
        const std::filesystem::path header =
            project / "ProjectSetting" / "AssetIdentity.asset";
        const std::filesystem::path generationRoot =
            project / "Library" / "ModelAssetGenerations";
        const std::filesystem::path source =
            project / "Assets" / "Models" / "Prim_Cube.glb";
        std::filesystem::path canonical = source;
        canonical += ".meta";
        check.Check(std::filesystem::is_regular_file(header), "fixture epoch header 존재");
        check.Check(std::filesystem::is_regular_file(canonical), "fixture canonical sidecar 존재");
        check.Check(std::filesystem::is_directory(generationRoot), "fixture generation root 존재");

        std::vector<std::filesystem::path> modelDirectories;
        std::error_code error;
        if (std::filesystem::is_directory(generationRoot, error) && !error)
        {
            for (const std::filesystem::directory_entry& entry
                : std::filesystem::directory_iterator(generationRoot, error))
            {
                if (!error && entry.is_directory()) modelDirectories.push_back(entry.path());
            }
        }
        check.Check(modelDirectories.size() == 1u, "fixture ModelId directory 정확히 1개");
        if (modelDirectories.size() != 1u)
        {
            outLog += "  단정 " + std::to_string(check.passed + check.failed)
                + "건 중 통과 " + std::to_string(check.passed) + " · 실패 "
                + std::to_string(check.failed) + "\n";
            outLog += "  assertions total="
                + std::to_string(check.passed + check.failed)
                + " passed=" + std::to_string(check.passed)
                + " failed=" + std::to_string(check.failed) + "\n";
            return false;
        }

        Uuid::Uuid16 modelId{};
        check.Check(assets::TryParseCanonicalUuidV8(
            modelDirectories.front().filename().string(), modelId),
            "generation directory가 canonical UUIDv8 ModelId");
        const std::filesystem::path onePath = modelDirectories.front() / "1";
        const std::filesystem::path twoPath = modelDirectories.front() / "2";
        check.Check(std::filesystem::is_directory(onePath), "generation 1 존재");
        check.Check(std::filesystem::is_directory(twoPath), "generation 2 존재");

        const assets::ModelAssetGenerationLoadResult one =
            Load(header, onePath, modelId, 1u);
        const assets::ModelAssetGenerationLoadResult two =
            Load(header, twoPath, modelId, 2u, canonical);
        check.Check(one.Succeeded(), "generation 1 전체 closure load");
        check.Check(two.Succeeded(), "generation 2 + canonical sidecar load");
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
            return false;
        }

        const auto first = one.generation;
        const auto second = two.generation;
        check.Check(first->Identity().modelId == second->Identity().modelId
            && first->Identity().generation == 1u
            && second->Identity().generation == 2u,
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
            "Prim_Cube embedded texture가 generation closure에 포함됨");
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
            first->Meshes().front().meshId, 1u };
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
        check.Check(first->Identity().generation == 1u && !first->Meshes().empty(),
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
                ? Load(header, bad, modelId, 2u)
                : assets::ModelAssetGenerationLoadResult{};
            check.Check(mutated && !rejected.Succeeded()
                && HasIssue(rejected, expected),
                std::string(name) + " 게시 전 거부");
            check.Check(cache.ResolveCurrent(modelId) == second,
                std::string(name) + " 실패 뒤 current generation 불변");
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
        check.Check(second->Identity().generation == 2u
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
            + " tamper=4"
            + " replacements=" + std::to_string(snapshot.replacements)
            + " retires=" + std::to_string(snapshot.retires) + "\n";
        outLog += "  단정 " + std::to_string(check.passed + check.failed)
            + "건 중 통과 " + std::to_string(check.passed) + " · 실패 "
            + std::to_string(check.failed) + "\n";
        outLog += "  assertions total="
            + std::to_string(check.passed + check.failed)
            + " passed=" + std::to_string(check.passed)
            + " failed=" + std::to_string(check.failed) + "\n";
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
