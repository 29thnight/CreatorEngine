#pragma once
#include "EnhancedCameraReplayInput.h"
#include "../Graph/EnhancedRenderPass.h"

// Diagnostic transform/pose slice. Materials remain current immutable owners.
// Resolve geometry against the selected live closure; never deserialize pointers,
// publication IDs, generation numbers or animator keys from another process.
struct EnhancedDrawReplayInput
{
    struct Draw
    {
        std::array<uint8_t, 32> assetIds{};
        assets::ModelMeshDomain domain{ assets::ModelMeshDomain::LegacyAggregate };
        uint64_t geometryDigest{};
        uint32_t route{}; // opaque=0, forward=1, LX=2
        math::matrix4x4 world{};
        std::vector<math::matrix4x4> bones;
    };
    std::vector<Draw> draws;
    static constexpr size_t kMaxBytes = 64u * 1024u * 1024u;
    static constexpr uint32_t kMaxDraws = 16384, kMaxBones = 1024;
    static constexpr std::array<uint8_t, 8> kMagic{'C','E','D','R','W','0','0','1'};

    static std::array<uint8_t, 32> Identity(const RHIModelMeshView& mesh)
    {
        std::array<uint8_t, 32> result{};
        if (mesh.handle.domain == assets::ModelMeshDomain::Granular)
        {
            const auto& asset = mesh.handle.asset.key;
            std::copy(asset.assetId.value.data.begin(), asset.assetId.value.data.end(), result.begin());
            std::copy(asset.subassetId.value.data.begin(), asset.subassetId.value.data.end(), result.begin() + 16);
        }
        else
        {
            std::copy(mesh.handle.modelId.data.begin(), mesh.handle.modelId.data.end(), result.begin());
            std::copy(mesh.handle.meshId.data.begin(), mesh.handle.meshId.data.end(), result.begin() + 16);
        }
        return result;
    }
    static uint64_t GeometryDigest(const RHIModelMeshView& mesh)
    {
        uint64_t hash = 14695981039346656037ull;
        const auto mix = [&](std::span<const uint8_t> bytes) {
            for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
        };
        const auto number = [&](uint64_t value) {
            std::array<uint8_t, 8> bytes{};
            for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>(value >> (8*i));
            mix(bytes);
        };
        if (mesh.handle.domain == assets::ModelMeshDomain::Granular)
        {
            const auto& blob = mesh.handle.blob;
            number(static_cast<uint64_t>(mesh.handle.domain));
            number(static_cast<uint64_t>(blob.kind));
            number(blob.byteSize); number(blob.representation); number(blob.schemaVersion);
            mix(blob.contentSha256);
            number(blob.targetPlatform.size());
            mix({ reinterpret_cast<const uint8_t*>(blob.targetPlatform.data()), blob.targetPlatform.size() });
            number(blob.targetAbi.size());
            mix({ reinterpret_cast<const uint8_t*>(blob.targetAbi.data()), blob.targetAbi.size() });
        }
        number(mesh.vertexBytes); number(mesh.vertexStride);
        number(mesh.vertexAttributeMask); number(mesh.vertexLayoutHash); number(mesh.indexCount);
        mix({static_cast<const uint8_t*>(mesh.vertexData), static_cast<size_t>(mesh.vertexBytes)});
        mix({reinterpret_cast<const uint8_t*>(mesh.indexData), size_t(mesh.indexCount) * sizeof(uint32_t)});
        return hash;
    }
    static bool Finite(const math::matrix4x4& value)
    {
        std::array<float, 16> floats{};
        std::memcpy(floats.data(), &value, sizeof(value));
        return std::ranges::all_of(floats, [](float v) { return std::isfinite(v); });
    }
    std::vector<uint8_t> Encode() const
    {
        std::vector<uint8_t> bytes(kMagic.begin(), kMagic.end());
        const auto number = [&](uint64_t value, unsigned count) {
            for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8*i)));
        };
        const auto matrix = [&](const math::matrix4x4& value) {
            std::array<float, 16> floats{};
            std::memcpy(floats.data(), &value, sizeof(value));
            for (float v : floats) number(std::bit_cast<uint32_t>(v), 4);
        };
        const bool granular = std::ranges::any_of(draws, [](const Draw& draw)
        {
            return draw.domain == assets::ModelMeshDomain::Granular;
        });
        number(granular ? 2u : 1u, 4); number(draws.size(), 4);
        for (const auto& draw : draws)
        {
            number(draw.route, 4);
            if (granular)
            {
                number(static_cast<uint64_t>(draw.domain), 4);
            }
            bytes.insert(bytes.end(), draw.assetIds.begin(), draw.assetIds.end());
            number(draw.geometryDigest, 8); number(draw.bones.size(), 4);
            matrix(draw.world);
            for (const auto& bone : draw.bones) matrix(bone);
        }
        number(EnhancedCameraReplayInput::Checksum(bytes), 8);
        return bytes;
    }
    static bool Decode(std::span<const uint8_t> bytes, EnhancedDrawReplayInput& output, std::string& error)
    {
        const auto reject = [&](const char* why) { error = why; return false; };
        if (bytes.size() < 24 || bytes.size() > kMaxBytes || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
            return reject("draw replay size or magic mismatch");
        uint64_t hash{};
        for (unsigned i=0; i<8; ++i) hash |= uint64_t(bytes[bytes.size()-8+i]) << (8*i);
        const auto payload = bytes.first(bytes.size()-8);
        if (hash != EnhancedCameraReplayInput::Checksum(payload)) return reject("draw replay checksum mismatch");
        size_t offset=8;
        const auto number = [&](unsigned count) {
            uint64_t value{};
            for (unsigned i=0; i<count; ++i) value |= uint64_t(payload[offset++]) << (8*i);
            return value;
        };
        const auto matrix = [&]() {
            std::array<float,16> floats{};
            for (auto& v : floats) v=std::bit_cast<float>(static_cast<uint32_t>(number(4)));
            math::matrix4x4 value;
            std::memcpy(&value, floats.data(), sizeof(value));
            return value;
        };
        const auto version = number(4);
        if (version != 1u && version != 2u)
        {
            return reject("unsupported draw replay version");
        }
        const std::size_t recordBytes = version == 1u ? 112u : 116u;
        const auto count=number(4);
        if (count>kMaxDraws || count>(payload.size()-offset)/recordBytes) return reject("draw replay count exceeds payload or budget");
        EnhancedDrawReplayInput candidate;
        candidate.draws.reserve(static_cast<size_t>(count));
        for (uint64_t i=0; i<count; ++i)
        {
            if (payload.size()-offset<recordBytes) return reject("draw replay record truncated");
            Draw draw;
            draw.route=static_cast<uint32_t>(number(4));
            if (version == 2u)
            {
                const auto domain = number(4);
                if (domain > static_cast<uint64_t>(assets::ModelMeshDomain::Granular))
                {
                    return reject("draw replay identity domain invalid");
                }
                draw.domain = static_cast<assets::ModelMeshDomain>(domain);
            }
            std::copy_n(payload.begin()+offset, 32, draw.assetIds.begin()); offset+=32;
            draw.geometryDigest=number(8);
            const auto boneCount=number(4);
            if (draw.route>2 || boneCount>kMaxBones || boneCount>(payload.size()-offset-64)/64)
                return reject("draw replay route or pose count invalid");
            if (std::ranges::all_of(std::span(draw.assetIds).first(16), [](auto v){return v==0;})
                || (draw.domain == assets::ModelMeshDomain::LegacyAggregate
                    && std::ranges::all_of(std::span(draw.assetIds).last(16), [](auto v){return v==0;})))
                return reject("draw replay asset identity invalid");
            draw.world=matrix();
            if (!Finite(draw.world)) return reject("draw replay world nonfinite");
            draw.bones.reserve(static_cast<size_t>(boneCount));
            for (uint64_t j=0; j<boneCount; ++j)
            {
                auto bone=matrix();
                if (!Finite(bone)) return reject("draw replay pose nonfinite");
                draw.bones.push_back(bone);
            }
            candidate.draws.push_back(std::move(draw));
        }
        if (offset!=payload.size()) return reject("draw replay trailing payload");
        output=std::move(candidate); error.clear(); return true;
    }
    static bool Load(const std::string& path, EnhancedDrawReplayInput& output, std::string& error)
    {
        std::error_code ec;
        const auto file=std::filesystem::path(path);
        const auto size=std::filesystem::file_size(file, ec);
        if (!file.is_absolute() || ec || size<24 || size>kMaxBytes)
        { error="draw replay requires a bounded absolute file"; return false; }
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        std::ifstream stream(file, std::ios::binary);
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) || stream.peek()!=EOF)
        { error="draw replay file read failed"; return false; }
        return Decode(bytes, output, error);
    }
    static bool Seal(std::span<const EnhancedDrawItem> opaque, std::span<const EnhancedDrawItem> forward,
        std::span<const EnhancedDrawItem> graph, EnhancedDrawReplayInput& output, std::string& error)
    {
        EnhancedDrawReplayInput candidate;
        size_t byteSize=24;
        for (uint32_t route=0; route<3; ++route)
        {
            const auto items=route==0 ? opaque : route==1 ? forward : graph;
            for (const auto& item : items)
            {
                byteSize+=116+size_t(item.boneCount)*64;
                if (!item.modelMeshView.IsComplete() || item.boneCount>kMaxBones
                    || (item.boneCount && !item.bonePalette) || byteSize>kMaxBytes || candidate.draws.size()>=kMaxDraws)
                { error="draw replay requires complete owned geometry and bounded pose"; return false; }
                Draw draw;
                draw.route=route; draw.assetIds=Identity(item.modelMeshView);
                draw.domain = item.modelMeshView.handle.domain;
                draw.geometryDigest=GeometryDigest(item.modelMeshView); draw.world=item.worldMatrix;
                if (item.boneCount) draw.bones.assign(item.bonePalette, item.bonePalette+item.boneCount);
                if (!Finite(draw.world) || !std::ranges::all_of(draw.bones, Finite))
                { error="draw replay source contains nonfinite transforms"; return false; }
                candidate.draws.push_back(std::move(draw));
            }
        }
        output=std::move(candidate); error.clear(); return true;
    }
    // The archive itself owns palette storage until frame preparation finishes.
    // Validate the ENTIRE closure before changing any item (transactional apply).
    bool Apply(std::span<EnhancedDrawItem> opaque, std::span<EnhancedDrawItem> forward,
        std::span<EnhancedDrawItem> graph, std::string& error) const
    {
        EnhancedDrawReplayInput current;
        if (!Seal(opaque,forward,graph,current,error)) return false;
        if (current.draws.size()!=draws.size())
        { error="draw replay selected closure count mismatch"; return false; }
        for (size_t i=0; i<draws.size(); ++i)
        {
            const auto& saved=draws[i]; const auto& live=current.draws[i];
            if (saved.route!=live.route || saved.domain != live.domain
                || saved.assetIds!=live.assetIds || saved.geometryDigest!=live.geometryDigest
                || saved.bones.size()!=live.bones.size())
            { error="draw replay geometry/route/pose closure mismatch"; return false; }
        }
        size_t index=0;
        for (uint32_t route=0; route<3; ++route)
        {
            auto items=route==0 ? opaque : route==1 ? forward : graph;
            for (size_t j=0; j<items.size(); ++j,++index)
            {
                const auto& saved=draws[index]; auto& item=items[j];
                item.worldMatrix=saved.world;
                item.bonePalette=saved.bones.empty() ? nullptr : saved.bones.data();
                item.boneCount=static_cast<uint32_t>(saved.bones.size());
                item.animatorKey=saved.bones.empty() ? 0 : index+1;
            }
        }
        error.clear(); return true;
    }
};
