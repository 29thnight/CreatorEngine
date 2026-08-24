#include "ShaderMeta.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <unordered_set>

namespace
{
    constexpr std::size_t kMaxMetaBytes = 1024 * 1024;
    constexpr std::size_t kMaxNameBytes = 128;
    constexpr std::size_t kMaxSourceBytes = 512;
    constexpr std::size_t kMaxProperties = 256;
    constexpr std::size_t kMaxKeywordAxes = 32;
    constexpr std::size_t kMaxKeywordValues = 16;
    constexpr std::size_t kMaxPasses = 32;

    bool Fail(std::string_view context, std::string_view detail,
        std::string& outError)
    {
        outError = std::string(context) + ": " + std::string(detail);
        return false;
    }

    bool IsIdentifier(std::string_view value)
    {
        if (value.empty() || value.size() > kMaxNameBytes) return false;
        const auto alpha = [](char character)
        {
            return (character >= 'A' && character <= 'Z')
                || (character >= 'a' && character <= 'z');
        };
        const auto digit = [](char character)
        {
            return character >= '0' && character <= '9';
        };
        if ('_' != value.front() && !alpha(value.front())) return false;
        for (const char character : value.substr(1))
        {
            if ('_' != character && !alpha(character) && !digit(character))
                return false;
        }
        return true;
    }

    bool ValidateMap(const YAML::Node& node,
        std::initializer_list<std::string_view> allowed,
        std::string_view context, std::string& outError)
    {
        if (!node || !node.IsMap()) return Fail(context, "map이어야 한다", outError);
        for (const auto& field : node)
        {
            if (!field.first.IsScalar())
                return Fail(context, "field 이름은 scalar여야 한다", outError);
            const std::string key = field.first.Scalar();
            const bool known = std::any_of(allowed.begin(), allowed.end(),
                [&key](std::string_view candidate) { return key == candidate; });
            if (!known) return Fail(context, "알 수 없는 field '" + key + "'", outError);
        }
        return true;
    }

    bool ReadRequiredScalar(const YAML::Node& node, const char* key,
        std::string_view context, std::string& outValue, std::string& outError)
    {
        const YAML::Node value = node[key];
        if (!value || !value.IsScalar())
            return Fail(context, std::string("필수 scalar '") + key + "'가 없다", outError);
        outValue = value.Scalar();
        if (outValue.empty())
            return Fail(context, std::string("'") + key + "'가 비었다", outError);
        return true;
    }

    bool ReadIdentifier(const YAML::Node& node, const char* key,
        std::string_view context, std::string& outValue, std::string& outError)
    {
        if (!ReadRequiredScalar(node, key, context, outValue, outError)) return false;
        if (!IsIdentifier(outValue))
            return Fail(context, std::string("'") + key + "'가 식별자가 아니다", outError);
        return true;
    }

    bool ReadStrictBool(const YAML::Node& node, const char* key,
        std::string_view context, bool& outValue, std::string& outError)
    {
        const YAML::Node value = node[key];
        if (!value || !value.IsScalar())
            return Fail(context, std::string("'") + key + "'는 bool scalar여야 한다", outError);
        const std::string scalar = value.Scalar();
        if ("true" == scalar) outValue = true;
        else if ("false" == scalar) outValue = false;
        else return Fail(context, std::string("'") + key + "'는 true|false여야 한다", outError);
        return true;
    }

    template<std::size_t Size>
    bool ParseFloatArray(const YAML::Node& node, std::string_view context,
        std::array<float, Size>& outValue, std::string& outError)
    {
        if (!node || !node.IsSequence() || node.size() != Size)
            return Fail(context, std::to_string(Size) + "개 float sequence여야 한다", outError);
        for (std::size_t index = 0; index < Size; ++index)
        {
            if (!node[index].IsScalar())
                return Fail(context, "배열 원소는 float scalar여야 한다", outError);
            const float value = node[index].as<float>();
            if (!std::isfinite(value))
                return Fail(context, "NaN/Inf 기본값은 허용하지 않는다", outError);
            outValue[index] = value;
        }
        return true;
    }

    bool ParsePropertyType(std::string_view value, ShaderPropertyType& outType)
    {
        if ("float" == value) outType = ShaderPropertyType::Float;
        else if ("float2" == value) outType = ShaderPropertyType::Float2;
        else if ("float3" == value) outType = ShaderPropertyType::Float3;
        else if ("float4" == value) outType = ShaderPropertyType::Float4;
        else if ("int" == value) outType = ShaderPropertyType::Int;
        else if ("bool" == value) outType = ShaderPropertyType::Bool;
        else if ("float4x4" == value) outType = ShaderPropertyType::Float4x4;
        else if ("texture2d" == value) outType = ShaderPropertyType::Texture2D;
        else return false;
        return true;
    }

    bool ParsePropertyDefault(const YAML::Node& node, ShaderPropertyType type,
        std::string_view context, ShaderPropertyDefault& outValue,
        std::string& outError)
    {
        if (!node)
        {
            if (ShaderPropertyType::Texture2D == type)
            {
                outValue = std::monostate{};
                return true;
            }
            return Fail(context, "비리소스 property는 default가 필요하다", outError);
        }

        switch (type)
        {
        case ShaderPropertyType::Float:
        {
            if (!node.IsScalar()) return Fail(context, "float default가 아니다", outError);
            const float value = node.as<float>();
            if (!std::isfinite(value)) return Fail(context, "NaN/Inf default", outError);
            outValue = value;
            return true;
        }
        case ShaderPropertyType::Float2:
        {
            std::array<float, 2> value{};
            if (!ParseFloatArray(node, context, value, outError)) return false;
            outValue = value;
            return true;
        }
        case ShaderPropertyType::Float3:
        {
            std::array<float, 3> value{};
            if (!ParseFloatArray(node, context, value, outError)) return false;
            outValue = value;
            return true;
        }
        case ShaderPropertyType::Float4:
        {
            std::array<float, 4> value{};
            if (!ParseFloatArray(node, context, value, outError)) return false;
            outValue = value;
            return true;
        }
        case ShaderPropertyType::Int:
            if (!node.IsScalar()) return Fail(context, "int default가 아니다", outError);
            outValue = node.as<std::int32_t>();
            return true;
        case ShaderPropertyType::Bool:
        {
            if (!node.IsScalar()) return Fail(context, "bool default가 아니다", outError);
            const std::string value = node.Scalar();
            if ("true" == value) outValue = true;
            else if ("false" == value) outValue = false;
            else return Fail(context, "bool default는 true|false여야 한다", outError);
            return true;
        }
        case ShaderPropertyType::Float4x4:
        {
            std::array<float, 16> value{};
            if (!ParseFloatArray(node, context, value, outError)) return false;
            outValue = value;
            return true;
        }
        case ShaderPropertyType::Texture2D:
        {
            if (!node.IsScalar())
                return Fail(context, "texture2d default는 asset GUID여야 한다", outError);
            const FileGuid guid(node.Scalar());
            if (guid == FileGuid{})
                return Fail(context, "texture2d default GUID가 nil이다", outError);
            outValue = guid;
            return true;
        }
        }
        return Fail(context, "지원하지 않는 property type", outError);
    }

    bool ParseProperties(const YAML::Node& node,
        std::vector<ShaderPropertyDesc>& outProperties, std::string& outError)
    {
        if (!node) return true;
        if (!node.IsSequence()) return Fail("properties", "sequence여야 한다", outError);
        if (node.size() > kMaxProperties)
            return Fail("properties", "상한 " + std::to_string(kMaxProperties) + "개 초과", outError);

        std::unordered_set<std::string> names;
        outProperties.reserve(node.size());
        for (std::size_t index = 0; index < node.size(); ++index)
        {
            const YAML::Node propertyNode = node[index];
            const std::string context = "properties[" + std::to_string(index) + "]";
            if (!ValidateMap(propertyNode, { "name", "label", "type", "default" },
                context, outError)) return false;

            ShaderPropertyDesc property;
            if (!ReadIdentifier(propertyNode, "name", context, property.name, outError))
                return false;
            if (!names.emplace(property.name).second)
                return Fail(context, "property 이름이 중복됐다: " + property.name, outError);

            if (const YAML::Node label = propertyNode["label"])
            {
                if (!label.IsScalar() || label.Scalar().empty()
                    || label.Scalar().size() > kMaxNameBytes)
                    return Fail(context, "label이 비었거나 너무 길다", outError);
                property.label = label.Scalar();
            }
            else property.label = property.name;

            std::string typeName;
            if (!ReadRequiredScalar(propertyNode, "type", context, typeName, outError))
                return false;
            if (!ParsePropertyType(typeName, property.type))
                return Fail(context, "지원하지 않는 property type: " + typeName, outError);
            if (!ParsePropertyDefault(propertyNode["default"], property.type,
                context + ".default", property.defaultValue, outError)) return false;
            outProperties.push_back(std::move(property));
        }
        return true;
    }

    bool ParseKeywords(const YAML::Node& node,
        std::vector<ShaderKeywordAxis>& outKeywords, std::string& outError)
    {
        if (!node) return true;
        if (!node.IsSequence()) return Fail("keywords", "sequence여야 한다", outError);
        if (node.size() > kMaxKeywordAxes)
            return Fail("keywords", "축 상한 " + std::to_string(kMaxKeywordAxes) + "개 초과", outError);

        std::unordered_set<std::string> axes;
        outKeywords.reserve(node.size());
        for (std::size_t index = 0; index < node.size(); ++index)
        {
            const YAML::Node keywordNode = node[index];
            const std::string context = "keywords[" + std::to_string(index) + "]";
            if (!ValidateMap(keywordNode, { "axis", "values" }, context, outError))
                return false;

            ShaderKeywordAxis axis;
            if (!ReadIdentifier(keywordNode, "axis", context, axis.name, outError))
                return false;
            if (!axes.emplace(axis.name).second)
                return Fail(context, "keyword 축이 중복됐다: " + axis.name, outError);

            const YAML::Node values = keywordNode["values"];
            if (!values || !values.IsSequence() || values.size() < 2
                || values.size() > kMaxKeywordValues)
            {
                return Fail(context, "values는 2~" + std::to_string(kMaxKeywordValues)
                    + "개 sequence여야 한다", outError);
            }
            std::unordered_set<std::string> uniqueValues;
            axis.values.reserve(values.size());
            for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex)
            {
                if (!values[valueIndex].IsScalar())
                    return Fail(context, "keyword value는 scalar여야 한다", outError);
                const std::string value = values[valueIndex].Scalar();
                if (!IsIdentifier(value))
                    return Fail(context, "keyword value가 식별자가 아니다: " + value, outError);
                if (!uniqueValues.emplace(value).second)
                    return Fail(context, "keyword value가 중복됐다: " + value, outError);
                axis.values.push_back(value);
            }
            outKeywords.push_back(std::move(axis));
        }
        return true;
    }

    bool ParseStage(const YAML::Node& node, std::string_view context,
        std::optional<ShaderStageEntry>& outStage, std::string& outError)
    {
        if (!node) return true;
        if (!ValidateMap(node, { "entry" }, context, outError)) return false;
        ShaderStageEntry stage;
        if (!ReadIdentifier(node, "entry", context, stage.entry, outError)) return false;
        outStage = std::move(stage);
        return true;
    }

    bool ParseRenderState(const YAML::Node& node, std::string_view context,
        ShaderRenderState& outState, std::string& outError)
    {
        if (!node) return true;
        if (!ValidateMap(node,
            { "fill", "cull", "blend", "depthWrite", "depthTest", "topology" },
            context, outError)) return false;

        if (const YAML::Node fill = node["fill"])
        {
            if (!fill.IsScalar()) return Fail(context, "fill은 scalar여야 한다", outError);
            if ("solid" == fill.Scalar()) outState.fillMode = RHIFillMode::Solid;
            else if ("wireframe" == fill.Scalar()) outState.fillMode = RHIFillMode::Wireframe;
            else return Fail(context, "fill은 solid|wireframe이어야 한다", outError);
        }
        if (const YAML::Node cull = node["cull"])
        {
            if (!cull.IsScalar()) return Fail(context, "cull은 scalar여야 한다", outError);
            if ("none" == cull.Scalar()) outState.cullMode = RHICullMode::None;
            else if ("back" == cull.Scalar()) outState.cullMode = RHICullMode::Back;
            else if ("front" == cull.Scalar()) outState.cullMode = RHICullMode::Front;
            else return Fail(context, "cull은 none|back|front여야 한다", outError);
        }
        if (const YAML::Node blend = node["blend"])
        {
            if (!blend.IsScalar()) return Fail(context, "blend는 scalar여야 한다", outError);
            if ("off" == blend.Scalar()) outState.blendMode = ShaderBlendMode::Off;
            else if ("alpha" == blend.Scalar()) outState.blendMode = ShaderBlendMode::Alpha;
            else if ("additive" == blend.Scalar()) outState.blendMode = ShaderBlendMode::Additive;
            else return Fail(context, "blend는 off|alpha|additive여야 한다", outError);
        }
        if (node["depthWrite"] && !ReadStrictBool(node, "depthWrite", context,
            outState.depthWrite, outError)) return false;
        if (const YAML::Node depthTest = node["depthTest"])
        {
            if (!depthTest.IsScalar())
                return Fail(context, "depthTest는 scalar여야 한다", outError);
            if ("off" == depthTest.Scalar()) outState.depthTest = RHICompareOp::None;
            else if ("less" == depthTest.Scalar()) outState.depthTest = RHICompareOp::Less;
            else if ("lessEqual" == depthTest.Scalar())
                outState.depthTest = RHICompareOp::LessEqual;
            else return Fail(context, "depthTest는 off|less|lessEqual이어야 한다", outError);
        }
        if (const YAML::Node topology = node["topology"])
        {
            if (!topology.IsScalar())
                return Fail(context, "topology는 scalar여야 한다", outError);
            if ("triangle" == topology.Scalar())
                outState.topologyType = RHITopologyType::Triangle;
            else if ("line" == topology.Scalar())
                outState.topologyType = RHITopologyType::Line;
            else if ("point" == topology.Scalar())
                outState.topologyType = RHITopologyType::Point;
            else return Fail(context, "topology는 triangle|line|point여야 한다", outError);
        }
        if (RHICompareOp::None == outState.depthTest && outState.depthWrite)
            return Fail(context, "depthTest off에서 depthWrite true일 수 없다", outError);
        return true;
    }

    bool ParseQueue(std::string_view value, ShaderPassQueue& outQueue)
    {
        if ("opaque" == value) outQueue = ShaderPassQueue::Opaque;
        else if ("transparent" == value) outQueue = ShaderPassQueue::Transparent;
        else if ("shadow" == value) outQueue = ShaderPassQueue::Shadow;
        else if ("compute" == value) outQueue = ShaderPassQueue::Compute;
        else return false;
        return true;
    }

    bool ParsePasses(const YAML::Node& node,
        std::vector<ShaderPassDesc>& outPasses, std::string& outError)
    {
        if (!node || !node.IsSequence() || node.size() == 0)
            return Fail("passes", "비어 있지 않은 sequence여야 한다", outError);
        if (node.size() > kMaxPasses)
            return Fail("passes", "상한 " + std::to_string(kMaxPasses) + "개 초과", outError);

        std::unordered_set<std::string> names;
        outPasses.reserve(node.size());
        for (std::size_t index = 0; index < node.size(); ++index)
        {
            const YAML::Node passNode = node[index];
            const std::string context = "passes[" + std::to_string(index) + "]";
            if (!ValidateMap(passNode,
                { "name", "vs", "ps", "cs", "state", "queue" }, context, outError))
                return false;

            ShaderPassDesc pass;
            if (!ReadIdentifier(passNode, "name", context, pass.name, outError))
                return false;
            if (!names.emplace(pass.name).second)
                return Fail(context, "pass 이름이 중복됐다: " + pass.name, outError);
            if (!ParseStage(passNode["vs"], context + ".vs", pass.vertex, outError)
                || !ParseStage(passNode["ps"], context + ".ps", pass.pixel, outError)
                || !ParseStage(passNode["cs"], context + ".cs", pass.compute, outError))
                return false;

            std::string queueName;
            if (!ReadRequiredScalar(passNode, "queue", context, queueName, outError))
                return false;
            if (!ParseQueue(queueName, pass.queue))
                return Fail(context, "queue는 opaque|transparent|shadow|compute여야 한다", outError);

            const bool compute = pass.compute.has_value();
            if (compute)
            {
                if (pass.vertex || pass.pixel)
                    return Fail(context, "compute pass에 graphics stage를 섞을 수 없다", outError);
                if (passNode["state"])
                    return Fail(context, "compute pass에는 graphics state가 없어야 한다", outError);
                if (ShaderPassQueue::Compute != pass.queue)
                    return Fail(context, "compute pass의 queue는 compute여야 한다", outError);
            }
            else
            {
                if (!pass.vertex)
                    return Fail(context, "graphics pass에는 vs가 필요하다", outError);
                if (ShaderPassQueue::Compute == pass.queue)
                    return Fail(context, "graphics pass의 queue가 compute다", outError);
                if (!ParseRenderState(passNode["state"], context + ".state",
                    pass.state, outError)) return false;
                if (ShaderPassQueue::Transparent == pass.queue
                    && ShaderBlendMode::Off == pass.state.blendMode)
                    return Fail(context, "transparent queue에는 blend가 필요하다", outError);
                if (ShaderPassQueue::Shadow == pass.queue
                    && ShaderBlendMode::Off != pass.state.blendMode)
                    return Fail(context, "shadow queue는 blend를 사용할 수 없다", outError);
            }
            outPasses.push_back(std::move(pass));
        }
        return true;
    }

    bool IsSafeRelativeSource(const std::filesystem::path& source)
    {
        if (source.empty() || source.is_absolute() || source.has_root_path()) return false;
        for (const std::filesystem::path& component : source)
        {
            if (component == "." || component == "..") return false;
        }
        const std::string extension = source.extension().string();
        return ".hlsl" == extension || ".slang" == extension;
    }
}

void ShaderRenderState::ApplyTo(RHIGraphicsPipelineDesc& desc) const
{
    desc.fillMode = fillMode;
    desc.cullMode = cullMode;
    desc.depthEnable = RHICompareOp::None != depthTest;
    desc.depthFunc = depthTest;
    desc.depthWriteMask = depthWrite ? RHIDepthWrite::All : RHIDepthWrite::Zero;
    desc.topologyType = topologyType;

    desc.blendEnable = ShaderBlendMode::Off != blendMode;
    desc.independentBlend = ShaderBlendMode::Additive == blendMode;
    desc.renderTargetBlend[0] = {};
    if (ShaderBlendMode::Additive == blendMode)
    {
        RHIRenderTargetBlend& blend = desc.renderTargetBlend[0];
        blend.enable = true;
        blend.srcColor = RHIBlendFactor::One;
        blend.dstColor = RHIBlendFactor::One;
        blend.srcAlpha = RHIBlendFactor::One;
        blend.dstAlpha = RHIBlendFactor::One;
    }
}

std::filesystem::path ShaderMeta::ResolveSource(
    const std::filesystem::path& metaPath) const
{
    return (metaPath.parent_path() / source).lexically_normal();
}

bool ShaderMetaLoader::LoadFile(const std::filesystem::path& path,
    const FileGuid& guid, ShaderMeta& outMeta, std::string& outError)
{
    if (".shadermeta" != path.extension().string())
        return Fail(path.string(), "확장자가 .shadermeta가 아니다", outError);

    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(path, error);
    if (error) return Fail(path.string(), "파일 크기를 읽지 못했다", outError);
    if (0 == bytes || bytes > kMaxMetaBytes)
        return Fail(path.string(), "파일이 비었거나 1MiB 상한을 넘었다", outError);

    std::ifstream file(path, std::ios::binary);
    if (!file) return Fail(path.string(), "파일을 열지 못했다", outError);
    std::ostringstream text;
    text << file.rdbuf();
    if (!file.eof() && file.fail())
        return Fail(path.string(), "파일을 끝까지 읽지 못했다", outError);
    return Parse(text.str(), path, guid, outMeta, outError);
}

bool ShaderMetaLoader::Parse(std::string_view text,
    const std::filesystem::path& originPath, const FileGuid& guid,
    ShaderMeta& outMeta, std::string& outError)
{
    try
    {
        if (text.empty() || text.size() > kMaxMetaBytes)
            return Fail(originPath.string(), "입력이 비었거나 1MiB 상한을 넘었다", outError);
        if (guid == FileGuid{})
            return Fail(originPath.string(), "asset GUID가 nil이다", outError);

        const YAML::Node root = YAML::Load(std::string(text));
        if (!ValidateMap(root,
            { "schema", "name", "source", "properties", "keywords", "passes" },
            originPath.string(), outError)) return false;

        const YAML::Node schemaNode = root["schema"];
        if (!schemaNode || !schemaNode.IsScalar())
            return Fail(originPath.string(), "필수 schema scalar가 없다", outError);
        const std::uint32_t schema = schemaNode.as<std::uint32_t>();
        if (ShaderMeta::kSchemaVersion != schema)
            return Fail(originPath.string(), "지원하지 않는 schema version "
                + std::to_string(schema), outError);

        ShaderMeta meta;
        meta.guid = guid;
        meta.schemaVersion = schema;
        if (!ReadIdentifier(root, "name", originPath.string(), meta.name, outError))
            return false;

        std::string sourceName;
        if (!ReadRequiredScalar(root, "source", originPath.string(), sourceName, outError))
            return false;
        if (sourceName.size() > kMaxSourceBytes)
            return Fail(originPath.string(), "source 경로가 너무 길다", outError);
        const std::filesystem::path authoredSource(sourceName);
        if (!IsSafeRelativeSource(authoredSource))
            return Fail(originPath.string(),
                "source는 상위 이동 없는 상대 .hlsl|.slang 경로여야 한다", outError);
        meta.source = authoredSource.lexically_normal();
        const std::filesystem::path resolved = meta.ResolveSource(originPath);
        std::error_code sourceError;
        if (!std::filesystem::is_regular_file(resolved, sourceError) || sourceError)
            return Fail(originPath.string(), "source 파일이 없다: " + resolved.string(), outError);

        if (!ParseProperties(root["properties"], meta.properties, outError)
            || !ParseKeywords(root["keywords"], meta.keywords, outError)
            || !ParsePasses(root["passes"], meta.passes, outError))
            return false;

        outMeta = std::move(meta);
        outError.clear();
        return true;
    }
    catch (const YAML::Exception& exception)
    {
        return Fail(originPath.string(),
            "YAML 해석 실패: " + std::string(exception.what()), outError);
    }
    catch (const std::exception& exception)
    {
        return Fail(originPath.string(),
            "ShaderMeta 검증 실패: " + std::string(exception.what()), outError);
    }
}
