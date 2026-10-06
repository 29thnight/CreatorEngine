#pragma once

#include "RHI/RHIPipelineState.h"
#include "TypeTrait.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Authoring
{
    class ReadNode;
}

enum class ShaderPropertyType : std::uint8_t
{
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Bool,
    Float4x4,
    Texture2D,
};

using ShaderPropertyDefault = std::variant<
    std::monostate,
    float,
    std::array<float, 2>,
    std::array<float, 3>,
    std::array<float, 4>,
    std::int32_t,
    bool,
    std::array<float, 16>,
    FileGuid>;

struct ShaderPropertyDesc
{
    std::string name;
    std::string label;
    ShaderPropertyType type{ ShaderPropertyType::Float };
    ShaderPropertyDefault defaultValue;
    // Graph-generated names match reflected symbols; the stable source ID and
    // authored label retain the Blackboard identity independently of registers.
    std::uint64_t parameterId{};
    std::string semantic{ "value" }; // value|vector|color|normal|texture
    std::string colorSpace{ "data" }; // data|linear|srgb
    bool exposed{ true };
    bool operator==(const ShaderPropertyDesc&) const = default;
};

struct ShaderMaterialSampler
{
    std::string name;
    std::string description;
    std::uint64_t parameterId{};
    bool exposed{};
    bool operator==(const ShaderMaterialSampler&) const = default;
};

struct ShaderGeneratedMaterial
{
    static constexpr std::uint32_t kAdapterVersion = 1;
    FileGuid graphGuid{};
    std::uint32_t adapterVersion{ kAdapterVersion };
    std::string generation;
    std::string sourceSha256;
    std::uint32_t features{};
    bool surface{};
    bool volume{};
    std::vector<ShaderMaterialSampler> samplers;
    bool operator==(const ShaderGeneratedMaterial&) const = default;
};

struct ShaderKeywordAxis
{
    std::string name;
    std::vector<std::string> values;
    bool operator==(const ShaderKeywordAxis&) const = default;
};

struct ShaderStageEntry
{
    std::string entry;
    bool operator==(const ShaderStageEntry&) const = default;
};

enum class ShaderPassQueue : std::uint8_t
{
    Opaque,
    Transparent,
    Shadow,
    Compute,
};

enum class ShaderBlendMode : std::uint8_t
{
    Off,
    Alpha,
    Additive,
};

// ShaderMeta의 state 블록은 별도 그래픽 어휘를 만들지 않는다. 실제 PSO 기술과
// 같은 RHI 열거를 소유하고, 조립 시 bytecode/layout/format을 보존한 채 상태만 채운다.
struct ShaderRenderState
{
    RHIFillMode fillMode{ RHIFillMode::Solid };
    RHICullMode cullMode{ RHICullMode::Back };
    ShaderBlendMode blendMode{ ShaderBlendMode::Off };
    RHICompareOp depthTest{ RHICompareOp::Less };
    bool depthWrite{ true };
    RHITopologyType topologyType{ RHITopologyType::Triangle };

    void ApplyTo(RHIGraphicsPipelineDesc& desc) const;
    bool operator==(const ShaderRenderState&) const = default;
};

// Explicit author promise, checked in addition to reflected binding shape.
// IndexedInstanceV1 resolves gVisibleInstanceIds[SV_InstanceID] at t6/space0
// BEFORE all t4 instance and t5 palette reads. firstInstance is always zero.
// Position may use only the sealed affine world and nonnegative, normalized
// four-weight linear blend skinning (sum tolerance 1e-5, zero = bind pose).
// Wind, morph, displacement and other shader-side position changes are excluded;
// the source sphere expanded by the sealed pose must enclose every emitted vertex.
// Omitting the contract retains direct submission, even with the same root layout.
enum class ShaderGeometryVisibility : std::uint8_t
{
    Direct,
    IndexedInstanceV1,
};

struct ShaderPassDesc
{
    std::string name;
    std::optional<ShaderStageEntry> vertex;
    std::optional<ShaderStageEntry> pixel;
    std::optional<ShaderStageEntry> compute;
    ShaderRenderState state;
    ShaderPassQueue queue{ ShaderPassQueue::Opaque };
    ShaderGeometryVisibility geometryVisibility{ ShaderGeometryVisibility::Direct };

    bool IsCompute() const { return compute.has_value(); }
    bool operator==(const ShaderPassDesc&) const = default;
};

struct ShaderMeta
{
    static constexpr std::uint32_t kSchemaVersion = 1;

    FileGuid guid{};
    std::uint32_t schemaVersion{ kSchemaVersion };
    std::string name;
    std::filesystem::path source;
    // runtime-only loader context. authored source는 meta 파일 기준 상대 경로이므로
    // immutable snapshot이 RenderThread로 넘어간 뒤에도 정식 경로를 재구성하려면
    // origin이 함께 있어야 한다. 디스크 schema field는 아니다.
    std::filesystem::path originPath;
    std::vector<ShaderPropertyDesc> properties;
    std::vector<ShaderKeywordAxis> keywords;
    std::vector<ShaderPassDesc> passes;
    // Present only on derived Graph outputs. Graph/Blackboard owns authoring.
    std::optional<ShaderGeneratedMaterial> generatedMaterial;
    bool operator==(const ShaderMeta&) const = default;

    std::filesystem::path ResolveSource(
        const std::filesystem::path& metaPath) const;
};

namespace ShaderMetaLoader
{
    // guid는 기존 AssetMetaRegistry가 해석한 sidecar 정체성이다. 이 로더는 .meta를
    // 다시 읽거나 별도 registry를 만들지 않는다.
    bool LoadFile(const std::filesystem::path& path, const FileGuid& guid,
        ShaderMeta& outMeta, std::string& outError);

    // Cooked D5 document는 GUID-addressed Derived 경로에 있지만 `source`는 여전히
    // authoring .shadermeta 기준 상대 HLSL이다(B3 전 계약). documentPath에서
    // payload를 읽되 sourceOriginPath를 상대 경로 기준과 runtime origin으로 쓴다.
    bool LoadFile(const std::filesystem::path& documentPath,
        const std::filesystem::path& sourceOriginPath, const FileGuid& guid,
        ShaderMeta& outMeta, std::string& outError);

    // Editor importer와 자가 검증이 디스크 게시 전에 같은 검증기를 쓸 수 있는 경계.
    // originPath는 source 상대 경로의 기준이며 .shadermeta 파일명을 포함한다.
    bool Parse(std::string_view text, const std::filesystem::path& originPath,
        const FileGuid& guid, ShaderMeta& outMeta, std::string& outError);

    // AssetCooker와 cooked runtime loader가 같은 schema 검증을 공유하는 경계.
    // root의 소유자는 이 호출이 끝날 때까지 살아 있어야 한다.
    bool ParseDocument(const Authoring::ReadNode& root,
        const std::filesystem::path& originPath, const FileGuid& guid,
        ShaderMeta& outMeta, std::string& outError);

    // A verified cooked generation supplies the exact source bytes. Reuses the
    // authoring schema and source digest checks without reading source files.
    // This boundary accepts generated metadata only; it does not compile Slang.
    bool ParseGenerated(std::string_view text, std::string_view source,
        const FileGuid& guid, ShaderMeta& outMeta, std::string& outError);

    // Player accepts only the cooked CEDO tree, without entering a text parser.
    bool ParseGeneratedCooked(std::span<const std::byte> bytes, std::string_view source,
        const FileGuid& guid, ShaderMeta& outMeta, std::string& outError);
}
