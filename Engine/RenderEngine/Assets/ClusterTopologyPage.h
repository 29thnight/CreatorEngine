#pragma once
#include "MeshletCompression.h"

namespace assets::cluster
{
inline constexpr uint32_t TopologyPageMagic = 0x32434d51u; // QMC2
struct TopologyPageView
{
    uint32_t vertices{}, triangles{}, sourceOffset{}, triangleOffset{}, primitiveOffset{};
    std::span<const std::byte> attributes;
};

inline TopologyPageView InspectTopologyPage(std::span<const std::byte> packet,
                                           uint32_t sourceVertexLimit, uint32_t primitiveLimit)
{
    using namespace quantized;
    Require(Word(packet, 0) == TopologyPageMagic && Word(packet, 4) == 1 && Word(packet, 8) == 32 &&
                Word(packet, 24) == packet.size(), "QMC2 header/extent");
    TopologyPageView result;
    const auto counts = Word(packet, 28);
    result.vertices = counts & 0xffffu;
    result.triangles = counts >> 16;
    Require(result.vertices && result.vertices <= experiment::kMeshletMaxVertices &&
                result.triangles && result.triangles <= experiment::kMeshletMaxTriangles &&
                sourceVertexLimit && primitiveLimit, "QMC2 logical bounds");
    result.sourceOffset = Word(packet, 12);
    result.triangleOffset = Word(packet, 16);
    result.primitiveOffset = Word(packet, 20);
    Require(Word(packet, 32) == Magic && Word(packet, 36) == 1 && Word(packet, 44) == result.vertices,
            "QMC2 attribute layout/count");
    const uint64_t attributeBytes = Word(packet, 52);
    Require(attributeBytes >= 32 && attributeBytes <= packet.size() - 32 &&
                result.sourceOffset == ((32 + attributeBytes + 15) & ~uint64_t(15)) &&
                result.triangleOffset == uint64_t(result.sourceOffset) + result.vertices * 4 &&
                result.primitiveOffset == ((uint64_t(result.triangleOffset) + result.triangles * 3 + 3) & ~uint64_t(3)) &&
                packet.size() == ((uint64_t(result.primitiveOffset) + result.triangles * 4 + 15) & ~uint64_t(15)),
            "QMC2 section ranges");
    result.attributes = packet.subspan(32, attributeBytes);
    for (uint32_t vertex = 0; vertex < result.vertices; ++vertex)
        Require(Word(packet, result.sourceOffset + vertex * 4) < sourceVertexLimit, "QMC2 source vertex identity");
    for (uint32_t triangle = 0; triangle < result.triangles; ++triangle)
    {
        Require(Word(packet, result.primitiveOffset + triangle * 4) < primitiveLimit, "QMC2 primitive identity");
        for (uint32_t corner = 0; corner < 3; ++corner)
            Require(std::to_integer<uint32_t>(packet[result.triangleOffset + triangle * 3 + corner]) < result.vertices,
                    "QMC2 triangle corner");
    }
    const auto zero = [&](uint64_t begin, uint64_t end) {
        for (auto index = begin; index < end; ++index)
            Require(packet[index] == std::byte{}, "QMC2 noncanonical padding");
    };
    zero(32 + attributeBytes, result.sourceOffset);
    zero(uint64_t(result.triangleOffset) + result.triangles * 3, result.primitiveOffset);
    zero(uint64_t(result.primitiveOffset) + result.triangles * 4, packet.size());
    return result;
}

inline std::vector<std::byte> EncodeTopologyPage(std::span<const std::byte> attributes,
                                                const experiment::MeshletPayload& topology,
                                                const experiment::MeshletDescriptor& descriptor,
                                                uint32_t sourceVertexLimit, uint32_t primitiveLimit)
{
    using namespace quantized;
    Require(attributes.size() <= MaximumBytes - 32 &&
                uint64_t(descriptor.vertexOffset) + descriptor.vertexCount <= topology.vertexRemap.size() &&
                uint64_t(descriptor.triangleOffset) + descriptor.triangleCount * 3 <= topology.triangleIndices.size() &&
                uint64_t(descriptor.primitiveOffset) + descriptor.triangleCount <= topology.primitiveRemap.size(),
            "QMC2 encode topology range");
    const auto source = (32ull + attributes.size() + 15) & ~uint64_t(15);
    const auto triangles = source + descriptor.vertexCount * 4ull;
    const auto primitives = (triangles + descriptor.triangleCount * 3ull + 3) & ~uint64_t(3);
    const auto size = (primitives + descriptor.triangleCount * 4ull + 15) & ~uint64_t(15);
    Require(size <= MaximumBytes && size <= UINT32_MAX, "QMC2 page size");
    std::vector<std::byte> result(size);
    Put(result, 0, TopologyPageMagic); Put(result, 4, 1); Put(result, 8, 32);
    Put(result, 12, static_cast<uint32_t>(source)); Put(result, 16, static_cast<uint32_t>(triangles));
    Put(result, 20, static_cast<uint32_t>(primitives)); Put(result, 24, static_cast<uint32_t>(size));
    Put(result, 28, descriptor.vertexCount | descriptor.triangleCount << 16);
    std::memcpy(result.data() + 32, attributes.data(), attributes.size());
    for (uint32_t vertex = 0; vertex < descriptor.vertexCount; ++vertex)
        Put(result, source + vertex * 4, topology.vertexRemap[descriptor.vertexOffset + vertex]);
    for (uint32_t corner = 0; corner < descriptor.triangleCount * 3; ++corner)
        result[triangles + corner] = static_cast<std::byte>(topology.triangleIndices[descriptor.triangleOffset + corner]);
    for (uint32_t triangle = 0; triangle < descriptor.triangleCount; ++triangle)
        Put(result, primitives + triangle * 4, topology.primitiveRemap[descriptor.primitiveOffset + triangle]);
    InspectTopologyPage(result, sourceVertexLimit, primitiveLimit);
    return result;
}

// Admission is bounded to one meshlet. No full model vertex/index arrays are
// reconstructed, including for synchronous pinned-root reads.
inline void ValidateTopologyPageAttributes(std::span<const std::byte> packet, uint32_t mask,
                                           uint32_t sourceVertexLimit, uint32_t primitiveLimit,
                                           uint32_t boneCount, const experiment::MeshletDescriptor& descriptor,
                                           float positionError)
{
    using namespace quantized;
    const auto view = InspectTopologyPage(packet, sourceVertexLimit, primitiveLimit);
    Require(view.vertices == descriptor.vertexCount && view.triangles == descriptor.triangleCount &&
                Word(view.attributes, 8) == mask && std::isfinite(positionError) && positionError >= 0,
            "QMC2 metadata/page disagreement");
    std::vector<std::byte> raw;
    std::string error;
    Require(Decode(view.attributes, raw, error), "QMC2 malformed attribute payload");
    const auto stride = StrideOf(mask);
    for (uint32_t vertex = 0; vertex < view.vertices; ++vertex)
    {
        const auto scalar = [&](uint32_t offset) { return std::bit_cast<float>(Word(raw, uint64_t(vertex) * stride + offset)); };
        for (const auto& attribute : kVertexAttributeTable)
        {
            if (!Has(mask, attribute.attribute) || attribute.attribute == VertexAttribute::BoneIndices) continue;
            for (uint32_t offset = 0; offset < SizeOf(attribute.format); offset += 4)
            {
                const auto value = scalar(OffsetOf(mask, attribute.attribute) + offset);
                Require(std::isfinite(value) && std::abs(value) <= 1e6f, "QMC2 nonfinite/out-of-range attribute");
            }
        }
        const auto normal = OffsetOf(mask, VertexAttribute::Normal);
        Require(std::hypot(scalar(normal), scalar(normal + 4), scalar(normal + 8)) > 1e-10,
                "QMC2 zero normal");
        const auto sign = scalar(OffsetOf(mask, VertexAttribute::Tangent) + 12);
        Require(sign == -1.f || sign == 1.f, "QMC2 tangent handedness");
        const double x = double(scalar(0)) - descriptor.sphereCenter[0];
        const double y = double(scalar(4)) - descriptor.sphereCenter[1];
        const double z = double(scalar(8)) - descriptor.sphereCenter[2];
        const double radius = descriptor.sphereRadius + double(positionError) * 1.732051 + 1e-5;
        Require(std::isfinite(radius) && radius >= 0 && std::sqrt(x*x + y*y + z*z) <= radius,
                "QMC2 positions exceed authenticated meshlet bounds");
        if (Has(mask, VertexAttribute::BoneIndices))
        {
            Require(boneCount != 0, "QMC2 missing skin dependency");
            const auto indices = Word(raw, uint64_t(vertex) * stride + OffsetOf(mask, VertexAttribute::BoneIndices));
            const auto weights = OffsetOf(mask, VertexAttribute::BoneWeights);
            float sum{};
            for (uint32_t influence = 0; influence < 4; ++influence)
            {
                const auto weight = scalar(weights + influence * 4);
                Require(weight >= 0 && weight <= 1 && (weight == 0 || ((indices >> (influence * 8)) & 255) < boneCount),
                        "QMC2 skin weight/index range");
                sum += weight;
            }
            // Four independently quantized weights can each differ by 1e-5.
            Require(sum == 0 || std::abs(sum - 1.f) <= 1e-4f, "QMC2 skin weight sum");
        }
        else Require(boneCount == 0, "QMC2 unexpected skin dependency");
    }
}
}
