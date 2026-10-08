#include "ShaderMeta.h"

#include "AuthoringParsedDocument.h"
#include "AuthoringCookedDocument.h"
#include "Sha256.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <regex>
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

    bool ValidateMap(const Authoring::ReadNode& node,
        std::initializer_list<std::string_view> allowed,
        std::string_view context, std::string& outError)
    {
        if (!node || !node.IsMap()) return Fail(context, "map이어야 한다", outError);
        // ★ 키를 노드로 다루지 않는다. ryml에서 맵의 키는 **자식 노드의 속성**이지
        //   별도 노드가 아니다 — 어댑터의 `MapEntry`가 그 비대칭을 흡수한다.
        for (const Authoring::MapEntry entry : node.Map())
        {
            if (!entry.key.IsScalar())
                return Fail(context, "field 이름은 scalar여야 한다", outError);
            const std::string key = entry.key.AsString();
            const bool known = std::any_of(allowed.begin(), allowed.end(),
                [&key](std::string_view candidate) { return key == candidate; });
            if (!known) return Fail(context, "알 수 없는 field '" + key + "'", outError);
        }
        return true;
    }

    bool ReadRequiredScalar(const Authoring::ReadNode& node, const char* key,
        std::string_view context, std::string& outValue, std::string& outError)
    {
        const Authoring::ReadNode value = node[key];
        if (!value || !value.IsScalar())
            return Fail(context, std::string("필수 scalar '") + key + "'가 없다", outError);
        outValue = value.AsString();
        if (outValue.empty())
            return Fail(context, std::string("'") + key + "'가 비었다", outError);
        return true;
    }

    bool ReadIdentifier(const Authoring::ReadNode& node, const char* key,
        std::string_view context, std::string& outValue, std::string& outError)
    {
        if (!ReadRequiredScalar(node, key, context, outValue, outError)) return false;
        if (!IsIdentifier(outValue))
            return Fail(context, std::string("'") + key + "'가 식별자가 아니다", outError);
        return true;
    }

    bool ReadStrictBool(const Authoring::ReadNode& node, const char* key,
        std::string_view context, bool& outValue, std::string& outError)
    {
        const Authoring::ReadNode value = node[key];
        if (!value || !value.IsScalar())
            return Fail(context, std::string("'") + key + "'는 bool scalar여야 한다", outError);
        const std::string scalar = value.AsString();
        if ("true" == scalar) outValue = true;
        else if ("false" == scalar) outValue = false;
        else return Fail(context, std::string("'") + key + "'는 true|false여야 한다", outError);
        return true;
    }

    template<std::size_t Size>
    bool ParseFloatArray(const Authoring::ReadNode& node, std::string_view context,
        std::array<float, Size>& outValue, std::string& outError)
    {
        if (!node || !node.IsSequence() || node.Size() != Size)
            return Fail(context, std::to_string(Size) + "개 float sequence여야 한다", outError);
        for (std::size_t index = 0; index < Size; ++index)
        {
            if (!node.At(index).IsScalar())
                return Fail(context, "배열 원소는 float scalar여야 한다", outError);
            const float value = node.At(index).As<float>();
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

    bool ParsePropertyDefault(const Authoring::ReadNode& node, ShaderPropertyType type,
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
            const float value = node.As<float>();
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
            outValue = node.As<std::int32_t>();
            return true;
        case ShaderPropertyType::Bool:
        {
            if (!node.IsScalar()) return Fail(context, "bool default가 아니다", outError);
            const std::string value = node.AsString();
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
            const FileGuid guid{ node.AsString() };
            if (guid == FileGuid{})
                return Fail(context, "texture2d default GUID가 nil이다", outError);
            outValue = guid;
            return true;
        }
        }
        return Fail(context, "지원하지 않는 property type", outError);
    }

    bool ParseProperties(const Authoring::ReadNode& node,
        std::vector<ShaderPropertyDesc>& outProperties, std::string& outError)
    {
        if (!node) return true;
        if (!node.IsSequence()) return Fail("properties", "sequence여야 한다", outError);
        if (node.Size() > kMaxProperties)
            return Fail("properties", "상한 " + std::to_string(kMaxProperties) + "개 초과", outError);

        std::unordered_set<std::string> names;
        outProperties.reserve(node.Size());
        for (std::size_t index = 0; index < node.Size(); ++index)
        {
            const Authoring::ReadNode propertyNode = node.At(index);
            const std::string context = "properties[" + std::to_string(index) + "]";
            if (!ValidateMap(propertyNode, { "name", "label", "type", "default",
                "parameterId", "semantic", "colorSpace", "exposed" },
                context, outError)) return false;

            ShaderPropertyDesc property;
            if (!ReadIdentifier(propertyNode, "name", context, property.name, outError))
                return false;
            if (!names.emplace(property.name).second)
                return Fail(context, "property 이름이 중복됐다: " + property.name, outError);

            if (const Authoring::ReadNode label = propertyNode["label"])
            {
                if (!label.IsScalar() || label.Scalar().empty()
                    || label.Scalar().size() > kMaxNameBytes)
                    return Fail(context, "label이 비었거나 너무 길다", outError);
                property.label = label.AsString();
            }
            else property.label = property.name;

            std::string typeName;
            if (!ReadRequiredScalar(propertyNode, "type", context, typeName, outError))
                return false;
            if (!ParsePropertyType(typeName, property.type))
                return Fail(context, "지원하지 않는 property type: " + typeName, outError);
            if (!ParsePropertyDefault(propertyNode["default"], property.type,
                context + ".default", property.defaultValue, outError)) return false;
            if (const auto id = propertyNode["parameterId"])
            {
                if (!id.IsScalar()) return Fail(context, "parameterId must be scalar", outError);
                property.parameterId = id.As<std::uint64_t>();
            }
            if (propertyNode["exposed"] && !ReadStrictBool(propertyNode, "exposed", context,
                property.exposed, outError)) return false;
            if (propertyNode["semantic"] && !ReadRequiredScalar(propertyNode, "semantic", context,
                property.semantic, outError)) return false;
            if (propertyNode["colorSpace"] && !ReadRequiredScalar(propertyNode, "colorSpace", context,
                property.colorSpace, outError)) return false;
            if (property.semantic != "value" && property.semantic != "vector" &&
                property.semantic != "color" && property.semantic != "normal" && property.semantic != "texture")
                return Fail(context, "unknown property semantic", outError);
            if ((property.semantic == "color" && property.type != ShaderPropertyType::Float4) ||
                ((property.semantic == "normal" || property.semantic == "vector") &&
                    property.type != ShaderPropertyType::Float3) ||
                (property.semantic == "texture" && property.type != ShaderPropertyType::Texture2D))
                return Fail(context, "property semantic/type mismatch", outError);
            if (property.colorSpace != "data" && property.colorSpace != "linear" && property.colorSpace != "srgb")
                return Fail(context, "unknown property colorSpace", outError);
            if (property.colorSpace != "data" && property.semantic != "color" && property.semantic != "texture")
                return Fail(context, "colorSpace requires a color or texture semantic", outError);
            outProperties.push_back(std::move(property));
        }
        return true;
    }

    bool ParseKeywords(const Authoring::ReadNode& node,
        std::vector<ShaderKeywordAxis>& outKeywords, std::string& outError)
    {
        if (!node) return true;
        if (!node.IsSequence()) return Fail("keywords", "sequence여야 한다", outError);
        if (node.Size() > kMaxKeywordAxes)
            return Fail("keywords", "축 상한 " + std::to_string(kMaxKeywordAxes) + "개 초과", outError);

        std::unordered_set<std::string> axes;
        outKeywords.reserve(node.Size());
        for (std::size_t index = 0; index < node.Size(); ++index)
        {
            const Authoring::ReadNode keywordNode = node.At(index);
            const std::string context = "keywords[" + std::to_string(index) + "]";
            if (!ValidateMap(keywordNode, { "axis", "values" }, context, outError))
                return false;

            ShaderKeywordAxis axis;
            if (!ReadIdentifier(keywordNode, "axis", context, axis.name, outError))
                return false;
            if (!axes.emplace(axis.name).second)
                return Fail(context, "keyword 축이 중복됐다: " + axis.name, outError);

            const Authoring::ReadNode values = keywordNode["values"];
            if (!values || !values.IsSequence() || values.Size() < 2
                || values.Size() > kMaxKeywordValues)
            {
                return Fail(context, "values는 2~" + std::to_string(kMaxKeywordValues)
                    + "개 sequence여야 한다", outError);
            }
            std::unordered_set<std::string> uniqueValues;
            axis.values.reserve(values.Size());
            for (std::size_t valueIndex = 0; valueIndex < values.Size(); ++valueIndex)
            {
                if (!values.At(valueIndex).IsScalar())
                    return Fail(context, "keyword value는 scalar여야 한다", outError);
                const std::string value = values.At(valueIndex).AsString();
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

    bool ParseStage(const Authoring::ReadNode& node, std::string_view context,
        std::optional<ShaderStageEntry>& outStage, std::string& outError)
    {
        if (!node) return true;
        if (!ValidateMap(node, { "entry" }, context, outError)) return false;
        ShaderStageEntry stage;
        if (!ReadIdentifier(node, "entry", context, stage.entry, outError)) return false;
        outStage = std::move(stage);
        return true;
    }

    bool ParseRenderState(const Authoring::ReadNode& node, std::string_view context,
        ShaderRenderState& outState, std::string& outError)
    {
        if (!node) return true;
        if (!ValidateMap(node,
            { "fill", "cull", "blend", "depthWrite", "depthTest", "topology" },
            context, outError)) return false;

        if (const Authoring::ReadNode fill = node["fill"])
        {
            if (!fill.IsScalar()) return Fail(context, "fill은 scalar여야 한다", outError);
            if ("solid" == fill.Scalar()) outState.fillMode = RHIFillMode::Solid;
            else if ("wireframe" == fill.Scalar()) outState.fillMode = RHIFillMode::Wireframe;
            else return Fail(context, "fill은 solid|wireframe이어야 한다", outError);
        }
        if (const Authoring::ReadNode cull = node["cull"])
        {
            if (!cull.IsScalar()) return Fail(context, "cull은 scalar여야 한다", outError);
            if ("none" == cull.Scalar()) outState.cullMode = RHICullMode::None;
            else if ("back" == cull.Scalar()) outState.cullMode = RHICullMode::Back;
            else if ("front" == cull.Scalar()) outState.cullMode = RHICullMode::Front;
            else return Fail(context, "cull은 none|back|front여야 한다", outError);
        }
        if (const Authoring::ReadNode blend = node["blend"])
        {
            if (!blend.IsScalar()) return Fail(context, "blend는 scalar여야 한다", outError);
            if ("off" == blend.Scalar()) outState.blendMode = ShaderBlendMode::Off;
            else if ("alpha" == blend.Scalar()) outState.blendMode = ShaderBlendMode::Alpha;
            else if ("additive" == blend.Scalar()) outState.blendMode = ShaderBlendMode::Additive;
            else return Fail(context, "blend는 off|alpha|additive여야 한다", outError);
        }
        if (node["depthWrite"] && !ReadStrictBool(node, "depthWrite", context,
            outState.depthWrite, outError)) return false;
        if (const Authoring::ReadNode depthTest = node["depthTest"])
        {
            if (!depthTest.IsScalar())
                return Fail(context, "depthTest는 scalar여야 한다", outError);
            if ("off" == depthTest.Scalar()) outState.depthTest = RHICompareOp::None;
            else if ("less" == depthTest.Scalar()) outState.depthTest = RHICompareOp::Less;
            else if ("lessEqual" == depthTest.Scalar())
                outState.depthTest = RHICompareOp::LessEqual;
            else return Fail(context, "depthTest는 off|less|lessEqual이어야 한다", outError);
        }
        if (const Authoring::ReadNode topology = node["topology"])
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

    bool ParsePasses(const Authoring::ReadNode& node,
        std::vector<ShaderPassDesc>& outPasses, std::string& outError)
    {
        if (!node || !node.IsSequence() || node.Size() == 0)
            return Fail("passes", "비어 있지 않은 sequence여야 한다", outError);
        if (node.Size() > kMaxPasses)
            return Fail("passes", "상한 " + std::to_string(kMaxPasses) + "개 초과", outError);

        std::unordered_set<std::string> names;
        outPasses.reserve(node.Size());
        for (std::size_t index = 0; index < node.Size(); ++index)
        {
            const Authoring::ReadNode passNode = node.At(index);
            const std::string context = "passes[" + std::to_string(index) + "]";
            if (!ValidateMap(passNode,
                { "name", "vs", "ps", "cs", "state", "queue", "geometryVisibility" }, context, outError))
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

            if (passNode["geometryVisibility"])
            {
                std::string contract;
                if (!ReadRequiredScalar(passNode, "geometryVisibility", context, contract, outError))
                {
                    return false;
                }
                if (contract == "indexed-instance-v1")
                {
                    pass.geometryVisibility = ShaderGeometryVisibility::IndexedInstanceV1;
                }
                else if (contract != "direct")
                {
                    return Fail(context, "unknown geometryVisibility contract", outError);
                }
                if (pass.geometryVisibility != ShaderGeometryVisibility::Direct &&
                    (pass.name != "GBuffer" || pass.queue != ShaderPassQueue::Opaque ||
                     !pass.vertex || !pass.pixel || pass.compute))
                {
                    return Fail(context, "indexed-instance-v1 requires an opaque GBuffer VS+PS pass", outError);
                }
            }

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

    bool ParseGeneratedMaterial(const Authoring::ReadNode& node,
        const std::filesystem::path& source, ShaderMeta& meta, std::string& outError,
        std::optional<std::string_view> sourceBytes = {})
    {
        if (!node) return true;
        constexpr std::string_view context = "generatedMaterial";
        if (!ValidateMap(node, { "graph", "adapter", "generation", "sourceSha256",
            "features", "surface", "volume", "samplers" }, context, outError)) return false;
        ShaderGeneratedMaterial generated;
        std::string graph;
        if (!ReadRequiredScalar(node, "graph", context, graph, outError) ||
            !ReadRequiredScalar(node, "generation", context, generated.generation, outError) ||
            !ReadRequiredScalar(node, "sourceSha256", context, generated.sourceSha256, outError) ||
            !ReadStrictBool(node, "surface", context, generated.surface, outError) ||
            !ReadStrictBool(node, "volume", context, generated.volume, outError)) return false;
        generated.graphGuid = FileGuid{graph};
        const auto adapter = node["adapter"], features = node["features"];
        if (!adapter || !adapter.IsScalar() || !features || !features.IsScalar())
            return Fail(context, "adapter/features must be scalars", outError);
        generated.adapterVersion = adapter.As<std::uint32_t>();
        generated.features = features.As<std::uint32_t>();
        const auto digest = [](std::string_view value) {
            return value.size() == 64 && std::ranges::all_of(value, [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
        };
        if (generated.graphGuid == FileGuid{} || generated.graphGuid != meta.guid ||
            generated.adapterVersion != ShaderGeneratedMaterial::kAdapterVersion ||
            !digest(generated.generation) || !digest(generated.sourceSha256) ||
            (generated.features & ~0x3FFFu) != 0 || (!generated.surface && !generated.volume) ||
            generated.volume != ((generated.features & 0x2000u) != 0))
            return Fail(context, "invalid generated material identity/features", outError);
        const auto samplers = node["samplers"];
        if (!samplers || !samplers.IsSequence() || samplers.Size() > 64)
            return Fail(context, "samplers must be a bounded sequence", outError);
        std::unordered_set<std::string> names;
        for (const auto& property : meta.properties) names.insert(property.name);
        for (std::size_t index = 0; index < samplers.Size(); ++index)
        {
            const auto entry = samplers.At(index);
            ShaderMaterialSampler sampler;
            if (!ValidateMap(entry, { "name", "description", "parameterId", "exposed" }, context, outError) ||
                !ReadIdentifier(entry, "name", context, sampler.name, outError) ||
                !ReadRequiredScalar(entry, "description", context, sampler.description, outError) ||
                !ReadStrictBool(entry, "exposed", context, sampler.exposed, outError)) return false;
            const auto id = entry["parameterId"];
            if (!id || !id.IsScalar()) return Fail(context, "sampler parameterId missing", outError);
            sampler.parameterId = id.As<std::uint64_t>();
            if (!names.insert(sampler.name).second || !std::regex_match(sampler.description,
                std::regex("(linear|nearest)-(repeat|clamp)|(linear|nearest)-(linear|nearest)-"
                           "(repeat|clamp|mirror)-(repeat|clamp|mirror)")))
                return Fail(context, "duplicate or unsupported sampler", outError);
            generated.samplers.push_back(std::move(sampler));
        }
        std::string fileText;
        if (!sourceBytes)
        {
            std::error_code error;
            const auto bytes = std::filesystem::file_size(source, error);
            if (error || bytes == 0 || bytes > 16u * 1024u * 1024u)
                return Fail(context, "generated source is missing or oversized", outError);
            std::ifstream stream(source, std::ios::binary);
            fileText.assign(std::istreambuf_iterator<char>(stream), {});
            if (!stream) return Fail(context, "generated source read failed", outError);
            sourceBytes = fileText;
        }
        const auto text = *sourceBytes;
        if (text.empty() || text.size() > 16u * 1024u * 1024u ||
            Hash::ToHex(Hash::Sha256::Compute(text.data(), text.size())) != generated.sourceSha256)
            return Fail(context, "generated source SHA-256 mismatch", outError);
        meta.generatedMaterial = std::move(generated);
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
    return LoadFile(path, path, guid, outMeta, outError);
}

bool ShaderMetaLoader::LoadFile(const std::filesystem::path& documentPath,
    const std::filesystem::path& sourceOriginPath, const FileGuid& guid,
    ShaderMeta& outMeta, std::string& outError,
    std::array<std::uint8_t, 32>* outDocumentDigest)
{
    if (".shadermeta" != documentPath.extension().string())
        return Fail(documentPath.string(), "확장자가 .shadermeta가 아니다", outError);
    if (".shadermeta" != sourceOriginPath.extension().string())
        return Fail(sourceOriginPath.string(),
            "source origin 확장자가 .shadermeta가 아니다", outError);

    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(documentPath, error);
    if (error) return Fail(documentPath.string(), "파일 크기를 읽지 못했다", outError);
    if (0 == bytes || bytes > kMaxMetaBytes)
        return Fail(documentPath.string(), "파일이 비었거나 1MiB 상한을 넘었다", outError);

    std::string documentError;
    const Authoring::ParsedDocument document =
        Authoring::ParsedDocument::ParseFile(documentPath.string(), documentError);
    if (!document)
        return Fail(documentPath.string(), "문서 해석 실패: " + documentError, outError);
    ShaderMeta candidate;
    if (!ParseDocument(document.Root(), sourceOriginPath, guid, candidate, outError))
    {
        return false;
    }
    if (outDocumentDigest)
    {
        std::vector<std::byte> canonical;
        if (!Authoring::EncodeCookedDocument(document.Root(), canonical, outError))
        {
            return false;
        }
        Hash::Sha256 hash;
        const auto origin = candidate.originPath.generic_u8string();
        const auto documentDigest = Hash::Sha256::Compute(canonical.data(), canonical.size());
        hash.Update(documentDigest.data(), documentDigest.size());
        hash.Update(origin.data(), origin.size());
        *outDocumentDigest = hash.Finish();
    }
    outMeta = std::move(candidate);
    return true;
}

bool ShaderMetaLoader::Parse(std::string_view text,
    const std::filesystem::path& originPath, const FileGuid& guid,
    ShaderMeta& outMeta, std::string& outError)
{
    if (text.empty() || text.size() > kMaxMetaBytes)
        return Fail(originPath.string(), "입력이 비었거나 1MiB 상한을 넘었다", outError);

    std::string parseError;
    const Authoring::ParsedDocument document =
        Authoring::ParsedDocument::ParseText(std::string(text), parseError);
    if (!document)
        return Fail(originPath.string(), "YAML 해석 실패: " + parseError, outError);
    return ParseDocument(document.Root(), originPath, guid, outMeta, outError);
}

static bool ParseShaderMetaDocument(const Authoring::ReadNode& root,
    const std::filesystem::path& originPath, const FileGuid& guid,
    ShaderMeta& outMeta, std::string& outError, std::optional<std::string_view> sourceBytes,
    bool cookedMetadata = false)
{
    try
    {
        if (guid == FileGuid{})
            return Fail(originPath.string(), "asset GUID가 nil이다", outError);
        if (!ValidateMap(root,
            { "schema", "name", "source", "properties", "keywords", "passes", "generatedMaterial" },
            originPath.string(), outError)) return false;

        const Authoring::ReadNode schemaNode = root["schema"];
        if (!schemaNode || !schemaNode.IsScalar())
            return Fail(originPath.string(), "필수 schema scalar가 없다", outError);
        const std::uint32_t schema = schemaNode.As<std::uint32_t>();
        if (ShaderMeta::kSchemaVersion != schema)
            return Fail(originPath.string(), "지원하지 않는 schema version "
                + std::to_string(schema), outError);

        ShaderMeta meta;
        meta.guid = guid;
        meta.schemaVersion = schema;
        meta.originPath = originPath.lexically_normal();
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
        if (!cookedMetadata && !sourceBytes &&
            (!std::filesystem::is_regular_file(resolved, sourceError) || sourceError))
        {
            return Fail(originPath.string(), "source 파일이 없다: " + resolved.string(), outError);
        }

        if (cookedMetadata && root["generatedMaterial"])
        {
            return Fail(originPath.string(),
                "generated metadata requires its verified cooked material program", outError);
        }

        if (!ParseProperties(root["properties"], meta.properties, outError)
            || !ParseKeywords(root["keywords"], meta.keywords, outError)
            || !ParsePasses(root["passes"], meta.passes, outError)
            || !ParseGeneratedMaterial(root["generatedMaterial"], resolved, meta, outError, sourceBytes))
            return false;

        if (sourceBytes && !meta.generatedMaterial)
            return Fail(originPath.string(), "cooked source bytes require generated material metadata", outError);

        outMeta = std::move(meta);
        outError.clear();
        return true;
    }
    // ★ `YAML::Exception` 전용 catch는 사라졌다. ryml은 예외가 아니라 **abort**가
    //   기본값이라 잡을 것이 없고(D3-b-1의 에러 정책이 그것을 예외로 바꾼다),
    //   파싱 실패는 위에서 `ParsedDocument`가 값으로 돌려준다. 아래 catch는
    //   변환 실패(`As<T>()`)처럼 검증 도중 던지는 것들을 계속 받는다.
    catch (const std::exception& exception)
    {
        return Fail(originPath.string(),
            "ShaderMeta 검증 실패: " + std::string(exception.what()), outError);
    }
}

bool ShaderMetaLoader::ParseDocument(const Authoring::ReadNode& root,
    const std::filesystem::path& originPath, const FileGuid& guid,
    ShaderMeta& outMeta, std::string& outError)
{
    return ParseShaderMetaDocument(root, originPath, guid, outMeta, outError, {});
}

bool ShaderMetaLoader::ParseCookedMetadata(std::span<const std::byte> bytes,
    const FileGuid& guid, ShaderMeta& outMeta, std::string& outError)
{
    if (bytes.empty() || bytes.size() > kMaxMetaBytes)
    {
        return Fail("cooked shader metadata", "metadata is empty or exceeds 1 MiB", outError);
    }
    const auto document = Authoring::ParsedDocument::ParseCooked(bytes, outError);
    if (!document)
    {
        return false;
    }
    // No physical source origin is invented for an immutable content blob.
    return ParseShaderMetaDocument(document.Root(), {}, guid, outMeta, outError, {}, true);
}

bool ShaderMetaLoader::ParseGenerated(std::string_view text, std::string_view source,
    const FileGuid& guid, ShaderMeta& outMeta, std::string& outError)
{
    if (text.empty() || text.size() > kMaxMetaBytes)
        return Fail("generated material", "metadata is empty or oversized", outError);
    const auto document = Authoring::ParsedDocument::ParseText(std::string(text), outError);
    if (!document) return false;
    return ParseShaderMetaDocument(document.Root(), "material.shadermeta", guid, outMeta, outError, source);
}

bool ShaderMetaLoader::ParseGeneratedCooked(std::span<const std::byte> bytes, std::string_view source,
    const FileGuid& guid, ShaderMeta& outMeta, std::string& outError)
{
    if (bytes.empty() || bytes.size() > kMaxMetaBytes)
        return Fail("generated material", "cooked metadata is empty or oversized", outError);
    const auto document = Authoring::ParsedDocument::ParseCooked(bytes, outError);
    if (!document) return false;
    return ParseShaderMetaDocument(document.Root(), "material.shadermeta", guid, outMeta, outError, source);
}
