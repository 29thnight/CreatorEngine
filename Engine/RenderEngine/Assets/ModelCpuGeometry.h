#include "../../Containers/ContainerAPI.h"
#pragma once
#include "ModelVertexLayout.h"
#include <mathematics/vector3.hpp>
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets::cpu_geometry
{
// Exact rest-pose positions and original LOD0 indices. No normals, UVs,
// meshlets, coarse LODs, material data or decoded GPU pages are retained.
struct Geometry
{
    ce::dynamic_array<math::vector3> positions;
    ce::dynamic_array<uint32_t> indices;
};
inline constexpr uint32_t Magic = 0x50434543u; // CECP
inline constexpr uint32_t MaxVertices = 1u << 20, MaxIndices = 12u << 20;
inline constexpr uint64_t MaxBytes = 24ull + uint64_t(MaxVertices) * 12 + uint64_t(MaxIndices) * 4;
inline std::string Path(const ce::fixed_array<uint8_t,32>& digest)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string path = "Derived/CpuGeometry/";
    for (auto byte : digest) { path += hex[byte >> 4]; path += hex[byte & 15]; }
    return path + ".cecpu";
}
inline ce::dynamic_array<std::byte> Encode(uint32_t mask, uint32_t count, std::span<const std::byte> vertices,
                                     std::span<const uint32_t> indices)
{
    const auto stride = StrideOf(mask);
    if (!IsSupportedModelVertexLayout(mask) || count < 3 || count > MaxVertices ||
        vertices.size() != uint64_t(count) * stride || indices.empty() || indices.size() > MaxIndices || indices.size() % 3)
        throw std::runtime_error("Invalid exact CPU geometry extent");
    ce::dynamic_array<std::byte> out;
    out.reserve(24 + uint64_t(count) * 12 + indices.size() * 4);
    const auto word = [&](uint32_t value) { for (uint32_t shift = 0; shift < 32; shift += 8) out.push_back(std::byte(value >> shift)); };
    word(Magic); word(1); word(count); word(static_cast<uint32_t>(indices.size())); word(12); word(0);
    const auto offset = OffsetOf(mask, VertexAttribute::Position);
    for (uint32_t vertex = 0; vertex < count; ++vertex)
        for (uint32_t component = 0; component < 3; ++component)
        {
            uint32_t bits; std::memcpy(&bits, vertices.data() + uint64_t(vertex) * stride + offset + component * 4, 4);
            const float value = std::bit_cast<float>(bits);
            if (!std::isfinite(value) || std::abs(value) > 1e6f) throw std::runtime_error("Invalid exact CPU position");
            word(bits);
        }
    for (uint32_t index : indices) { if (index >= count) throw std::runtime_error("Invalid exact CPU index"); word(index); }
    return out;
}
inline bool Decode(std::span<const std::byte> bytes, Geometry& output, std::string& error)
{
    try
    {
        if (bytes.size() < 24 || bytes.size() > MaxBytes) throw std::runtime_error("CPU geometry extent");
        size_t cursor{};
        const auto word = [&]() { if (bytes.size() - cursor < 4) throw std::runtime_error("CPU geometry truncation");
            uint32_t value{}; for (uint32_t shift = 0; shift < 32; shift += 8) value |= uint32_t(bytes[cursor++]) << shift; return value; };
        if (word() != Magic || word() != 1) throw std::runtime_error("CPU geometry schema");
        const auto vertices = word(), indices = word();
        if (vertices < 3 || vertices > MaxVertices || !indices || indices > MaxIndices || indices % 3 ||
            word() != 12 || word() != 0 || bytes.size() != 24ull + uint64_t(vertices) * 12 + uint64_t(indices) * 4)
            throw std::runtime_error("CPU geometry counts/profile");
        Geometry result; result.positions.resize(vertices); result.indices.resize(indices);
        for (auto& p : result.positions)
        {
            float value[3];
            for (auto& component : value) { component = std::bit_cast<float>(word());
                if (!std::isfinite(component) || std::abs(component) > 1e6f) throw std::runtime_error("CPU geometry position"); }
            p = {value[0],value[1],value[2]};
        }
        for (auto& index : result.indices) { index = word(); if (index >= vertices) throw std::runtime_error("CPU geometry index"); }
        output = std::move(result); error.clear(); return true;
    }
    catch (const std::exception& failure) { error = failure.what(); return false; }
}
}
