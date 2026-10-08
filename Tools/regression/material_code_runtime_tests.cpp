#include "Material.h"
#include "MaterialPropertyPacker.h"
#include "RHI/RHIShaderCompiler.h"
#include "Render/Scene/ExperimentMaterialSealing.h"
#include "AuthoringNodeViewAccess.h"
#include "AuthoringWriteNode.h"
#include "DataSystem.h"
#include "ReflectionTypedYml.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace reflgen::generated
{
    void register_RenderEngine(reflgen::registry&);
}

namespace
{
    bool SameRuntimeInstance(const own::shared_owner<const LX::Runtime::Instance>& left,
                             const own::shared_owner<const LX::Runtime::Instance>& right)
    {
        if (left && right)
        {
            return std::addressof(*left) == std::addressof(*right);
        }
        return !left && !right;
    }

    static_assert(std::is_same_v<decltype(std::declval<const Material&>().GetLXMaterialInstance()),
        own::shared_owner<const LX::Runtime::Instance>>);
    static_assert(std::is_same_v<decltype(EnhancedMaterialDrawSnapshot::runtimeInstance),
        own::shared_owner<const LX::Runtime::Instance>>);
    static_assert(std::is_same_v<decltype(EnhancedForwardMaterialDrawSnapshot::runtimeInstance),
        own::shared_owner<const LX::Runtime::Instance>>);
}

void RunMaterialCodeRuntimeTests(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const auto source = root / "GenericCode.slang";
    {
        std::ofstream file(source);
        file << R"(
cbuffer CodeValues : register(b2, space0) {
    float amount; float2 uv; float3 direction; float4 tint;
    int count; bool enabled; float4x4 transform;
};
Texture2D<float4> image : register(t4, space0);
SamplerState imageSampler : register(s1, space0);
RWStructuredBuffer<float4> result : register(u0, space0);
[numthreads(1,1,1)] void CodeCS() {
    result[0] = image.SampleLevel(imageSampler, uv, 0) + tint +
        float4(direction, amount + float(count) + (enabled ? 1 : 0)) + mul(transform, tint);
}
)";
        if (!file) throw std::runtime_error("Cannot write generic Code fixture");
    }
    std::size_t checks{};
    const auto check = [&](bool valid, const std::string& message) {
        ++checks;
        if (!valid) throw std::runtime_error(message);
    };
    std::string error;
    ShaderMeta meta;
    meta.guid = FileGuid{"66666666-6666-4666-8666-666666666666"};
    meta.name = "Generic code material";
    meta.source = source.filename();
    meta.originPath = root / "GenericCode.shadermeta";
    std::array<float, 16> identity{};
    identity[0] = identity[5] = identity[10] = identity[15] = 1;
    meta.properties = {
        {"amount", "Amount", ShaderPropertyType::Float, .23f},
        {"uv", "UV", ShaderPropertyType::Float2, std::array<float, 2>{.1f, .2f}},
        {"direction", "Direction", ShaderPropertyType::Float3, std::array<float, 3>{0, 1, 0}},
        {"tint", "Tint", ShaderPropertyType::Float4, std::array<float, 4>{.1f, .2f, .3f, 1}},
        {"count", "Count", ShaderPropertyType::Int, std::int32_t{-2}},
        {"enabled", "Enabled", ShaderPropertyType::Bool, true},
        {"transform", "Transform", ShaderPropertyType::Float4x4, identity},
        {"image", "Image", ShaderPropertyType::Texture2D, FileGuid{}}
    };
    meta.properties[2].exposed = false;
    meta.keywords = {{"QUALITY", {"low", "high"}}};
    ShaderPassDesc pass;
    pass.name = "Compute";
    pass.compute = ShaderStageEntry{"CodeCS"};
    pass.queue = ShaderPassQueue::Compute;
    meta.passes = {pass};
    std::vector<RHIShaderReflection> reflections;
    ShaderMetaBindingLayout layout;
    for (const auto output : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        RHIShaderCompiler::VerifiedShader verified;
        check(RHIShaderCompiler::VerifyFile(source.string(), "CodeCS", "cs_6_0", output, {}, verified, error),
              "Actual code compile/reflection: " + error);
        ShaderMetaBindingLayout candidate;
        check(ShaderMetaReflection::Resolve(meta, std::span(&verified.reflection, 1), candidate, error),
              "Actual code contract/layout: " + error);
        if (!reflections.empty()) check(candidate == layout, "Generic DXIL/SPIR-V binding parity");
        layout = std::move(candidate);
        reflections.push_back(std::move(verified.reflection));
    }
    const ShaderMetaHandle handle{333, 1};
    Material material;
    check(material.ConfigureShaderProperties(meta, layout, error, handle), "Code input adapts to LX: " + error);
    auto accepted = material.GetLXMaterialInstance();
    check(accepted && !material.HasMaterialGraph() && !accepted->shader->meta.generatedMaterial &&
          accepted->shader->codeHandle == handle, "Code shader retains generic contract without a Graph/BSDF program");
    float amount{};
    int count{};
    bool enabled{};
    check(material.TryGetFloat("CodeValues.amount", amount) && amount == .23f &&
          material.TryGetInt("CodeValues.count", count) && count == -2 &&
          material.TryGetBool("CodeValues.enabled", enabled) && enabled, "Generic defaults survive common packing");
    std::vector<std::uint8_t> reference(layout.constantBufferByteSize);
    for (const auto& property : meta.properties)
    {
        MaterialPropertyValue value;
        check(MaterialPropertyPacker::ApplyDefault(property, value, error) &&
              MaterialPropertyPacker::PackProperty(property, *MaterialPropertyPacker::FindBinding(layout, property.name),
                                                    value, reference, error), "Reference packer: " + property.name);
    }
    check(reference == accepted->uniforms, "Common instance is bit-exact with the established packer");
    check(material.TrySetVector("CodeValues.uv", math::vector2{.7f, .8f}) &&
          !SameRuntimeInstance(material.GetLXMaterialInstance(), accepted), "Float2 setter publishes an immutable LX snapshot");
    check(accepted->uniforms == reference && material.GetLXMaterialInstance()->uniforms != reference,
          "Publishing an own-backed instance preserves the retained immutable snapshot");
    math::vector4 uv{};
    check(material.TryGetVector("CodeValues.uv", uv) && uv.x == .7f && uv.y == .8f, "Float2 common readback");
    math::matrix4x4 matrix{};
    identity[12] = 3.f;
    std::memcpy(&matrix, identity.data(), sizeof(matrix));
    check(material.TrySetMatrix("CodeValues.transform", matrix), "Float4x4 mutation stays generic");
    math::matrix4x4 actual{};
    check(material.TryGetMatrix("CodeValues.transform", actual) && !std::memcmp(&actual, &matrix, sizeof(matrix)),
          "Matrix byte-exact common readback");
    check(material.TrySetKeywordSelection("QUALITY", "high") && material.GetKeywordSelections()[0] == 1,
          "Keyword changes publish through the same runtime owner");
    accepted = material.GetLXMaterialInstance();
    check(!material.TrySetFloat("CodeValues.amount", std::numeric_limits<float>::quiet_NaN()) &&
          SameRuntimeInstance(material.GetLXMaterialInstance(), accepted), "Nonfinite edit preserves accepted bytes and owner");
    check(!material.TrySetVector("CodeValues.direction", math::vector3{1, 0, 0}) &&
          !material.TrySetInt("CodeValues.amount", 7) && !material.TrySetKeywordSelection("QUALITY", "missing") &&
          SameRuntimeInstance(material.GetLXMaterialInstance(), accepted), "Private/type/keyword errors preserve the snapshot");
    Material clone(material);
    check(clone.ConfigureShaderProperties(meta, layout, error, handle) &&
          clone.GetLXMaterialInstance()->shader == accepted->shader &&
          clone.TrySetFloat("CodeValues.amount", .8f) && SameRuntimeInstance(material.GetLXMaterialInstance(), accepted),
          "Code clone shares the shader contract and edits an independent instance");
    auto badLayout = layout;
    badLayout.properties[1].byteOffset = 0;
    check(!material.ConfigureShaderProperties(meta, badLayout, error, {333, 2}) &&
          SameRuntimeInstance(material.GetLXMaterialInstance(), accepted), "Bad reload layout preserves the complete accepted generation");
    auto changedMeta = meta;
    changedMeta.name += " reloaded";
    check(clone.ConfigureShaderProperties(changedMeta, layout, error, {333, 2}) &&
          clone.GetLXMaterialInstance()->shader != accepted->shader && accepted->shader->meta.name == meta.name,
          "Reload installs a distinct immutable shader owner");

    ExperimentMaterialSealing::SealSource sealSource;
    check(ExperimentMaterialSealing::BuildSealSourceFromLegacy(material, meta, sealSource, error),
          "Generic Code source adapts to frame sealing: " + error);
    std::vector<std::uint8_t> sealedBytes;
    std::vector<EnhancedMaterialTextureBinding> bindings;
    own::shared_owner<const LX::Runtime::Instance> sealed;
    check(ExperimentMaterialSealing::SealCore(sealSource, meta, layout, sealedBytes, bindings, error, &sealed, handle) &&
          sealed && sealed->shader == accepted->shader && sealedBytes == accepted->uniforms &&
          sealed->keywordSelections == accepted->keywordSelections, "Frame sealing consumes the same generic LX contract");
    EnhancedMaterialDrawSnapshot gbuffer;
    gbuffer.shaderMetaHandle = handle;
    gbuffer.bindingLayout = layout;
    gbuffer.propertyBytes = sealedBytes;
    gbuffer.keywordSelections = sealed->keywordSelections;
    gbuffer.runtimeInstance = sealed;
    EnhancedForwardMaterialDrawSnapshot forward;
    forward.shaderMetaHandle = handle;
    forward.bindingLayout = layout;
    forward.propertyBytes = sealedBytes;
    forward.keywordSelections = sealed->keywordSelections;
    forward.runtimeInstance = sealed;
    check(gbuffer.IsValid() && forward.IsValid(), "Existing GBuffer and Forward packets retain the common runtime owner");
    const auto retainedBytes = sealedBytes;
    const auto retained = sealed;
    sealSource.codeValues[0].m_numericValue[0] = std::numeric_limits<float>::infinity();
    check(!ExperimentMaterialSealing::SealCore(sealSource, meta, layout, sealedBytes, bindings, error, &sealed, handle) &&
          SameRuntimeInstance(sealed, retained) && sealedBytes == retainedBytes,
          "Bad frame input preserves previous owner and outputs");

    reflgen::generated::register_RenderEngine(Meta::Types());
    Meta::Typed::RegisterOps<Material>();
    Authoring::WriteDocument saved;
    auto* data = DataSystem::GetInstance();
    check(data->SerializeMaterialPayload(material, saved.Root()), "Actual generic Material authoring save");
    const auto node = saved.Root().Read();
    Material restored;
    check(data->DeserializeMaterialPayload(restored, Authoring::NodeViewAccess::Make(node)) &&
          restored.ConfigureShaderProperties(meta, layout, error, handle) &&
          restored.GetLXMaterialInstance()->uniforms == accepted->uniforms, "Generic Matrix/Float2 save and reopen");
    std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
    Material decoded;
    check(data->SerializeMaterialBinaryPayload(material, binary) && data->DeserializeMaterialBinaryPayload(decoded, binary) &&
          decoded.ConfigureShaderProperties(meta, layout, error, handle) &&
          decoded.GetLXMaterialInstance()->uniforms == accepted->uniforms, "Generic Matrix/Float2 binary reopen");
    std::filesystem::remove(source);
    check(accepted->shader->meta.name == meta.name && material.TryGetMatrix("CodeValues.transform", actual),
          "Accepted code contract and values are independent of loader file lifetime");
    std::cout << "MAT7_CODE_RUNTIME_OK checks=" << checks << " backends=2 float2=true matrices=true sharedShader=true frameSeal=true failurePreserved=true\n";
}
