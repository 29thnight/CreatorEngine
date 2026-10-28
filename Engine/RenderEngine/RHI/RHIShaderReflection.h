#include "../../Containers/ContainerAPI.h"
#pragma once

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

enum class RHIShaderStage : std::uint8_t
{
    Vertex,
    Pixel,
    Compute,
    Mesh,
};

enum class RHIShaderResourceKind : std::uint8_t
{
    ConstantBuffer,
    Texture,
    StorageTexture,
    Sampler,
    StructuredBuffer,
    StorageBuffer,
    ByteAddressBuffer,
    StorageByteAddressBuffer,
};

enum class RHIShaderScalarKind : std::uint8_t
{
    Bool,
    Int32,
    UInt32,
    Float32,
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] RHIShaderValueType
{
    RHIShaderScalarKind scalar{ RHIShaderScalarKind::Float32 };
    std::uint16_t rows{ 1 };
    std::uint16_t columns{ 1 };
    std::uint32_t arrayElements{ 1 };

    bool operator==(const RHIShaderValueType&) const = default;
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] RHIShaderFieldReflection
{
    std::string name;
    RHIShaderValueType type;
    std::uint32_t byteOffset{};
    std::uint32_t byteSize{};

    bool operator==(const RHIShaderFieldReflection&) const = default;
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] RHIShaderResourceReflection
{
    std::string name;
    RHIShaderResourceKind kind{ RHIShaderResourceKind::ConstantBuffer };
    std::uint32_t registerIndex{};
    std::uint32_t registerSpace{};
    std::uint32_t arrayElements{ 1 };
    std::uint32_t byteSize{};
    ce::dynamic_array<RHIShaderFieldReflection> fields;

    bool operator==(const RHIShaderResourceReflection&) const = default;
};

// backend decoration이 아니라 HLSL 논리 register/space 기준의 정본이다.
// Vulkan b/t/u/s binding shift는 Slang 추출 경계에서 제거한다.
struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] RHIShaderReflection
{
    RHIShaderStage stage{ RHIShaderStage::Vertex };
    ce::dynamic_array<RHIShaderResourceReflection> resources;

    bool operator==(const RHIShaderReflection&) const = default;
};

std::string_view ToString(RHIShaderStage stage);
std::string_view ToString(RHIShaderResourceKind kind);
std::string_view ToString(RHIShaderScalarKind kind);

bool AreShaderReflectionsEquivalent(const RHIShaderReflection& left,
    const RHIShaderReflection& right, std::string& outError);
