#pragma once

#include "../../Lattice/Material/LXMaterialCompiler.h"
#include "LXMaterialPipeline.h"
#include "RHI/RHIShaderReflection.h"
#include "RHI/RHIShaderCompiler.h"

#include <memory>
#include <span>

namespace material_graph
{
// 호스트(Includes/MaterialGraphSceneHost.slang)나 그것이 포함하는 셰이더를
// 바꾸면 올린다 — 안 올리면 쿠킹된 옛 바이트코드가 조용히 재사용된다.
// 13: 그림자 표본을 CascadedShadow.slang 으로 모으고 디버그 보기를 더했다.
// 14: 그림자 가장자리 텐트 필터와 넓은 필터의 법선 오프셋을 더했다.
// 15: GPU-visible owner IDs and direct runtime split-sum IBL bindings.
inline constexpr std::string_view SceneHostIdentity = "|lx-scene-host:16";
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
struct Capabilities
{
    std::uint32_t deferredFeatures = 0x003Fu;
    bool coreForward = false;
    bool layeredLookup = false;
    bool refraction = false;
    bool subsurface = false;
    bool volume = false;
};

struct Budget
{
    std::uint32_t textureSamples = 32;
    std::uint32_t textures = 64;
    std::uint32_t samplers = 64;
    std::uint32_t uniformBytes = 65536;
    std::uint32_t variants = 1;
    std::uint64_t compiledBytes = 32u * 1024u * 1024u;
};

struct Selection
{
    Tier tier{};
    Route route{};
    std::string reason;
};

bool SelectRoute(const LX::LXMaterialProgram& program, const Capabilities& capabilities, const Budget& budget,
                 Selection& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics);

inline constexpr std::uint32_t TextureRegister = 16;
inline constexpr std::uint32_t SamplerRegister = 3;
inline constexpr std::uint32_t UniformRegister = 2;

// LX's logical space1 resources are projected into the current product's
// space0 material ranges. Resource names and source-map lines stay unchanged.
std::string BuildBoundSource(const LX::LXMaterialProgram& program);

struct ParameterBinding
{
    LX::LXMaterialParameter parameter;
    std::uint32_t offset{};
    std::uint32_t bytes{};

    bool operator==(const ParameterBinding&) const = default;
};

struct BindingLayout
{
    std::uint32_t uniformBytes{};
    std::vector<ParameterBinding> parameters;
    std::vector<LX::LXMaterialResource> textures;
    std::vector<LX::LXMaterialResource> samplers;

    bool operator==(const BindingLayout&) const = default;
};

bool ResolveBindings(const LX::LXMaterialProgram& program, const RHIShaderReflection& reflection, const Budget& budget,
                     BindingLayout& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics);
bool MergeMaterialReflections(std::span<const RHIShaderReflection* const> stages, RHIShaderReflection& result,
                              std::vector<LX::LXMaterialDiagnostic>& diagnostics);

struct CompileTarget
{
    RHIShaderBinary binary{};
    std::string entry;
    std::string profile;
};

struct VerifiedProduct
{
    LX::LXMaterialProgram program;
    Selection selection;
    BindingLayout layout;
    std::vector<LX::LXMaterialShaderArtifact> shaders;
    std::vector<CompileTarget> targets;
    // Meta, source, common binding layout and bytecode share this generation.
    std::shared_ptr<const GeneratedMaterialShader> materialShader;
    // Sorted union of the files every target compile read (root source excluded).
    // In memory only: the cooked product format does not carry it.
    std::vector<std::filesystem::path> dependencies;
};

// The file starts with BuildBoundSource(program), followed by a host pass.
// All requested targets must compile and agree on the material byte layout.
bool VerifyProduct(const LX::LXMaterialProgram& program, const std::filesystem::path& sourceFile,
                   std::span<const CompileTarget> targets, const RHIShaderPermutation& permutation,
                   RHIShaderCompileOptions options, const Capabilities& capabilities, const Budget& budget,
                   VerifiedProduct& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics,
                   std::vector<RHIShaderReflection>* materialReflections = nullptr);

// Describe the selected cooked stages using their sealed compiler/dependency
// identity. Empty entry names select an unambiguous single surface pair.
bool DescribeGraphicsShader(const VerifiedProduct& product, RHIShaderBinary backend,
    std::string_view vertex, std::string_view pixel, LX::Runtime::GraphicsShaderDescription& result,
    std::string& error);

bool DescribeComputeShader(const VerifiedProduct& product, RHIShaderBinary backend,
    std::string_view entry, LX::Runtime::ComputeShaderDescription& result, std::string& error);

struct ParameterOverride
{
    LX::Id id{};
    LX::LXSocketValue value;
};

struct TextureBinding
{
    std::uint32_t slot{};
    RHITextureEntry texture;
    // The owner must keep both the CPU generation and its RHI registration live.
    std::shared_ptr<const void> owner;
    bool linearStorage = false;
};

struct ResourcePacket
{
    std::vector<std::uint8_t> uniforms;
    std::vector<RHIBindingDesc> textures;
    std::vector<RHISamplerDesc> samplers;
    std::vector<std::shared_ptr<const void>> owners;
};

// CPU instance packing uses the same reflected layout as the render packet.
bool PrepareUniforms(const BindingLayout& layout, std::span<const ParameterOverride> parameters,
                     std::vector<std::uint8_t>& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics);

bool PrepareResources(const BindingLayout& layout, std::span<const ParameterOverride> parameters,
                      std::span<const TextureBinding> textures, ResourcePacket& result,
                      std::vector<LX::LXMaterialDiagnostic>& diagnostics);

// The common Material packer has already produced this immutable instance's
// uniform block. Build resource descriptors without packing those values again.
bool PrepareResourcesWithUniforms(const BindingLayout& layout, std::span<const std::uint8_t> uniforms,
                      std::span<const TextureBinding> textures, ResourcePacket& result,
                      std::vector<LX::LXMaterialDiagnostic>& diagnostics);

// Cook payloads use a versioned, bounded binary envelope. Backend/entry pairs
// are unique and the exact dependency-bearing semantic key is retained.
struct CookedProgram
{
    VerifiedProduct product;
    std::string metadata;
    std::string boundSource;
};

inline constexpr std::uint32_t CookedProgramVersion = 4;
bool WriteCookedProgram(const VerifiedProduct& product, const Budget& budget, std::vector<std::uint8_t>& result,
                        std::string& error);
bool ReadCookedProgram(std::span<const std::uint8_t> bytes, const Budget& budget, CookedProgram& result,
                       std::string& error);

struct PipelineGeneration
{
    std::uint64_t generation{};
    std::string semanticKey;
    Selection selection;
    BindingLayout layout;
    ResourcePacket resources;
    LX::Runtime::GraphicsPipeline pipeline;
};

// One render-thread owner publishes the PSO, route, uniform layout and texture
// owners together. It must retire only handles exclusively owned by this slot.
class PipelineSlot
{
  public:
    // Bytecode is selected from the verified surface-only generation. Volume
    // must be published with its composition pipeline by the owning renderer.
    // The caller supplies fixed-function state, formats and the installed layout.
    bool Publish(IRenderPipelineCache& cache, const VerifiedProduct& product, RHIShaderBinary backend,
                 const Capabilities& capabilities, const Budget& budget, std::span<const ParameterOverride> parameters,
                 std::span<const TextureBinding> textures, const RHIGraphicsPipelineDesc& pipeline,
                 RHICompletionPoint retireAfter, std::vector<LX::LXMaterialDiagnostic>& diagnostics);
    const std::shared_ptr<const PipelineGeneration>& Active() const { return active_; }
    // Called with the completed graphics timeline before retiring CPU texture
    // owners. Zero fences retain owners until destruction after device idle.
    // The slot must outlive all submitted generations or be drained idle.
    void CollectRetired(RHICompletionPoint completed);
    std::size_t RetiredGenerationCount() const { return retired_.size(); }

  private:
    struct RetiredGeneration
    {
        std::shared_ptr<const PipelineGeneration> owner;
        RHICompletionPoint after;
    };
    std::shared_ptr<const PipelineGeneration> active_;
    std::vector<RetiredGeneration> retired_;
};
} // namespace material_graph
