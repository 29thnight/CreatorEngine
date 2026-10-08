#pragma once

#include "../../Lattice/Material/LXMaterialCompiler.h"
#include "ShaderMetaReflection.h"
#include "LXMaterialRuntime.h"

#include <type_traits>

namespace material_graph
{
struct VerifiedProduct;
struct GeneratedMaterialShader : LX::Runtime::ShaderGeneration
{
    // Retain the validated binary tree for generation validation and recooking.
    std::vector<std::byte> cookedContract;
};

    // Public converting owners retain the derived allocation without raw adoption
    // or a std pointer cast. Embedded metadata/layout remain aggregate borrows.
    static_assert(std::is_constructible_v<own::shared_owner<const LX::Runtime::ShaderGeneration>,
        own::shared_owner<const GeneratedMaterialShader>>);
    static_assert(!std::is_constructible_v<own::shared_owner<const LX::Runtime::ShaderGeneration>,
        const LX::Runtime::ShaderGeneration*>);

// Rebuild the common binding contract from the validated product layout and
// canonical generated pair, with no filesystem or compiler dependency.
bool RestoreMaterialShaderMeta(const VerifiedProduct& product, FileGuid graphGuid,
    std::string_view document, std::string_view source,
    GeneratedMaterialShader& result, std::string& error,
    std::span<const std::byte> cookedContract);

// The engine adapter owns pass descriptions; LX remains renderer-neutral.
// Only a complete, schema/reflection-validated source pair is published. The
// immutable generation directory is renamed in one operation. A failed candidate
// leaves the accepted files and caller's result intact.
bool PublishMaterialShaderMeta(const LX::LXMaterialProgram& program, FileGuid graphGuid,
    std::string_view source, std::span<const ShaderPassDesc> passes,
    std::span<const RHIShaderReflection> reflections, const std::filesystem::path& root,
    GeneratedMaterialShader& result, std::string& error);
} // namespace material_graph
