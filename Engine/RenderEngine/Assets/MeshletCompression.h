#include "../../Containers/ContainerAPI.h"
#pragma once
#include "../../Utility_Framework/Foundation/EngineFunc.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "QuantizedGeometry.h"
#include "OctahedralNormal.h"
#include "../Experiment/MeshletData.h"
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets::cluster
{
    // Policy v2: position uses one mesh-wide grid, so duplicated seam vertices
    // decode identically in every meshlet. Normals use oct-SNORM12x2 when the
    // mesh-wide error policy admits it; skin weights retain their original bits.
    struct Policy
    {
        ce::fixed_array<unsigned, 8> bits{16, 10, 16, 16, 10, 8, 32, 32};
        ce::fixed_array<float, 8> absoluteError{0.0001f, 0.002f, 0.0001f, 0.0001f, 0.002f, 0.002f, 0, 0.00001f};
    };
    struct Grid
    {
        ce::dynamic_array<float> origin, step;
        float positionError{};
        float normalError{0.002f};
        bool octahedralNormals{true};
    };
    struct Archive
    {
        uint32_t mask{}, sourceVertices{};
        bool pagedTopology{};
        Grid grid;
        ce::dynamic_array<ce::dynamic_array<std::byte>> packets;
        ce::dynamic_array<uint64_t> fileOffsets;
        ce::dynamic_array<uint32_t> packetSizes;
        ce::owned_func<ce::dynamic_array<std::byte>(uint32_t)> pageReader;
        uint64_t PacketBytes(uint32_t page) const
        {
            quantized::Require(page < packets.size(), "Cluster page ordinal");
            return packetSizes.empty() ? packets[page].size() : packetSizes[page];
        }
        ce::dynamic_array<std::byte> Read(uint32_t page) const
        {
            auto result = pageReader ? pageReader(page) : packets.at(page);
            quantized::Require(result.size() == PacketBytes(page), "Cluster page short read");
            return result;
        }
        uint64_t Bytes() const
        {
            uint64_t size{};
            for (uint32_t page = 0; page < packets.size(); ++page)
            {
                size += PacketBytes(page);
            }
            return size;
        }
    };
    inline constexpr uint32_t Magic = 0x31434d51u; // QMC1
    inline Grid MakeGrid(uint32_t mask, uint32_t count, std::span<const std::byte> raw, const Policy& policy = {})
    {
        using namespace quantized;
        const auto stride = StrideOf(mask);
        Require(IsSupportedModelVertexLayout(mask) && count && raw.size() == uint64_t(count) * stride,
                "Cluster quantization layout/count");
        Grid grid;
        grid.normalError = policy.absoluteError[static_cast<unsigned>(VertexAttribute::Normal)];
        grid.origin.resize(stride / 4);
        grid.step.resize(stride / 4);
        for (const auto& attribute : kVertexAttributeTable)
        {
            if (!Has(mask, attribute.attribute) || attribute.attribute == VertexAttribute::BoneIndices ||
                attribute.attribute == VertexAttribute::BoneWeights)
            {
                continue;
            }
            if (attribute.attribute == VertexAttribute::Normal || attribute.attribute == VertexAttribute::Tangent)
            {
                bool unit = true;
                for (uint32_t vertex = 0; vertex < count; ++vertex)
                {
                    double length{};
                    for (unsigned component = 0; component < 3; ++component)
                    {
                        const auto value = std::bit_cast<float>(
                            Word(raw, uint64_t(vertex) * stride + OffsetOf(mask, attribute.attribute) + component * 4));
                        length += double(value) * value;
                    }
                    unit = unit && length >= .99 && length <= 1.01;
                }
                if (attribute.attribute == VertexAttribute::Normal)
                {
                    // One codec decision for the whole mesh keeps seam duplicates
                    // identical even when another meshlet contains a non-unit normal.
                    grid.octahedralNormals = unit;
                    for (uint32_t vertex = 0; vertex < count && grid.octahedralNormals; ++vertex)
                    {
                        ce::fixed_array<float, 3> normal;
                        std::memcpy(normal.data(),
                                    raw.data() + uint64_t(vertex) * stride + OffsetOf(mask, VertexAttribute::Normal),
                                    12);
                        const auto decoded = DecodeOctahedralNormal(EncodeOctahedralNormal(normal));
                        for (unsigned component = 0; component < 3; ++component)
                        {
                            grid.octahedralNormals =
                                grid.octahedralNormals &&
                                std::abs(decoded[component] - normal[component]) <= grid.normalError;
                        }
                    }
                }
                if (!unit)
                {
                    continue;
                }
            }
            const auto a = static_cast<unsigned>(attribute.attribute);
            const auto bits = policy.bits[a];
            Require((bits == 8 || bits == 10 || bits == 16 || bits == 32) && std::isfinite(policy.absoluteError[a]) &&
                        policy.absoluteError[a] >= 0,
                    "Cluster attribute policy");
            for (unsigned scalar = 0; scalar < SizeOf(attribute.format) / 4; ++scalar)
            {
                const auto channel = OffsetOf(mask, attribute.attribute) / 4 + scalar;
                float minimum = std::numeric_limits<float>::max(), maximum = -minimum;
                for (uint32_t vertex = 0; vertex < count; ++vertex)
                {
                    const auto value = std::bit_cast<float>(Word(raw, uint64_t(vertex) * stride + channel * 4));
                    Require(std::isfinite(value), "Nonfinite cluster attribute");
                    minimum = (std::min)(minimum, value);
                    maximum = (std::max)(maximum, value);
                }
                if (bits == 32 || minimum == maximum)
                {
                    continue;
                }
                // Signed unit directions and unsigned weights/colors have fixed
                // semantic ranges. HDR/out-of-range values retain their raw bits.
                if (attribute.attribute == VertexAttribute::Normal || attribute.attribute == VertexAttribute::Tangent)
                {
                    if (minimum < -1 || maximum > 1)
                    {
                        continue;
                    }
                    minimum = -1;
                    maximum = 1;
                }
                else if (attribute.attribute == VertexAttribute::Color ||
                         attribute.attribute == VertexAttribute::BoneWeights)
                {
                    if (minimum < 0 || maximum > 1)
                    {
                        continue;
                    }
                    minimum = 0;
                    maximum = 1;
                }
                const auto levels = (1u << bits) - 1;
                const float step = static_cast<float>((double(maximum) - minimum) / levels);
                if (!std::isfinite(step) || step < std::numeric_limits<float>::min())
                {
                    continue;
                }
                float error{};
                bool accepted = true;
                for (uint32_t vertex = 0; vertex < count; ++vertex)
                {
                    const auto value = std::bit_cast<float>(Word(raw, uint64_t(vertex) * stride + channel * 4));
                    const auto q = std::clamp(std::round((double(value) - minimum) / step), 0.0, double(levels));
                    const float product = float(q) * step;
                    const float decoded = minimum + product;
                    error = (std::max)(error, std::abs(decoded - value));
                    accepted = accepted && std::isfinite(decoded) && decoded >= minimum && decoded <= maximum &&
                               error <= policy.absoluteError[a];
                }
                if (accepted)
                {
                    grid.origin[channel] = minimum;
                    grid.step[channel] = step;
                    if (attribute.attribute == VertexAttribute::Position)
                    {
                        grid.positionError = (std::max)(grid.positionError, error);
                    }
                }
            }
        }
        return grid;
    }
    inline ce::dynamic_array<std::byte> Encode(uint32_t mask, uint32_t count, std::span<const std::byte> raw,
                                         const Grid& grid)
    {
        using namespace quantized;
        const auto channels = StrideOf(mask) / 4;
        Require(grid.step.size() == channels && grid.origin.size() == channels, "Cluster grid layout");
        auto codes = ce::dynamic_array<std::byte>(raw.begin(), raw.end());
        for (uint32_t vertex = 0; vertex < count; ++vertex)
        {
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                if (grid.step[channel] == 0)
                {
                    continue;
                }
                const auto offset = uint64_t(vertex) * channels * 4 + channel * 4;
                const auto value = std::bit_cast<float>(Word(raw, offset));
                const auto q = std::round((double(value) - grid.origin[channel]) / grid.step[channel]);
                Require(q >= 0 && q <= 65535, "Cluster code range");
                Put(codes, offset, std::bit_cast<uint32_t>(float(q)));
            }
        }
        bool octahedral = grid.octahedralNormals && Has(mask, VertexAttribute::Normal);
        const auto normalOffset = OffsetOf(mask, VertexAttribute::Normal);
        ce::dynamic_array<uint32_t> normals;
        if (octahedral)
        {
            normals.reserve(count);
            for (uint32_t vertex = 0; vertex < count; ++vertex)
            {
                ce::fixed_array<float, 3> normal;
                std::memcpy(normal.data(), raw.data() + uint64_t(vertex) * channels * 4 + normalOffset, 12);
                const double length =
                    double(normal[0]) * normal[0] + double(normal[1]) * normal[1] + double(normal[2]) * normal[2];
                if (!std::isfinite(length) || length < .99 || length > 1.01)
                {
                    octahedral = false;
                    break;
                }
                const auto code = EncodeOctahedralNormal(normal);
                const auto decoded = DecodeOctahedralNormal(code);
                for (unsigned component = 0; component < 3; ++component)
                {
                    octahedral = octahedral && std::abs(normal[component] - decoded[component]) <= grid.normalError;
                }
                normals.push_back(code);
            }
        }
        if (octahedral)
        {
            for (uint32_t vertex = 0; vertex < count; ++vertex)
            {
                const auto offset = uint64_t(vertex) * channels * 4 + normalOffset;
                Put(codes, offset, std::bit_cast<uint32_t>(float(normals[vertex])));
                Put(codes, offset + 4, 0);
                Put(codes, offset + 8, 0);
            }
        }
        ce::dynamic_array<std::byte> encoded;
        std::string error;
        Require(quantized::Encode(mask, count, codes, encoded, error), "Cluster code encoding");
        const uint32_t payload = 32 + channels * 8;
        ce::dynamic_array<std::byte> packet(payload);
        Put(packet, 0, Magic);
        Put(packet, 4, 2);
        Put(packet, 28, uint32_t(octahedral));
        Put(packet, 8, mask);
        Put(packet, 12, count);
        Put(packet, 16, payload);
        Put(packet, 20, payload + static_cast<uint32_t>(encoded.size()));
        Put(packet, 24, std::bit_cast<uint32_t>(grid.positionError));
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            Put(packet, 32 + channel * 8, std::bit_cast<uint32_t>(grid.origin[channel]));
            Put(packet, 36 + channel * 8, std::bit_cast<uint32_t>(grid.step[channel]));
            if (octahedral && channel >= normalOffset / 4 && channel < normalOffset / 4 + 3)
            {
                Put(packet, 32 + channel * 8, 0);
                Put(packet, 36 + channel * 8, 0);
            }
        }
        packet.insert(packet.end(), encoded.begin(), encoded.end());
        return packet;
    }
    inline bool Decode(std::span<const std::byte> packet, ce::dynamic_array<std::byte>& out, std::string& error)
    {
        using namespace quantized;
        out.clear();
        try
        {
            Require(Word(packet, 0) == Magic && (Word(packet, 4) == 1 || Word(packet, 4) == 2) &&
                        Word(packet, 20) == packet.size() &&
                        (Word(packet, 28) == 0 || (Word(packet, 4) == 2 && Word(packet, 28) == 1)),
                    "Cluster packet header");
            const auto mask = Word(packet, 8), count = Word(packet, 12), start = Word(packet, 16);
            const auto channels = StrideOf(mask) / 4;
            Require(IsSupportedModelVertexLayout(mask) && count && count <= experiment::kMeshletMaxVertices &&
                        packet.size() <= MaximumBytes && start == 32 + channels * 8 && start <= packet.size(),
                    "Cluster packet layout");
            ce::dynamic_array<std::byte> codes;
            Require(quantized::Decode(packet.subspan(start), mask, count, codes, error), "Cluster code payload");
            for (unsigned channel = 0; channel < channels; ++channel)
            {
                const auto origin = std::bit_cast<float>(Word(packet, 32 + channel * 8));
                const auto step = std::bit_cast<float>(Word(packet, 36 + channel * 8));
                Require(std::isfinite(origin) && std::isfinite(step) && step >= 0, "Cluster quantization grid");
                const auto channelOffset = channel * 4;
                const bool normalCode = Word(packet, 28) == 1 && channelOffset >= 12 && channelOffset < 24;
                const auto weightOffset =
                    Has(mask, VertexAttribute::BoneWeights) ? OffsetOf(mask, VertexAttribute::BoneWeights) : UINT32_MAX;
                const bool exactWeight = Word(packet, 4) == 2 && channelOffset >= weightOffset &&
                                         uint64_t(channelOffset) < uint64_t(weightOffset) + 16;
                Require(!(normalCode || exactWeight) || (origin == 0 && step == 0), "Cluster direction/skin code grid");
                for (uint32_t vertex = 0; vertex < count && step != 0; ++vertex)
                {
                    const auto offset = uint64_t(vertex) * channels * 4 + channel * 4;
                    const auto q = std::bit_cast<float>(Word(codes, offset));
                    Require(q >= 0 && q <= 65535 && q == std::floor(q), "Cluster quantized code");
                    const float product = q * step;
                    const float decoded = origin + product;
                    Require(std::isfinite(decoded), "Cluster decoded finite value");
                    Put(codes, offset, std::bit_cast<uint32_t>(decoded));
                }
            }
            if (Word(packet, 28) == 1)
            {
                Require(Has(mask, VertexAttribute::Normal), "Octahedral normal layout");
                const auto normal = OffsetOf(mask, VertexAttribute::Normal);
                for (uint32_t vertex = 0; vertex < count; ++vertex)
                {
                    const auto offset = uint64_t(vertex) * channels * 4 + normal;
                    const auto code = std::bit_cast<float>(Word(codes, offset));
                    Require(code >= 0 && code <= 16777215 && code == std::floor(code) && Word(codes, offset + 4) == 0 &&
                                Word(codes, offset + 8) == 0,
                            "Octahedral normal code");
                    const auto decoded = DecodeOctahedralNormal(uint32_t(code));
                    for (unsigned component = 0; component < 3; ++component)
                    {
                        Put(codes, offset + component * 4, std::bit_cast<uint32_t>(decoded[component]));
                    }
                }
            }
            out = std::move(codes);
            error.clear();
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
    inline own::shared_owner<const Archive> Build(uint32_t mask, uint32_t count, std::span<const std::byte> raw,
                                                  const experiment::MeshletPayload& meshlets, const Policy& policy = {})
    {
        auto result = own::make_shared<Archive>();
        result->mask = mask;
        result->sourceVertices = count;
        result->grid = MakeGrid(mask, count, raw, policy);
        const auto stride = StrideOf(mask);
        for (const auto& descriptor : meshlets.descriptors)
        {
            quantized::Require(descriptor.vertexCount && descriptor.vertexCount <= experiment::kMeshletMaxVertices &&
                                   uint64_t(descriptor.vertexOffset) + descriptor.vertexCount <=
                                       meshlets.vertexRemap.size(),
                               "Cluster vertex range");
            ce::dynamic_array<std::byte> vertices;
            for (uint32_t i = 0; i < descriptor.vertexCount; ++i)
            {
                const auto vertex = meshlets.vertexRemap[descriptor.vertexOffset + i];
                quantized::Require(vertex < count, "Cluster source vertex");
                vertices.insert(vertices.end(), raw.begin() + uint64_t(vertex) * stride,
                                raw.begin() + uint64_t(vertex + 1) * stride);
            }
            result->packets.push_back(Encode(mask, descriptor.vertexCount, vertices, result->grid));
        }
        return result;
    }
} // namespace assets::cluster
