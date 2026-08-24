#include "RHIShaderReflection.h"

namespace
{
    std::string DescribeResource(const RHIShaderResourceReflection& resource)
    {
        std::string description = resource.name + "(" + std::string(ToString(resource.kind))
            + ",r" + std::to_string(resource.registerIndex)
            + ",space" + std::to_string(resource.registerSpace)
            + ",array" + std::to_string(resource.arrayElements)
            + ",bytes" + std::to_string(resource.byteSize) + ",fields=[";
        for (std::size_t index = 0; index < resource.fields.size(); ++index)
        {
            if (0 != index) description += ",";
            const RHIShaderFieldReflection& field = resource.fields[index];
            description += field.name + ":" + std::string(ToString(field.type.scalar))
                + "-" + std::to_string(field.type.rows)
                + "x" + std::to_string(field.type.columns)
                + "[" + std::to_string(field.type.arrayElements) + "]@"
                + std::to_string(field.byteOffset) + "+"
                + std::to_string(field.byteSize);
        }
        return description + "])";
    }
}

std::string_view ToString(RHIShaderStage stage)
{
    switch (stage)
    {
    case RHIShaderStage::Vertex: return "vertex";
    case RHIShaderStage::Pixel: return "pixel";
    case RHIShaderStage::Compute: return "compute";
    }
    return "unknown";
}

std::string_view ToString(RHIShaderResourceKind kind)
{
    switch (kind)
    {
    case RHIShaderResourceKind::ConstantBuffer: return "constant-buffer";
    case RHIShaderResourceKind::Texture: return "texture";
    case RHIShaderResourceKind::StorageTexture: return "storage-texture";
    case RHIShaderResourceKind::Sampler: return "sampler";
    case RHIShaderResourceKind::StructuredBuffer: return "structured-buffer";
    case RHIShaderResourceKind::StorageBuffer: return "storage-buffer";
    case RHIShaderResourceKind::ByteAddressBuffer: return "byte-address-buffer";
    case RHIShaderResourceKind::StorageByteAddressBuffer:
        return "storage-byte-address-buffer";
    }
    return "unknown";
}

std::string_view ToString(RHIShaderScalarKind kind)
{
    switch (kind)
    {
    case RHIShaderScalarKind::Bool: return "bool";
    case RHIShaderScalarKind::Int32: return "int32";
    case RHIShaderScalarKind::UInt32: return "uint32";
    case RHIShaderScalarKind::Float32: return "float32";
    }
    return "unknown";
}

bool AreShaderReflectionsEquivalent(const RHIShaderReflection& left,
    const RHIShaderReflection& right, std::string& outError)
{
    if (left.stage != right.stage)
    {
        outError = "shader reflection stage가 다르다: "
            + std::string(ToString(left.stage)) + " vs "
            + std::string(ToString(right.stage));
        return false;
    }
    if (left.resources.size() != right.resources.size())
    {
        outError = "shader reflection resource 수가 다르다: "
            + std::to_string(left.resources.size()) + " vs "
            + std::to_string(right.resources.size());
        return false;
    }
    for (std::size_t index = 0; index < left.resources.size(); ++index)
    {
        if (left.resources[index] == right.resources[index]) continue;
        outError = "shader reflection resource가 다르다: index="
            + std::to_string(index) + " left=" + DescribeResource(left.resources[index])
            + " right=" + DescribeResource(right.resources[index]);
        return false;
    }
    outError.clear();
    return true;
}
