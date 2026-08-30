#include "MaterialAuthoringCodec.h"

#include "AssetIdentity.h"

#include <yaml-cpp/yaml.h>

#include <array>
#include <cstdint>

namespace experiment
{
    namespace
    {
        inline constexpr const char* kNilGuidText =
            "00000000-0000-0000-0000-000000000000";

        [[nodiscard]] std::string GuidText(const AssetId& id)
        {
            return id.IsValid() ? Uuid::ToString(id.value)
                : std::string(kNilGuidText);
        }

        [[nodiscard]] bool ParseGuid(const YAML::Node& node, bool allowNil,
            const char* context, AssetId& out, std::string& outError)
        {
            if (!node || !node.IsScalar())
            {
                outError = std::string(context) + " GUID가 스칼라가 아니다";
                return false;
            }
            const std::string text = node.as<std::string>();
            if (allowNil && text == kNilGuidText)
            {
                out = {};
                return true;
            }
            if (!TryParseCanonicalAssetId(text, out))
            {
                outError = std::string(context)
                    + " GUID가 canonical UUIDv4가 아니다: " + text;
                return false;
            }
            return true;
        }

        template <std::size_t Count>
        [[nodiscard]] bool ParseFloats(const YAML::Node& node,
            const char* context, std::array<float, Count>& out,
            std::string& outError)
        {
            if (!node.IsSequence() || node.size() != Count)
            {
                outError = std::string(context) + " 값은 float "
                    + std::to_string(Count) + "개 시퀀스여야 한다";
                return false;
            }
            for (std::size_t index = 0; index < Count; ++index)
            {
                if (!node[index].IsScalar())
                {
                    outError = std::string(context) + " 값에 비스칼라가 있다";
                    return false;
                }
                out[index] = node[index].as<float>();
            }
            return true;
        }

        [[nodiscard]] YAML::Node MakeFloats(std::initializer_list<float> values)
        {
            YAML::Node node(YAML::NodeType::Sequence);
            node.SetStyle(YAML::EmitterStyle::Flow);
            for (const float value : values) node.push_back(value);
            return node;
        }

        [[nodiscard]] bool SerializeValue(const MaterialProperty& property,
            YAML::Node& outEntry, std::string& outError)
        {
            if (const auto* value = std::get_if<bool>(&property.value))
            {
                outEntry["bool"] = *value;
            }
            else if (const auto* value = std::get_if<std::int32_t>(&property.value))
            {
                outEntry["int"] = *value;
            }
            else if (const auto* value =
                std::get_if<std::uint32_t>(&property.value))
            {
                outEntry["uint"] = *value;
            }
            else if (const auto* value = std::get_if<float>(&property.value))
            {
                outEntry["float"] = *value;
            }
            else if (const auto* value =
                std::get_if<math::vector2>(&property.value))
            {
                outEntry["float2"] = MakeFloats({ value->x, value->y });
            }
            else if (const auto* value =
                std::get_if<math::vector3>(&property.value))
            {
                outEntry["float3"] = MakeFloats({ value->x, value->y, value->z });
            }
            else if (const auto* value =
                std::get_if<math::vector4>(&property.value))
            {
                outEntry["float4"] = MakeFloats(
                    { value->x, value->y, value->z, value->w });
            }
            else if (const auto* value =
                std::get_if<std::string>(&property.value))
            {
                outEntry["string"] = *value;
            }
            else if (const auto* value =
                std::get_if<TextureReference>(&property.value))
            {
                YAML::Node texture(YAML::NodeType::Map);
                texture.SetStyle(YAML::EmitterStyle::Flow);
                texture["guid"] = GuidText(value->assetId);
                texture["colorSpace"] =
                    TextureColorSpace::Srgb == value->colorSpace
                    ? "srgb" : "linear";
                outEntry["texture"] = texture;
            }
            else
            {
                outError = "직렬화할 수 없는 property 값이다: " + property.name;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool DeserializeValue(const YAML::Node& entry,
            const std::string& name, MaterialPropertyValue& outValue,
            std::string& outError)
        {
            std::size_t valueKeys = 0;
            for (const auto& pair : entry)
            {
                const std::string key = pair.first.as<std::string>();
                if (key == "name") continue;
                ++valueKeys;
                const YAML::Node& value = pair.second;
                if (key == "bool" || key == "int" || key == "uint"
                    || key == "float" || key == "string")
                {
                    if (!value.IsScalar())
                    {
                        outError = "property 값이 스칼라가 아니다: " + name;
                        return false;
                    }
                    if (key == "bool") outValue = value.as<bool>();
                    else if (key == "int") outValue = value.as<std::int32_t>();
                    else if (key == "uint") outValue = value.as<std::uint32_t>();
                    else if (key == "float") outValue = value.as<float>();
                    else outValue = value.as<std::string>();
                }
                else if (key == "float2")
                {
                    std::array<float, 2> floats{};
                    if (!ParseFloats(value, name.c_str(), floats, outError))
                        return false;
                    outValue = math::vector2{ floats[0], floats[1] };
                }
                else if (key == "float3")
                {
                    std::array<float, 3> floats{};
                    if (!ParseFloats(value, name.c_str(), floats, outError))
                        return false;
                    outValue = math::vector3{ floats[0], floats[1], floats[2] };
                }
                else if (key == "float4")
                {
                    std::array<float, 4> floats{};
                    if (!ParseFloats(value, name.c_str(), floats, outError))
                        return false;
                    outValue = math::vector4{ floats[0], floats[1], floats[2],
                        floats[3] };
                }

                else if (key == "texture")
                {
                    if (!value.IsMap())
                    {
                        outError = "texture 값은 매핑이어야 한다: " + name;
                        return false;
                    }
                    TextureReference reference;
                    if (!ParseGuid(value["guid"], true,
                        ("property " + name + " texture").c_str(),
                        reference.assetId, outError))
                    {
                        return false;
                    }
                    const YAML::Node colorSpace = value["colorSpace"];
                    if (!colorSpace || !colorSpace.IsScalar())
                    {
                        outError = "texture colorSpace가 없다: " + name;
                        return false;
                    }
                    const std::string colorSpaceText =
                        colorSpace.as<std::string>();
                    if (colorSpaceText == "srgb")
                        reference.colorSpace = TextureColorSpace::Srgb;
                    else if (colorSpaceText == "linear")
                        reference.colorSpace = TextureColorSpace::Linear;
                    else
                    {
                        outError = "미지의 colorSpace다: " + colorSpaceText;
                        return false;
                    }
                    for (const auto& texturePair : value)
                    {
                        const std::string textureKey =
                            texturePair.first.as<std::string>();
                        if (textureKey != "guid" && textureKey != "colorSpace")
                        {
                            outError = "texture의 미지 키다: " + textureKey;
                            return false;
                        }
                    }
                    outValue = std::move(reference);
                }
                else
                {
                    outError = "미지의 property 값 키다: " + key + " ("
                        + name + ")";
                    return false;
                }
            }
            if (1u != valueKeys)
            {
                if (outError.empty())
                {
                    outError = "property 값 키는 정확히 하나여야 한다("
                        + std::to_string(valueKeys) + "개): " + name;
                }
                return false;
            }
            return true;
        }
    }

    bool SerializeMaterialAuthoring(const Material& material, YAML::Node& outNode,
        std::string& outError)
    {
        if (!material.shaderAssetId.IsValid()
            || !IsAssetIdV4(material.shaderAssetId))
        {
            outError = "shaderAssetId가 canonical UUIDv4가 아니다: "
                + material.name;
            return false;
        }

        YAML::Node node(YAML::NodeType::Map);
        node["schema"] = kMaterialAuthoringSchemaVersion;
        node["assetId"] = GuidText(material.assetId);
        node["shaderAssetId"] = GuidText(material.shaderAssetId);
        node["name"] = material.name;
        node["blendMode"] =
            MaterialBlendMode::Transparent == material.blendMode
            ? "transparent" : "opaque";

        YAML::Node properties(YAML::NodeType::Sequence);
        for (const MaterialProperty& property : material.properties)
        {
            if (property.name.empty())
            {
                outError = "빈 property 이름이 있다: " + material.name;
                return false;
            }
            YAML::Node entry(YAML::NodeType::Map);
            entry["name"] = property.name;
            if (!SerializeValue(property, entry, outError)) return false;
            properties.push_back(entry);
        }
        node["properties"] = properties;

        YAML::Node keywords(YAML::NodeType::Sequence);
        keywords.SetStyle(YAML::EmitterStyle::Flow);
        for (const std::string& keyword : material.keywords)
        {
            if (keyword.empty())
            {
                outError = "빈 keyword 문자열이 있다: " + material.name;
                return false;
            }
            keywords.push_back(keyword);
        }
        node["keywords"] = keywords;

        YAML::Node selections(YAML::NodeType::Sequence);
        selections.SetStyle(YAML::EmitterStyle::Flow);
        for (const std::uint16_t selection : material.keywordSelections)
            selections.push_back(static_cast<std::uint32_t>(selection));
        node["keywordSelections"] = selections;

        outNode = node;
        outError.clear();
        return true;
    }

    bool DeserializeMaterialAuthoring(const YAML::Node& node,
        Material& outMaterial, std::string& outError)
    {
        if (!node || !node.IsMap())
        {
            outError = "material 저작 문서가 매핑이 아니다";
            return false;
        }
        const YAML::Node schema = node["schema"];
        if (!schema || !schema.IsScalar()
            || schema.as<std::uint32_t>() != kMaterialAuthoringSchemaVersion)
        {
            outError = "material 저작 schema 버전이 다르다";
            return false;
        }

        Material material;
        if (!ParseGuid(node["assetId"], true, "assetId", material.assetId,
            outError))
        {
            return false;
        }
        if (!ParseGuid(node["shaderAssetId"], false, "shaderAssetId",
            material.shaderAssetId, outError))
        {
            return false;
        }
        const YAML::Node name = node["name"];
        if (!name || !name.IsScalar())
        {
            outError = "name이 없다";
            return false;
        }
        material.name = name.as<std::string>();

        const YAML::Node blendMode = node["blendMode"];
        if (!blendMode || !blendMode.IsScalar())
        {
            outError = "blendMode가 없다";
            return false;
        }
        const std::string blendModeText = blendMode.as<std::string>();
        if (blendModeText == "opaque")
            material.blendMode = MaterialBlendMode::Opaque;
        else if (blendModeText == "transparent")
            material.blendMode = MaterialBlendMode::Transparent;
        else
        {
            outError = "미지의 blendMode다: " + blendModeText;
            return false;
        }

        const YAML::Node properties = node["properties"];
        if (!properties || !properties.IsSequence())
        {
            outError = "properties가 시퀀스가 아니다";
            return false;
        }
        for (const YAML::Node& entry : properties)
        {
            if (!entry.IsMap())
            {
                outError = "property 항목이 매핑이 아니다";
                return false;
            }
            const YAML::Node entryName = entry["name"];
            if (!entryName || !entryName.IsScalar())
            {
                outError = "property name이 없다";
                return false;
            }
            MaterialProperty property;
            property.name = entryName.as<std::string>();
            if (property.name.empty())
            {
                outError = "빈 property 이름이 있다";
                return false;
            }
            if (!DeserializeValue(entry, property.name, property.value,
                outError))
            {
                return false;
            }
            material.properties.push_back(std::move(property));
        }

        const YAML::Node keywords = node["keywords"];
        if (!keywords || !keywords.IsSequence())
        {
            outError = "keywords가 시퀀스가 아니다";
            return false;
        }
        for (const YAML::Node& keyword : keywords)
        {
            if (!keyword.IsScalar())
            {
                outError = "keyword가 스칼라가 아니다";
                return false;
            }
            const std::string keywordText = keyword.as<std::string>();
            if (keywordText.empty())
            {
                outError = "빈 keyword 문자열이 있다";
                return false;
            }
            material.keywords.push_back(keywordText);
        }

        const YAML::Node selections = node["keywordSelections"];
        if (!selections || !selections.IsSequence())
        {
            outError = "keywordSelections가 시퀀스가 아니다";
            return false;
        }
        for (const YAML::Node& selection : selections)
        {
            if (!selection.IsScalar())
            {
                outError = "keywordSelection이 스칼라가 아니다";
                return false;
            }
            const std::uint32_t value = selection.as<std::uint32_t>();
            if (value > 0xFFFFu)
            {
                outError = "keywordSelection이 uint16 범위를 넘는다";
                return false;
            }
            material.keywordSelections.push_back(
                static_cast<std::uint16_t>(value));
        }

        outMaterial = std::move(material);
        outError.clear();
        return true;
    }

    bool SerializeMaterialPropertyValue(const MaterialProperty& property,
        YAML::Node& outEntry, std::string& outError)
    {
        return SerializeValue(property, outEntry, outError);
    }

    bool DeserializeMaterialPropertyValue(const YAML::Node& entry,
        const std::string& name, MaterialPropertyValue& outValue,
        std::string& outError)
    {
        return DeserializeValue(entry, name, outValue, outError);
    }
}
