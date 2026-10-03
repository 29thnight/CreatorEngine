#pragma once

#include "RHIShaderBlob.h"
#include "RHIShaderReflection.h"
#include <span>
#include <type_traits>

// Value-only, bounded serialization of one linked program's bytecode/layout.
// The file envelope binds this payload to its dependency/compiler request key.
namespace rhi_shader_verified_cache
{
inline constexpr std::uint64_t kMaxBlobBytes = 64ull << 20;
inline constexpr std::uint64_t kMaxPayloadBytes = kMaxBlobBytes + (4ull << 20);
inline constexpr std::uint32_t kMaxResources = 4096;
inline constexpr std::uint32_t kMaxFields = 65536;
inline constexpr std::uint32_t kMaxNameBytes = 16384;

struct Writer
{
    std::vector<std::uint8_t> bytes;
    bool valid{true};
    void Raw(const void* data, std::size_t size)
    {
        if (!valid || size > kMaxPayloadBytes - bytes.size()) { valid = false; return; }
        const auto* begin = static_cast<const std::uint8_t*>(data);
        bytes.insert(bytes.end(), begin, begin + size);
    }
    template<class T> void Integer(T value)
    {
        static_assert(std::is_unsigned_v<T>);
        for (std::size_t i = 0; i < sizeof(T); ++i)
        {
            const auto byte = static_cast<std::uint8_t>(value >> (i * 8));
            Raw(&byte, 1);
        }
    }
    void String(const std::string& value)
    {
        if (value.size() > kMaxNameBytes) { valid = false; return; }
        Integer(static_cast<std::uint32_t>(value.size()));
        Raw(value.data(), value.size());
    }
};

struct Reader
{
    std::span<const std::uint8_t> bytes;
    std::size_t cursor{};
    template<class T> bool Integer(T& value)
    {
        static_assert(std::is_unsigned_v<T>);
        if (sizeof(T) > bytes.size() - cursor) return false;
        value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value = static_cast<T>(value | (static_cast<T>(bytes[cursor++]) << (i * 8)));
        return true;
    }
    bool String(std::string& value)
    {
        std::uint32_t size{};
        if (!Integer(size) || size > kMaxNameBytes || size > bytes.size() - cursor) return false;
        value.assign(reinterpret_cast<const char*>(bytes.data() + cursor), size);
        cursor += size;
        return true;
    }
};

inline bool Encode(const RHIShaderBlob& blob, const RHIShaderReflection& reflection,
                   std::vector<std::uint8_t>& payload)
{
    if (!blob.IsValid() || blob.Size() > kMaxBlobBytes || reflection.resources.size() > kMaxResources)
        return false;
    Writer w;
    w.Integer(static_cast<std::uint64_t>(blob.Size()));
    w.Raw(blob.Data(), blob.Size());
    w.Integer(static_cast<std::uint8_t>(reflection.stage));
    w.Integer(static_cast<std::uint32_t>(reflection.resources.size()));
    std::size_t fields = 0;
    for (const auto& resource : reflection.resources)
    {
        if (resource.fields.size() > kMaxFields - fields) return false;
        fields += resource.fields.size();
        w.String(resource.name);
        w.Integer(static_cast<std::uint8_t>(resource.kind));
        w.Integer(resource.registerIndex);
        w.Integer(resource.registerSpace);
        w.Integer(resource.arrayElements);
        w.Integer(resource.byteSize);
        w.Integer(static_cast<std::uint32_t>(resource.fields.size()));
        for (const auto& field : resource.fields)
        {
            w.String(field.name);
            w.Integer(static_cast<std::uint8_t>(field.type.scalar));
            w.Integer(field.type.rows);
            w.Integer(field.type.columns);
            w.Integer(field.type.arrayElements);
            w.Integer(field.byteOffset);
            w.Integer(field.byteSize);
        }
    }
    if (!w.valid) return false;
    payload = std::move(w.bytes);
    return true;
}

inline bool Decode(std::span<const std::uint8_t> payload, RHIShaderStage expectedStage,
                   RHIShaderBlob& blob, RHIShaderReflection& reflection)
{
    if (payload.size() > kMaxPayloadBytes) return false;
    Reader r{payload};
    std::uint64_t blobSize{};
    if (!r.Integer(blobSize) || !blobSize || blobSize > kMaxBlobBytes || blobSize > payload.size() - r.cursor)
        return false;
    const auto blobOffset = r.cursor;
    r.cursor += static_cast<std::size_t>(blobSize);
    std::uint8_t stage{};
    std::uint32_t resources{};
    if (!r.Integer(stage) || stage != static_cast<std::uint8_t>(expectedStage) ||
        !r.Integer(resources) || resources > kMaxResources) return false;
    RHIShaderReflection candidate;
    candidate.stage = expectedStage;
    candidate.resources.reserve(resources);
    std::uint32_t totalFields = 0;
    for (std::uint32_t i = 0; i < resources; ++i)
    {
        RHIShaderResourceReflection resource;
        std::uint8_t kind{};
        std::uint32_t fields{};
        if (!r.String(resource.name) || !r.Integer(kind) ||
            kind > static_cast<std::uint8_t>(RHIShaderResourceKind::StorageByteAddressBuffer) ||
            !r.Integer(resource.registerIndex) || !r.Integer(resource.registerSpace) ||
            !r.Integer(resource.arrayElements) || !resource.arrayElements ||
            !r.Integer(resource.byteSize) || !r.Integer(fields) || fields > kMaxFields - totalFields)
            return false;
        resource.kind = static_cast<RHIShaderResourceKind>(kind);
        totalFields += fields;
        resource.fields.reserve(fields);
        for (std::uint32_t j = 0; j < fields; ++j)
        {
            RHIShaderFieldReflection field;
            std::uint8_t scalar{};
            if (!r.String(field.name) || !r.Integer(scalar) ||
                scalar > static_cast<std::uint8_t>(RHIShaderScalarKind::Float32) ||
                !r.Integer(field.type.rows) || !field.type.rows ||
                !r.Integer(field.type.columns) || !field.type.columns ||
                !r.Integer(field.type.arrayElements) || !field.type.arrayElements ||
                !r.Integer(field.byteOffset) || !r.Integer(field.byteSize) ||
                std::uint64_t(field.byteOffset) + field.byteSize > resource.byteSize)
                return false;
            field.type.scalar = static_cast<RHIShaderScalarKind>(scalar);
            resource.fields.push_back(std::move(field));
        }
        candidate.resources.push_back(std::move(resource));
    }
    if (r.cursor != payload.size()) return false;
    RHIShaderBlob candidateBlob;
    candidateBlob.Assign(payload.data() + blobOffset, static_cast<std::size_t>(blobSize));
    blob = std::move(candidateBlob);
    reflection = std::move(candidate);
    return true;
}
}
