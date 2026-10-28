#include "../Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "../../Lattice/Material/LXMaterialCompiler.h"
#include "LXMaterialPipeline.h"
#include "RHI/RHIShaderReflection.h"
#include "RHI/RHIShaderCompiler.h"

#include "../Utility_Framework/Foundation/StandardLibrary.h"

class Texture;

namespace material_graph
{
    // 호스트(Includes/MaterialGraphSceneHost.slang)나 그것이 포함하는 셰이더를
    // 바꾸면 올린다 — 안 올리면 쿠킹된 옛 바이트코드가 조용히 재사용된다.
    // 13: 그림자 표본을 CascadedShadow.slang 으로 모으고 디버그 보기를 더했다.
    // 14: 그림자 가장자리 텐트 필터와 넓은 필터의 법선 오프셋을 더했다.
    // 15: GPU-visible owner IDs and direct runtime split-sum IBL bindings.
    // 23: camera compute meshlet IDs and GPU-finalized 2D indirect dispatch ABI.
    // 30: occluder depth uses the HW/SW visibility producer; LXSceneDepthPS removed.
    // 32: visibility-packet Temporal shader interface (forced recook).
    // 34: spatial replacement DAG addressing and group-based GPU LOD (forced recook).
    // 31: object-space vertex streams; lx_world/lx_previousWorld/lx_shadowWorld constants.
    // 35: resident meshlet attribute packets and direct GPU pose/attribute consumption.
    // 36: float4-row geometry SRVs; aligned bone rows avoid whole-point gathers (forced recook).
    // 37: stable sparse cluster addresses and GPU LOD demand feedback.
    // 38: bounded component decoding and vector CGQ descriptors for shader preparation.
    // 39: authenticated thin geometry metadata and paged GPU connectivity.
    // 40: record-addressed GPU bucket packets and shared scene/material tables.
    // 41: opt-in primitive cache의 별도 GBuffer/Temporal cooked entry와 t174/t175 계약.
    inline constexpr std::string_view SceneHostIdentity = "|lx-scene-host:42";
    struct GeneratedMaterialShader;
    enum class Tier : std::uint8_t
    {
        Standard,
        Layered,
        Special
};

enum class Route : std::uint8_t
{
    Deferred,
    Forward,
    Volume
};

// Capabilities describe implemented transports, not user-selected quality.
struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] Capabilities
{
    std::uint32_t deferredFeatures = 0x003Fu;
    bool coreForward = false;
    bool layeredLookup = false;
    bool refraction = false;
    bool subsurface = false;
    bool volume = false;
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] Budget
{
    std::uint32_t textureSamples = 32;
    std::uint32_t textures = 64;
    std::uint32_t samplers = 64;
    std::uint32_t uniformBytes = 65536;
    std::uint32_t variants = 1;
    std::uint64_t compiledBytes = 32u * 1024u * 1024u;
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] Selection
{
    Tier tier{};
    Route route{};
    std::string reason;
};

bool SelectRoute(const LX::LXMaterialProgram& program, const Capabilities& capabilities, const Budget& budget,
                 Selection& result, ce::dynamic_array<LX::LXMaterialDiagnostic>& diagnostics);

inline constexpr std::uint32_t TextureRegister = 16384;
inline constexpr std::uint32_t SamplerRegister = 3;
inline constexpr std::uint32_t UniformRegister = 2;
inline constexpr std::uint32_t DescriptorIndexRegister = 15;
inline constexpr std::uint32_t BindlessTextureCapacity = 4096;
inline constexpr std::uint32_t BindlessSamplerCapacity = 36;

inline RHIShaderResourceReflection DescriptorIndexReflection()
{
    RHIShaderResourceReflection result;
    result.name = "LXDescriptorIndices";
    result.registerIndex = DescriptorIndexRegister;
    result.byteSize = 512;
    result.fields.push_back({"lx_descriptor_indices", {RHIShaderScalarKind::UInt32, 1, 4, 32}, 0, 512});
    return result;
}

// LX's logical space1 resources are projected into the current product's
// space0 material ranges. Resource names and source-map lines stay unchanged.
std::string BuildBoundSource(const LX::LXMaterialProgram& program);

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] ParameterBinding
{
    LX::LXMaterialParameter parameter;
    std::uint32_t offset{};
    std::uint32_t bytes{};

    bool operator==(const ParameterBinding&) const = default;
};

// Internal per-resource storage contract, not an editable graph parameter.
// Values match MaterialGraphTextureSample.slang: 0 = RGBA, 1 = BC5 normal XY.
enum class TextureSampleEncoding : std::uint32_t
{
    Rgba = 0,
    BC5Normal = 1
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] TextureEncodingBinding
{
    std::uint32_t slot{};
    std::uint32_t offset{};

    bool operator==(const TextureEncodingBinding&) const = default;
};

inline std::string TextureEncodingUniformName(std::uint32_t slot)
{
    return "lx_bound_texture_" + std::to_string(slot) + "_encoding";
}

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] BindingLayout
{
    std::uint32_t uniformBytes{};
    ce::dynamic_array<ParameterBinding> parameters;
    ce::dynamic_array<TextureEncodingBinding> textureEncodings;
    ce::dynamic_array<LX::LXMaterialResource> textures;
    ce::dynamic_array<LX::LXMaterialResource> samplers;

    bool operator==(const BindingLayout&) const = default;
};

bool ResolveBindings(const LX::LXMaterialProgram& program, const RHIShaderReflection& reflection, const Budget& budget,
                     BindingLayout& result, ce::dynamic_array<LX::LXMaterialDiagnostic>& diagnostics);
bool MergeMaterialReflections(std::span<const RHIShaderReflection* const> stages, RHIShaderReflection& result,
                              ce::dynamic_array<LX::LXMaterialDiagnostic>& diagnostics);

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] CompileTarget
{
    RHIShaderBinary binary{};
    std::string entry;
    std::string profile;
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] VerifiedProduct
{
    LX::LXMaterialProgram program;
    Selection selection;
    BindingLayout layout;
    ce::dynamic_array<LX::LXMaterialShaderArtifact> shaders;
    ce::dynamic_array<CompileTarget> targets;
    // Meta, source, common binding layout and bytecode share this generation.
    own::shared_owner<const GeneratedMaterialShader> materialShader;
    // Sorted union of the files every target compile read (root source excluded).
    // In memory only: the cooked product format does not carry it.
    ce::dynamic_array<std::filesystem::path> dependencies;
};

// The file starts with BuildBoundSource(program), followed by a host pass.
// All requested targets must compile and agree on the material byte layout.
bool VerifyProduct(const LX::LXMaterialProgram& program, const std::filesystem::path& sourceFile,
                   std::span<const CompileTarget> targets, const RHIShaderPermutation& permutation,
                   RHIShaderCompileOptions options, const Capabilities& capabilities, const Budget& budget,
                   VerifiedProduct& result, ce::dynamic_array<LX::LXMaterialDiagnostic>& diagnostics,
                   ce::dynamic_array<RHIShaderReflection>* materialReflections = nullptr);

// Describe the selected cooked stages using their sealed compiler/dependency
// identity. Empty entry names select an unambiguous single surface pair.
bool DescribeGraphicsShader(const VerifiedProduct& product, RHIShaderBinary backend, std::string_view vertex,
                            std::string_view pixel, LX::Runtime::GraphicsShaderDescription& result, std::string& error);

bool DescribeComputeShader(const VerifiedProduct& product, RHIShaderBinary backend, std::string_view entry,
                           LX::Runtime::ComputeShaderDescription& result, std::string& error);

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] ParameterOverride
{
    LX::Id id{};
    LX::LXSocketValue value;
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] TextureBinding
{
    std::uint32_t slot{};
    RHITextureEntry texture;
    // CPU representation pin; the texture cache separately retires native storage.
    own::shared_owner<const Texture> owner;
    bool linearStorage = false;
};

struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] ResourcePacket
{
    ce::dynamic_array<std::uint8_t> uniforms;
    ce::dynamic_array<RHIBindingDesc> textures;
    ce::dynamic_array<RHISamplerDesc> samplers;
    // One pin per Texture representation, deduplicated by m_assetId.
    ce::dynamic_array<own::shared_owner<const Texture>> owners;
};

// CPU instance packing uses the same reflected layout as the render packet.
bool PrepareUniforms(const BindingLayout& layout, std::span<const ParameterOverride> parameters,
                     ce::dynamic_array<std::uint8_t>& result, ce::dynamic_array<LX::LXMaterialDiagnostic>& diagnostics);

bool PrepareResources(const BindingLayout& layout, std::span<const ParameterOverride> parameters,
                      std::span<const TextureBinding> textures, ResourcePacket& result,
                      ce::dynamic_array<LX::LXMaterialDiagnostic>& diagnostics);

// The common Material packer has already produced this immutable instance's
// uniform block. Build resource descriptors without packing those values again.
bool PrepareResourcesWithUniforms(const BindingLayout& layout, std::span<const std::uint8_t> uniforms,
                                  std::span<const TextureBinding> textures, ResourcePacket& result,
                                  ce::dynamic_array<LX::LXMaterialDiagnostic>& diagnostics);

// Cook payloads use a versioned, bounded binary envelope. Backend/entry pairs
// are unique and the exact dependency-bearing semantic key is retained.
struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] CookedProgram
{
    VerifiedProduct product;
    std::string metadata;
    std::string boundSource;
    // 소스 생산자가 검증한 입력과 환경이다. 패키지 바이트에서는 복원하지 않는다.
    std::string authoringSourceKey;
    std::string authoringEnvironmentKey;
    // 생성 소스 자체는 보관하지 않아도 그 검색 위치에 새 include가 생겼는지는 다시 확인한다.
    std::filesystem::path authoringSourceDirectory;
};

// Scene mesh shaders consume GPU-compacted meshlet IDs and 2D indirect work.
// Previous products cannot be rebound to this host; assets must be recooked.
// 13: packet material byte loads share the verified reflected uniform packing.
inline constexpr std::uint32_t CookedProgramVersion = 13;
bool WriteCookedProgram(const VerifiedProduct& product, const Budget& budget, ce::dynamic_array<std::uint8_t>& result,
                        std::string& error);
bool ReadCookedProgram(std::span<const std::uint8_t> bytes, const Budget& budget, CookedProgram& result,
                       std::string& error);

} // namespace material_graph
