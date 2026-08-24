#pragma once

#include "RHI/RHIPipelineState.h"
#include "TypeTrait.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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
};

struct ShaderKeywordAxis
{
    std::string name;
    std::vector<std::string> values;
};

struct ShaderStageEntry
{
    std::string entry;
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
};

struct ShaderPassDesc
{
    std::string name;
    std::optional<ShaderStageEntry> vertex;
    std::optional<ShaderStageEntry> pixel;
    std::optional<ShaderStageEntry> compute;
    ShaderRenderState state;
    ShaderPassQueue queue{ ShaderPassQueue::Opaque };

    bool IsCompute() const { return compute.has_value(); }
};

struct ShaderMeta
{
    static constexpr std::uint32_t kSchemaVersion = 1;

    FileGuid guid{};
    std::uint32_t schemaVersion{ kSchemaVersion };
    std::string name;
    std::filesystem::path source;
    std::vector<ShaderPropertyDesc> properties;
    std::vector<ShaderKeywordAxis> keywords;
    std::vector<ShaderPassDesc> passes;

    std::filesystem::path ResolveSource(
        const std::filesystem::path& metaPath) const;
};

namespace ShaderMetaLoader
{
    // guid는 기존 AssetMetaRegistry가 해석한 sidecar 정체성이다. 이 로더는 .meta를
    // 다시 읽거나 별도 registry를 만들지 않는다.
    bool LoadFile(const std::filesystem::path& path, const FileGuid& guid,
        ShaderMeta& outMeta, std::string& outError);

    // Editor importer와 자가 검증이 디스크 게시 전에 같은 검증기를 쓸 수 있는 경계.
    // originPath는 source 상대 경로의 기준이며 .shadermeta 파일명을 포함한다.
    bool Parse(std::string_view text, const std::filesystem::path& originPath,
        const FileGuid& guid, ShaderMeta& outMeta, std::string& outError);
}
