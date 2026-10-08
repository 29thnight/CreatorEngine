// Focused CEMF3 source/codec regression assertions. Added for later authorized
// execution; this implementation pass deliberately did not build or run them.
#include "Experiment/Cooked/CookedModelSubAssetCodec.h"
#include "Experiment/Cooked/ModelAssetSetProducer.h"
#include "Experiment/Cooked/CookSupport.h"
#include "Assets/ModelSourcePreparation.h"
#include "Experiment/Import/SceneToModelDraft.h"
#include "Experiment/Import/MeshletBuilder.h"
#include "Experiment/Import/MeshLodBuilder.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    namespace ck = experiment::cooked;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    experiment::AssetId V8(std::uint8_t seed)
    {
        experiment::AssetId id;
        id.value.data[0] = seed;
        id.value.data[6] = 0x80u;
        id.value.data[8] = 0x80u;
        return id;
    }

    void VerifyCodecBoundsAndLayout()
    {
        ck::SkeletonArtifact skeleton;
        skeleton.skeleton.rootBone = experiment::BoneIndex(0u);
        skeleton.skeleton.bones = {
            { "Root", {}, math::matrix4x4::identity() },
            { "Left", experiment::BoneIndex(0u), math::matrix4x4::identity() },
            { "Right", experiment::BoneIndex(0u), math::matrix4x4::identity() } };
        std::string failure;
        Require(ck::ComputeBoneLayoutDigest(skeleton.skeleton, skeleton.boneLayoutSha256, failure), "layout hash");
        ck::AnimationClipArtifact clip;
        clip.skeletonAssetId = V8(1u);
        clip.requiredBoneCount = 3u;
        clip.requiredBoneLayoutSha256 = skeleton.boneLayoutSha256;
        clip.clip.name = "Selected";
        clip.clip.durationTicks = 30.0;
        clip.clip.ticksPerSecond = 30.0;
        experiment::AnimationChannel channel;
        channel.bone = experiment::BoneIndex(1u);
        channel.translations = { { 0.0, {} }, { 30.0, { 1.0f, 0.0f, 0.0f } } };
        clip.clip.channels.push_back(channel);
        std::vector<std::byte> clipBytes;
        std::vector<std::byte> skeletonBytes;
        Require(ck::WriteAnimationClipArtifact(clip, clipBytes, failure), "clip encode");
        Require(ck::WriteSkeletonArtifact(skeleton, skeletonBytes, failure), "skeleton encode");
        ck::AnimationClipArtifact decoded;
        ck::SkeletonArtifact decodedSkeleton;
        Require(ck::ReadAnimationClipArtifact(clipBytes, decoded, failure) &&
            ck::ReadSkeletonArtifact(skeletonBytes, decodedSkeleton, failure) &&
            ck::ValidateAnimationClipBinding(decoded, decodedSkeleton, failure), "exact typed roundtrip");
        for (std::size_t size = 0; size < clipBytes.size(); ++size)
        {
            Require(!ck::ReadAnimationClipArtifact(std::span(clipBytes).first(size), decoded, failure), "truncated clip accepted");
        }
        for (std::size_t size = 0; size < skeletonBytes.size(); ++size)
        {
            Require(!ck::ReadSkeletonArtifact(std::span(skeletonBytes).first(size), decodedSkeleton, failure), "truncated skeleton accepted");
        }
        Require(!ck::ReadSkeletonArtifact(clipBytes, decodedSkeleton, failure), "wrong typed magic accepted");
        auto corrupt = clipBytes;
        corrupt.push_back(std::byte{});
        Require(!ck::ReadAnimationClipArtifact(corrupt, decoded, failure), "trailing clip bytes accepted");
        corrupt = clipBytes;
        corrupt[14] = std::byte{ 0x40 }; // skeleton UUID version nibble (8-byte header + 6).
        Require(!ck::ReadAnimationClipArtifact(corrupt, decoded, failure), "UUIDv4 skeleton identity accepted");
        auto reordered = skeleton;
        std::swap(reordered.skeleton.bones[1].name, reordered.skeleton.bones[2].name);
        Require(ck::ComputeBoneLayoutDigest(reordered.skeleton, reordered.boneLayoutSha256, failure) &&
            !ck::ValidateAnimationClipBinding(clip, reordered, failure), "same-count reordered layout accepted");
        auto posed = skeleton;
        posed.skeleton.bones[1].inverseBindMatrix.m[3][0] = 4.0f;
        posed.skeleton.rootTransform.m[3][1] = 2.0f;
        Require(ck::ComputeBoneLayoutDigest(posed.skeleton, posed.boneLayoutSha256, failure) &&
            posed.boneLayoutSha256 == skeleton.boneLayoutSha256 &&
            ck::ValidateAnimationClipBinding(clip, posed, failure), "pose-only edit invalidated layout");
        std::vector<std::byte> unchanged;
        Require(ck::WriteAnimationClipArtifact(clip, unchanged, failure) && unchanged == clipBytes, "pose-only edit changed clip bytes");
        std::vector<std::byte> changedSkeleton;
        Require(ck::WriteSkeletonArtifact(posed, changedSkeleton, failure) && changedSkeleton != skeletonBytes, "pose edit lost from skeleton bytes");
        clip.clip.channels[0].translations[1].time = 0.0;
        Require(!ck::WriteAnimationClipArtifact(clip, unchanged, failure), "duplicate key time accepted");
        ck::ModelDescriptorArtifact descriptor{ V8(2u), V8(1u), "Model", { { V8(3u), "Selected", 30.0, 30.0, true } } };
        Require(ck::WriteModelDescriptorArtifact(descriptor, unchanged, failure), "descriptor encode");
        ck::ModelDescriptorArtifact restored;
        Require(ck::ReadModelDescriptorArtifact(unchanged, restored, failure) && restored.clips.size() == 1u,
            "descriptor roundtrip");
        descriptor.clips.push_back(descriptor.clips.front());
        Require(!ck::WriteModelDescriptorArtifact(descriptor, unchanged, failure), "duplicate descriptor identity accepted");
    }

    void VerifyGeometryCodecAndMetadata()
    {
        ck::SkeletonArtifact skeleton;
        skeleton.skeleton.rootBone = experiment::BoneIndex(0u);
        skeleton.skeleton.bones = { { "Root", {}, math::matrix4x4::identity() } };
        std::string failure;
        Require(ck::ComputeBoneLayoutDigest(skeleton.skeleton, skeleton.boneLayoutSha256, failure), "geometry layout hash");
        ck::Sha256Digest binding;
        Require(ck::ComputeSkinBindingDigest(skeleton.skeleton, binding, failure), "full skin binding hash");
        for (const auto attributes : experiment::kModelVertexMasks)
        {
            ck::ModelGeometryArtifact value;
            Require(value.mesh.vertices.SetLayout(attributes), "fixture geometry layout");
            const math::vector2 uv1{ 0.25f, 0.75f };
            const math::vector4 color{ 0.1f, 0.2f, 0.3f, 0.4f };
            const bool skinned = experiment::Has(attributes, experiment::VertexAttribute::BoneIndices);
            for (const math::vector3 position : { math::vector3{ 0.0f, 0.0f, 0.0f },
                math::vector3{ 1.0f, 0.0f, 0.0f }, math::vector3{ 0.0f, 1.0f, 0.0f } })
            {
                experiment::Vertex vertex;
                vertex.position = position;
                vertex.normal = { 0.0f, 0.0f, 1.0f };
                vertex.tangent = { 1.0f, 0.0f, 0.0f, -1.0f };
                if (skinned)
                {
                    vertex.boneIndices[0] = 0u;
                    vertex.boneWeights[0] = 1.0f;
                }
                Require(value.mesh.vertices.Append(vertex, &uv1, &color), "fixture geometry vertex");
            }
            value.mesh.indices = { 0u, 1u, 2u };
            value.mesh.bounds = math::aabb::from_min_max({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f });
            if (skinned)
            {
                value.requiredBoneCount = 1u;
                value.requiredSkinBindingSha256 = binding;
                Require(ck::ValidateModelGeometryBinding(value, skeleton, failure), "valid geometry binding rejected");
                auto movedBind = skeleton;
                movedBind.skeleton.bones[0].inverseBindMatrix.m[3][0] = 2.0f;
                Require(!ck::ValidateModelGeometryBinding(value, movedBind, failure), "geometry accepted changed inverse bind");
                movedBind = skeleton;
                movedBind.skeleton.rootTransform.m[3][1] = 2.0f;
                Require(!ck::ValidateModelGeometryBinding(value, movedBind, failure), "geometry accepted changed root transform");
                movedBind = skeleton;
                movedBind.skeleton.globalInverseTransform.m[3][2] = 2.0f;
                Require(!ck::ValidateModelGeometryBinding(value, movedBind, failure), "geometry accepted changed global inverse");
            }
            Require(experiment::importer::BuildMeshlets(value.mesh, value.mesh.meshlets, failure), "fixture meshlets");
            std::vector<std::byte> bytes;
            Require(ck::WriteModelGeometryArtifact(value, bytes, failure), "geometry encode");
            value.mesh.name = "Logical mesh name must not be in shared bytes";
            value.mesh.material = experiment::MaterialIndex(123u);
            std::vector<std::byte> renamed;
            Require(ck::WriteModelGeometryArtifact(value, renamed, failure) && renamed == bytes,
                "geometry blob contains logical mesh/material metadata");
            ck::ModelGeometryArtifact decoded;
            std::vector<std::string> warnings;
            Require(ck::ReadModelGeometryArtifact(bytes, decoded, failure, &warnings) && warnings.empty(), "geometry decode");
            Require(decoded.mesh.name.empty() && !decoded.mesh.material.IsValid() &&
                decoded.mesh.vertices.AttributeMask() == attributes && decoded.mesh.vertices.Stride() == experiment::StrideOf(attributes) &&
                std::ranges::equal(decoded.mesh.vertices.Bytes(), value.mesh.vertices.Bytes()) &&
                decoded.mesh.indices == value.mesh.indices && decoded.mesh.meshlets.HasMeshlets(), "geometry packed ABI changed");
            ck::ModelMeshSummary summary{ V8(10u), {}, "Mesh", value.mesh.bounds, attributes,
                value.mesh.vertices.Stride(), 3u, 3u, skinned };
            Require(ck::ValidateModelMeshSummary(summary, decoded, failure), "matching metadata rejected");
            summary.stride += 4u;
            Require(!ck::ValidateModelMeshSummary(summary, decoded, failure), "metadata layout mismatch accepted");
            summary.stride -= 4u;
            for (std::size_t size = 0; size < bytes.size(); ++size)
            {
                Require(!ck::ReadModelGeometryArtifact(std::span(bytes).first(size), decoded, failure), "truncated geometry accepted");
            }
            auto corrupt = bytes;
            corrupt.push_back(std::byte{});
            Require(!ck::ReadModelGeometryArtifact(corrupt, decoded, failure), "trailing geometry accepted");
            corrupt = bytes;
            corrupt[8] ^= std::byte{ 1u };
            Require(!ck::ReadModelGeometryArtifact(corrupt, decoded, failure), "geometry ABI mismatch accepted");
            // 100B 정본 헤더와 vertex/index 뒤의 선택적 meshlet digest만 손상시킨다.
            // derived 데이터 오류가 정상 indexed geometry까지 버리면 안 된다.
            const auto meshletSection = 100u + 3u * value.mesh.vertices.Stride() + 3u * 4u;
            corrupt = bytes;
            corrupt[meshletSection + 4u + 24u] ^= std::byte{ 1u };
            Require(ck::ReadModelGeometryArtifact(corrupt, decoded, failure, &warnings) &&
                !warnings.empty() && decoded.mesh.meshlets.IsEmpty() && decoded.mesh.indices == value.mesh.indices,
                "optional meshlet corruption lost valid indexed geometry");
            if (skinned)
            {
                corrupt = bytes;
                const auto offset = 100u + experiment::OffsetOf(attributes, experiment::VertexAttribute::BoneIndices);
                corrupt[offset] = std::byte{ 255u };
                Require(!ck::ReadModelGeometryArtifact(corrupt, decoded, failure), "weighted unused bone index accepted");
                corrupt[offset] = std::byte{ 1u };
                Require(!ck::ReadModelGeometryArtifact(corrupt, decoded, failure), "out-of-skeleton bone index accepted");
            }
            // 정본 vertex만 바꾼 malformed wire와 writer 입력을 함께 검사한다.
            // writer의 derived digest 오류가 정점 계약 누락을 가리지 않게 derived는 비운다.
            const auto rejectVertexValues = [&](experiment::VertexAttribute attribute,
                std::initializer_list<float> scalars,
                const std::array<std::uint8_t, experiment::MaxBoneInfluences>* bones,
                const char* expectedFailure)
            {
                auto invalid = value;
                invalid.mesh.meshlets = {};
                invalid.mesh.coarseLods = {};
                auto malformed = bytes;
                std::vector<std::byte> packed(value.mesh.vertices.Bytes().begin(), value.mesh.vertices.Bytes().end());
                const auto begin = experiment::OffsetOf(attributes, attribute);
                std::size_t component{};
                for (const float scalar : scalars)
                {
                    const auto offset = begin + component * sizeof(float);
                    std::memcpy(packed.data() + offset, &scalar, sizeof(scalar));
                    const auto bits = std::bit_cast<std::uint32_t>(scalar);
                    for (unsigned byte = 0; byte < 4u; ++byte)
                    {
                        malformed[100u + offset + byte] = static_cast<std::byte>((bits >> (byte * 8u)) & 0xffu);
                    }
                    ++component;
                }
                if (bones)
                {
                    const auto offset = experiment::OffsetOf(attributes, experiment::VertexAttribute::BoneIndices);
                    for (std::size_t slot = 0; slot < bones->size(); ++slot)
                    {
                        packed[offset + slot] = static_cast<std::byte>((*bones)[slot]);
                        malformed[100u + offset + slot] = static_cast<std::byte>((*bones)[slot]);
                    }
                }
                invalid.mesh.vertices = {};
                Require(invalid.mesh.vertices.AssignPacked(attributes, 3u, packed), "malformed vertex fixture packing");
                const std::vector<std::byte> sentinel{ std::byte{ 0xa5u } };
                auto untouched = sentinel;
                Require(!ck::WriteModelGeometryArtifact(invalid, untouched, failure) && untouched == sentinel &&
                    failure.find(expectedFailure) != std::string::npos, "invalid render vertex accepted by writer");
                const auto priorVertexBytes = std::vector<std::byte>(decoded.mesh.vertices.Bytes().begin(),
                    decoded.mesh.vertices.Bytes().end());
                Require(!ck::ReadModelGeometryArtifact(malformed, decoded, failure) &&
                    failure.find(expectedFailure) != std::string::npos &&
                    std::ranges::equal(decoded.mesh.vertices.Bytes(), priorVertexBytes),
                    "malformed render vertex wire accepted or changed output");
            };
            rejectVertexValues(experiment::VertexAttribute::Normal, { 0.0f, 0.0f, 0.0f }, nullptr, "nonzero normal");
            rejectVertexValues(experiment::VertexAttribute::Normal, { 1e-12f, 0.0f, 0.0f }, nullptr, "nonzero normal");
            rejectVertexValues(experiment::VertexAttribute::Normal,
                { (std::numeric_limits<float>::infinity)(), 0.0f, 0.0f }, nullptr, "finite");
            rejectVertexValues(experiment::VertexAttribute::Normal, { 1000001.0f, 0.0f, 0.0f }, nullptr, "finite");
            rejectVertexValues(experiment::VertexAttribute::Tangent, { 1.0f, 0.0f, 0.0f, 0.0f }, nullptr, "handedness");
            rejectVertexValues(experiment::VertexAttribute::Tangent, { 1.0f, 0.0f, 0.0f, 0.5f }, nullptr, "handedness");
            if (skinned)
            {
                rejectVertexValues(experiment::VertexAttribute::BoneWeights, { 2.0f, 0.0f, 0.0f, 0.0f },
                    nullptr, "range 0..1");
                rejectVertexValues(experiment::VertexAttribute::BoneWeights, { 0.5f, 0.0f, 0.0f, 0.0f },
                    nullptr, "sum to one");
                const std::array<std::uint8_t, experiment::MaxBoneInfluences> laterBone{ 255u, 0u, 255u, 255u };
                rejectVertexValues(experiment::VertexAttribute::BoneWeights, { 0.0f, 1.0f, 0.0f, 0.0f },
                    &laterBone, "positive first slot");
                auto bindPose = value;
                bindPose.mesh.meshlets = {};
                bindPose.mesh.coarseLods = {};
                bindPose.mesh.vertices = {};
                Require(bindPose.mesh.vertices.SetLayout(attributes), "bind-pose fixture layout");
                for (std::size_t index = 0; index < value.mesh.vertices.size(); ++index)
                {
                    auto vertex = value.mesh.vertices[index];
                    vertex.boneIndices.fill(experiment::InvalidPackedBoneIndex);
                    vertex.boneWeights.fill(0.0f);
                    Require(bindPose.mesh.vertices.Append(vertex, &uv1, &color), "bind-pose fixture vertex");
                }
                std::vector<std::byte> bindPoseBytes;
                ck::ModelGeometryArtifact restoredBindPose;
                Require(ck::WriteModelGeometryArtifact(bindPose, bindPoseBytes, failure) &&
                    ck::ReadModelGeometryArtifact(bindPoseBytes, restoredBindPose, failure) &&
                    restoredBindPose.mesh.vertices[0].boneWeights[0] == 0.0f,
                    "all-zero skin weights must retain the bind-pose path");
            }
            ck::ModelDescriptorArtifact descriptor;
            descriptor.modelAssetId = V8(20u);
            descriptor.skeletonAssetId = skinned ? V8(21u) : experiment::AssetId{};
            descriptor.meshes.push_back(summary);
            descriptor.nodes.push_back({ "Root", {}, math::matrix4x4::identity(), { summary.meshAssetId } });
            descriptor.materials.push_back({ V8(98u), "Surface", experiment::MaterialBlendMode::Opaque });
            descriptor.meshes[0].materialAssetId = V8(98u);
            std::vector<ck::AssetDependency> edges{
                { { { summary.meshAssetId, {} }, ck::CookedAssetKind::Mesh }, ck::AssetDependencyKind::Loadable,
                    ck::AssetDependencyScope::External } };
            if (skinned)
            {
                edges.push_back({ { { descriptor.skeletonAssetId, {} }, ck::CookedAssetKind::Skeleton },
                    ck::AssetDependencyKind::Loadable, ck::AssetDependencyScope::Internal });
            }
            Require(!ck::ValidateModelDescriptorDependencies(descriptor, edges, failure),
                "model material summary without a typed Loadable edge accepted");
            edges.push_back({ { { V8(98u), {} }, ck::CookedAssetKind::Material },
                ck::AssetDependencyKind::Loadable, ck::AssetDependencyScope::Internal });
            Require(ck::ValidateModelDescriptorDependencies(descriptor, edges, failure), "valid lazy descriptor edges rejected");
            Require(ck::WriteModelDescriptorArtifact(descriptor, renamed, failure), "geometry descriptor encode");
            ck::ModelDescriptorArtifact restored;
            Require(ck::ReadModelDescriptorArtifact(renamed, restored, failure) && restored.meshes.size() == 1u &&
                restored.nodes[0].meshAssetIds[0] == summary.meshAssetId, "geometry descriptor roundtrip");
            renamed[4] = std::byte{ 1u };
            Require(!ck::ReadModelDescriptorArtifact(renamed, restored, failure), "descriptor v1 accepted without recook");
            renamed[4] = std::byte{ 2u };
            Require(!ck::ReadModelDescriptorArtifact(renamed, restored, failure), "descriptor v2 accepted without material-edge recook");
            edges[0].kind = ck::AssetDependencyKind::Hard;
            Require(!ck::ValidateModelDescriptorDependencies(descriptor, edges, failure), "eager geometry descriptor edge accepted");
            descriptor.nodes[0].meshAssetIds[0] = V8(99u);
            Require(!ck::WriteModelDescriptorArtifact(descriptor, renamed, failure), "unknown node mesh identity accepted");
        }
    }

    void VerifySelectedMeshConversion()
    {
        namespace im = experiment::importer;
        im::ImportedScene scene;
        scene.nodes.resize(2u); // root 둘을 써서 기존 합성 루트 정책의 일치를 확인한다.
        scene.nodes[0].name = "First";
        scene.nodes[0].meshes = { im::ImportMeshIndex(0u) };
        scene.nodes[1].name = "Second";
        scene.nodes[1].meshes = { im::ImportMeshIndex(1u) };
        scene.meshes.resize(2u);
        auto& selected = scene.meshes[0];
        selected.name = "Selected";
        selected.streams.positions = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
        selected.streams.uv1 = { { 0.2f, 0.4f }, { 0.4f, 0.6f }, { 0.6f, 0.8f } };
        selected.streams.colors = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } };
        selected.streams.tangents = { { 1.0f, 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f, -1.0f } };
        selected.indices = { 0u, 1u, 2u };
        scene.meshes[1] = selected;
        // 형제의 보조 stream 길이를 깨뜨려 선택 변환이 형제를 packing하지 않음을 확인한다.
        // 설명 정보는 positions만 읽으므로 이 stream을 열지 않아야 한다.
        scene.meshes[1].streams.uv1.resize(1u);
        const auto converted = im::ConvertToMesh(scene, 0u);
        const auto metadata = im::ConvertToModelMetadata(scene, {});
        Require(converted.mesh.has_value() && metadata.succeeded && metadata.nodes.size() == 3u &&
            metadata.nodes[1].parent == experiment::NodeIndex(0u) && metadata.meshes.size() == 2u,
            "selected conversion or metadata-only synthetic root failed");
        Require(converted.mesh->vertices[0].tangent.w == -1.0f && converted.mesh->vertices.Uv1(0u)->x == 0.2f &&
            converted.mesh->vertices.Color(1u)->y == 1.0f &&
            metadata.meshes[0].attributes == converted.mesh->vertices.AttributeMask(), "selected conversion lost packed attributes");
        assets::ModelGeometryImportSettings settings;
        std::string failure;
        Require(assets::ReadModelGeometryImportSettings("importSettings:\n  buildMeshlets: true\n  lodLevels: 3\n", settings, failure) &&
            settings.buildMeshlets && settings.lodLevels == 3u, "persisted geometry settings lost");
        Require(!assets::ReadModelGeometryImportSettings("importSettings:\n  buildMeshlets: maybe\n", settings, failure),
            "invalid persisted meshlet setting accepted");
        Require(!assets::ReadModelGeometryImportSettings("importSettings:\n  lodLevels: 8\n", settings, failure),
            "invalid persisted LOD count accepted");
    }

    struct TemporaryProject final
    {
        std::filesystem::path path{};
        ~TemporaryProject()
        {
            if (!path.empty())
            {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        }
    };

    void Write(const std::filesystem::path& path, std::span<const std::byte> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(stream), "fixture write");
    }

    void Write(const std::filesystem::path& path, const std::string& text)
    {
        Write(path, std::as_bytes(std::span(text.data(), text.size())));
    }

    std::string Read(const std::filesystem::path& path)
    {
        std::string text;
        Require(ck::ReadTextFile(path, text), "fixture read");
        return text;
    }

    void VerifySelectedSourceCook()
    {
        TemporaryProject project;
        const auto candidate = std::filesystem::temp_directory_path() /
            ("ce-granular-source-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Require(std::filesystem::create_directory(candidate), "fixture directory collision");
        project.path = candidate;
        const auto root = project.path / "Assets";
        const auto source = root / "Probe.gltf";
        const auto bufferPath = root / "Probe.bin";
        const auto metaPath = root / "Probe.gltf.meta";
        const auto headerPath = project.path / "ProjectSetting" / "AssetIdentity.asset";
        const std::string document = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Root","translation":[0,0,0]}],"skins":[{"name":"Rig","skeleton":0,"joints":[0]}],"buffers":[{"uri":"Probe.bin","byteLength":56}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":24},{"buffer":0,"byteOffset":32,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":2,"type":"VEC3"}],"animations":[{"name":"Idle","samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]},{"name":"Walk","samplers":[{"input":0,"output":2}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}]})";
        std::vector<std::byte> buffer;
        for (const float value : { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f })
        {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned byte = 0; byte < 4u; ++byte)
            {
                buffer.push_back(static_cast<std::byte>((bits >> (byte * 8u)) & 0xffu));
            }
        }
        Write(source, document);
        Write(bufferPath, buffer);
        auto importer = assets::CreateModelSourceImporter(source);
        Require(static_cast<bool>(importer), "fixture importer");
        auto imported = importer->Import({ source });
        Require(imported.Succeeded(), "fixture import");
        auto elements = assets::CollectStableKeyElements(*imported.scene);
        std::string failure;
        Require(assets::NormalizeModelStableInputs(elements, failure), "fixture normalize");
        const auto keys = assets::DeriveModelStableKeys(elements, {});
        Require(keys.Succeeded(), "fixture stable keys");
        assets::IdentityEpochHeader header;
        header.identityEpoch = "granular-fixture";
        header.identityEpochSeed[0] = 1u;
        header.createdAt = "2026-10-08T00:00:00Z";
        Write(headerPath, assets::WriteIdentityEpochHeader(header));
        assets::ModelSidecarV2 sidecar;
        std::vector<assets::SidecarIssue> issues;
        const auto fingerprint = assets::MakeSourceFingerprint(std::span(
            reinterpret_cast<const std::uint8_t*>(document.data()), document.size()));
        Require(assets::BuildModelSidecarV2(header, "exporter:granular-fixture", 1u, fingerprint,
            keys.assignments, sidecar, issues), "fixture sidecar");
        Write(metaPath, assets::WriteModelSidecarV2(sidecar, {}, issues));
        const auto originalMeta = Read(metaPath);
        experiment::AssetId skeletonId;
        experiment::AssetId idleId;
        experiment::AssetId walkId;
        for (const auto& record : sidecar.subAssets)
        {
            if (record.kind == assets::SubAssetKind::Skeleton)
            {
                skeletonId = experiment::AssetId{ record.assetId };
            }
            if (record.kind == assets::SubAssetKind::Animation && record.name == "Idle")
            {
                idleId = experiment::AssetId{ record.assetId };
            }
            if (record.kind == assets::SubAssetKind::Animation && record.name == "Walk")
            {
                walkId = experiment::AssetId{ record.assetId };
            }
        }
        Require(skeletonId.IsValid() && idleId.IsValid() && walkId.IsValid(), "fixture authored identities");
        ck::ModelAssetSetCookRequest request{ source, root, headerPath,
            { { { idleId, {} }, ck::CookedAssetKind::AnimationClip },
              { { skeletonId, {} }, ck::CookedAssetKind::Skeleton } } };
        const auto selected = ck::BuildModelAssetSetProducts(request);
        Require(selected.Succeeded() && selected.products.size() == 2u, "selected source cook");
        Require(std::ranges::none_of(selected.products, [&](const auto& product)
            { return product.asset.key.assetId == walkId; }), "unselected sibling was emitted");
        Require(selected.products[0].dependencies.size() == 1u &&
            selected.products[0].dependencies[0].kind == ck::AssetDependencyKind::Hard &&
            selected.products[0].dependencies[0].target.key.assetId == skeletonId, "clip hard skeleton edge");
        Require(Read(metaPath) == originalMeta, "source cook mutated canonical identity/generation");
        auto descriptorRequest = request;
        descriptorRequest.selected = { { { experiment::AssetId{ sidecar.assetId }, {} }, ck::CookedAssetKind::Model } };
        const auto descriptorProduct = ck::BuildModelAssetSetProducts(descriptorRequest);
        ck::ModelDescriptorArtifact descriptor;
        Require(descriptorProduct.Succeeded() && descriptorProduct.products.size() == 1u &&
            ck::ReadModelDescriptorArtifact(descriptorProduct.products[0].artifactBytes, descriptor, failure) &&
            descriptor.clips.size() == 2u && descriptor.clips[0].clipAssetId == idleId &&
            descriptor.clips[1].clipAssetId == walkId, "descriptor changed persisted source clip index order");
        // Change only sibling Walk's external-buffer key: root and sidecar stay identical.
        const auto changed = std::bit_cast<std::uint32_t>(2.0f);
        for (unsigned byte = 0; byte < 4u; ++byte)
        {
            buffer[44u + byte] = static_cast<std::byte>((changed >> (byte * 8u)) & 0xffu);
        }
        Write(bufferPath, buffer);
        const auto siblingEdit = ck::BuildModelAssetSetProducts(request);
        Require(siblingEdit.Succeeded() && siblingEdit.sourceInputsSha256 != selected.sourceInputsSha256 &&
            siblingEdit.products[0].artifactBytes == selected.products[0].artifactBytes,
            "external sibling edit was omitted from source digest or invalidated selected clip bytes");
        request.selected[0].key.assetId = V8(99u);
        Require(!ck::BuildModelAssetSetProducts(request).Succeeded(), "unauthored UUIDv8 selection accepted");
        request.selected[0].key.assetId = idleId;
        std::filesystem::remove(bufferPath);
        Require(!ck::BuildModelAssetSetProducts(request).Succeeded(), "missing external buffer accepted");
        Require(Read(metaPath) == originalMeta, "failed source cook mutated canonical metadata");
    }
    void VerifySelectedMeshSourceCook()
    {
        TemporaryProject project;
        const auto candidate = std::filesystem::temp_directory_path() /
            ("ce-selected-mesh-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Require(std::filesystem::create_directory(candidate), "mesh fixture directory collision");
        project.path = candidate;
        const auto root = project.path / "Assets";
        const auto source = root / "Meshes.gltf";
        const auto bufferPath = root / "Meshes.bin";
        const auto metaPath = root / "Meshes.gltf.meta";
        const auto headerPath = project.path / "ProjectSetting" / "AssetIdentity.asset";
        const std::string document = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Root","children":[1,2]},{"name":"SelectedNode","mesh":0},{"name":"SiblingNode","mesh":1}],"buffers":[{"uri":"Meshes.bin","byteLength":72}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,0],"max":[0,1,0]}],"meshes":[{"name":"SelectedMesh","primitives":[{"attributes":{"POSITION":0}}]},{"name":"SiblingMesh","primitives":[{"attributes":{"POSITION":1}}]}]})";
        std::vector<std::byte> buffer;
        for (const float value : { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f })
        {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned byte = 0; byte < 4u; ++byte)
            {
                buffer.push_back(static_cast<std::byte>((bits >> (byte * 8u)) & 0xffu));
            }
        }
        Write(source, document);
        Write(bufferPath, buffer);
        auto importer = assets::CreateModelSourceImporter(source);
        Require(static_cast<bool>(importer), "mesh fixture importer");
        auto imported = importer->Import({ source });
        Require(imported.Succeeded() && imported.scene->meshes.size() == 2u, "mesh fixture import");
        auto elements = assets::CollectStableKeyElements(*imported.scene);
        std::string failure;
        Require(assets::NormalizeModelStableInputs(elements, failure), "mesh fixture stable inputs");
        const auto keys = assets::DeriveModelStableKeys(elements, {});
        Require(keys.Succeeded(), "mesh fixture stable keys");
        assets::IdentityEpochHeader header;
        header.identityEpoch = "mesh-fixture";
        header.identityEpochSeed[0] = 2u;
        header.createdAt = "2026-10-08T00:00:00Z";
        Write(headerPath, assets::WriteIdentityEpochHeader(header));
        assets::ModelSidecarV2 sidecar;
        std::vector<assets::SidecarIssue> issues;
        const auto fingerprint = assets::MakeSourceFingerprint(std::span(
            reinterpret_cast<const std::uint8_t*>(document.data()), document.size()));
        Require(assets::BuildModelSidecarV2(header, "exporter:mesh-fixture", 1u, fingerprint,
            keys.assignments, sidecar, issues), "mesh fixture sidecar");
        Write(metaPath, assets::WriteModelSidecarV2(sidecar,
            "importSettings:\n  buildMeshlets: true\n  lodLevels: 1\n", issues));
        const auto originalMeta = Read(metaPath);
        experiment::AssetId selectedId;
        for (const auto& binding : keys.assignments)
        {
            if (binding.kind == assets::SubAssetKind::Mesh && binding.index == 0u)
            {
                const auto found = std::ranges::find_if(sidecar.subAssets, [&](const auto& record)
                {
                    return record.kind == binding.kind && record.stableKey == binding.stableKey;
                });
                Require(found != sidecar.subAssets.end(), "mesh fixture authored binding");
                selectedId = experiment::AssetId{ found->assetId };
            }
        }
        ck::ModelAssetSetCookRequest request{ source, root, headerPath,
            { { { selectedId, {} }, ck::CookedAssetKind::Mesh } } };
        const auto selected = ck::BuildModelAssetSetProducts(request);
        Require(selected.Succeeded() && selected.products.size() == 1u &&
            selected.products[0].dependencies.empty() && selected.products[0].extension == ".cege",
            "selected static mesh emitted siblings or a skeleton edge");
        ck::ModelGeometryArtifact geometry;
        Require(ck::ReadModelGeometryArtifact(selected.products[0].artifactBytes, geometry, failure) &&
            geometry.mesh.meshlets.HasMeshlets() && geometry.requiredBoneCount == 0u &&
            !selected.warnings.empty(), "persisted meshlet/LOD fallback policy was lost");
        auto descriptorRequest = request;
        descriptorRequest.selected = { { { experiment::AssetId{ sidecar.assetId }, {} }, ck::CookedAssetKind::Model } };
        const auto descriptorProduct = ck::BuildModelAssetSetProducts(descriptorRequest);
        ck::ModelDescriptorArtifact descriptor;
        Require(descriptorProduct.Succeeded() && descriptorProduct.products.size() == 1u &&
            ck::ReadModelDescriptorArtifact(descriptorProduct.products[0].artifactBytes, descriptor, failure) &&
            descriptor.meshes.size() == 2u && descriptor.nodes.size() == 3u &&
            descriptorProduct.products[0].dependencies.size() == 2u &&
            ck::ValidateModelMeshSummary(descriptor.meshes[0], geometry, failure), "metadata-only source descriptor mismatch");
        const auto changed = std::bit_cast<std::uint32_t>(-2.0f);
        for (unsigned byte = 0; byte < 4u; ++byte)
        {
            buffer[36u + byte] = static_cast<std::byte>((changed >> (byte * 8u)) & 0xffu);
        }
        Write(bufferPath, buffer);
        const auto siblingEdit = ck::BuildModelAssetSetProducts(request);
        Require(siblingEdit.Succeeded() && siblingEdit.sourceInputsSha256 != selected.sourceInputsSha256 &&
            siblingEdit.products[0].artifactBytes == selected.products[0].artifactBytes,
            "sibling geometry changed selected artifact or escaped source capture");
        Require(Read(metaPath) == originalMeta, "mesh source cook mutated canonical metadata");
        request.selected[0].key.assetId = V8(99u);
        Require(!ck::BuildModelAssetSetProducts(request).Succeeded(), "unauthored Mesh UUIDv8 accepted");
    }

    void VerifyGeometryLodsAndPackedLimit()
    {
        ck::ModelGeometryArtifact value;
        for (std::uint32_t y = 0; y <= 8u; ++y)
        {
            for (std::uint32_t x = 0; x <= 8u; ++x)
            {
                experiment::Vertex vertex;
                vertex.position = { static_cast<float>(x), static_cast<float>(y), 0.0f };
                vertex.normal = { 0.0f, 0.0f, 1.0f };
                vertex.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                value.mesh.vertices.push_back(vertex);
                if (x < 8u && y < 8u)
                {
                    const auto first = y * 9u + x;
                    value.mesh.indices.insert(value.mesh.indices.end(),
                        { first, first + 1u, first + 9u, first + 1u, first + 10u, first + 9u });
                }
            }
        }
        value.mesh.bounds = math::aabb::from_min_max({ 0.0f, 0.0f, 0.0f }, { 8.0f, 8.0f, 0.0f });
        std::string failure;
        Require(experiment::importer::BuildMeshlets(value.mesh, value.mesh.meshlets, failure), "LOD fixture meshlets");
        experiment::MeshLodBuildSettings settings;
        settings.levelCount = 2u;
        Require(experiment::importer::BuildMeshLods(value.mesh, value.mesh.coarseLods, failure, settings) &&
            !value.mesh.coarseLods.levels.empty(), "LOD fixture reduction");
        std::vector<std::byte> bytes;
        Require(ck::WriteModelGeometryArtifact(value, bytes, failure), "LOD geometry encode");
        ck::ModelGeometryArtifact restored;
        std::vector<std::string> warnings;
        Require(ck::ReadModelGeometryArtifact(bytes, restored, failure, &warnings) && warnings.empty() &&
            restored.mesh.coarseLods.geometryDigest == value.mesh.coarseLods.geometryDigest &&
            restored.mesh.coarseLods.levels.size() == value.mesh.coarseLods.levels.size() &&
            restored.mesh.coarseLods.levels[0].indices == value.mesh.coarseLods.levels[0].indices,
            "LOD geometry roundtrip");
        auto corrupt = bytes;
        const auto meshletSection = 100u + value.mesh.vertices.ByteSize() + value.mesh.indices.size() * 4u;
        std::uint32_t meshletBytes{};
        for (unsigned byte = 0; byte < 4u; ++byte)
        {
            meshletBytes |= std::to_integer<std::uint32_t>(bytes[meshletSection + byte]) << (byte * 8u);
        }
        const auto lodSection = meshletSection + 4u + meshletBytes;
        corrupt[lodSection + 4u + 24u] ^= std::byte{ 1u };
        Require(ck::ReadModelGeometryArtifact(corrupt, restored, failure, &warnings) && !warnings.empty() &&
            restored.mesh.coarseLods.IsEmpty() && restored.mesh.meshlets.HasMeshlets() &&
            restored.mesh.indices == value.mesh.indices, "optional LOD corruption lost valid LOD0");
        ck::SkeletonArtifact skeleton;
        skeleton.skeleton.rootBone = experiment::BoneIndex(0u);
        for (std::uint32_t index = 0; index < 255u; ++index)
        {
            skeleton.skeleton.bones.push_back({ "Bone" + std::to_string(index),
                index == 0u ? experiment::BoneIndex{} : experiment::BoneIndex(0u), math::matrix4x4::identity() });
        }
        Require(ck::ComputeBoneLayoutDigest(skeleton.skeleton, skeleton.boneLayoutSha256, failure), "packed-limit layout");
        experiment::VertexBuffer skinned(experiment::kV2VertexAttributes);
        for (std::size_t index = 0; index < value.mesh.vertices.size(); ++index)
        {
            auto vertex = value.mesh.vertices[index];
            vertex.boneIndices[0] = 254u;
            vertex.boneWeights[0] = 1.0f;
            skinned.push_back(vertex);
        }
        value.mesh.vertices = std::move(skinned);
        value.mesh.meshlets = {};
        value.mesh.coarseLods = {};
        value.requiredBoneCount = 255u;
        Require(ck::ComputeSkinBindingDigest(skeleton.skeleton, value.requiredSkinBindingSha256, failure) &&
            ck::ValidateModelGeometryBinding(value, skeleton, failure) &&
            ck::WriteModelGeometryArtifact(value, bytes, failure) &&
            ck::ReadModelGeometryArtifact(bytes, restored, failure) && restored.mesh.vertices[0].boneIndices[0] == 254u,
            "highest supported packed bone index rejected");
    }

}

int main()
{
    try
    {
        VerifyCodecBoundsAndLayout();
        VerifyGeometryCodecAndMetadata();
        VerifySelectedMeshConversion();
        VerifySelectedSourceCook();
        VerifySelectedMeshSourceCook();
        VerifyGeometryLodsAndPackedLimit();
        std::cout << "MODEL_SUBASSET_CODEC_OK\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
