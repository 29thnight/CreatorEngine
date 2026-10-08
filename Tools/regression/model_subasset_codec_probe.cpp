// Focused CEMF3 source/codec regression assertions. Added for later authorized
// execution; this implementation pass deliberately did not build or run them.
#include "Experiment/Cooked/CookedModelSubAssetCodec.h"
#include "Experiment/Cooked/ModelAssetSetProducer.h"
#include "Experiment/Cooked/CookSupport.h"
#include "Assets/ModelSourcePreparation.h"

#include <algorithm>
#include <bit>
#include <chrono>
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
}

int main()
{
    try
    {
        VerifyCodecBoundsAndLayout();
        VerifySelectedSourceCook();
        std::cout << "MODEL_SUBASSET_CODEC_OK\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
