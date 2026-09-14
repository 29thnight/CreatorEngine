#include "MaterialAuthoringCodec.h"

#include "AssetIdentity.h"
#include "../Assets/AssetIdentityProfile.h" // MBC7: UUIDv8 subasset 신원(재질·embedded texture)
#include "AuthoringReadNode.h"
#include "AuthoringWriteNode.h"

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

        [[nodiscard]] bool ParseGuid(const Authoring::ReadNode& node, bool allowNil,
            const char* context, AssetId& out, std::string& outError)
        {
            if (!node || !node.IsScalar())
            {
                outError = std::string(context) + " GUID가 스칼라가 아니다";
                return false;
            }
            const std::string text = node.AsString();
            if (allowNil && text == kNilGuidText)
            {
                out = {};
                return true;
            }
            if (!TryParseCanonicalAssetId(text, out))
            {
                // PHASE 3.75 MBC7 — 혼합 신원 계약(PHASE 17 Strict): 비모델 자산은
                // UUIDv4, 모델 재질(assetId)과 embedded texture 참조는 UUIDv8 subasset
                // 신원이다. writer(SerializeMaterialPayload)는 이미 v8을 적고 있었는데
                // reader만 v4를 고집해 저장 씬의 모델 재질이 통째로 해석 실패했다
                // (실측: Gunner 콜드 로드 재질 property 0). 소문자 canonical만 받는다.
                if (!assets::TryParseCanonicalUuidV8(text, out.value))
                {
                    outError = std::string(context)
                        + " GUID가 canonical UUIDv4/UUIDv8가 아니다: " + text;
                    return false;
                }
            }
            return true;
        }

        // ── W7 sampler 어휘 ─────────────────────────────────────────────
        //
        // ★ 저작 파일에는 **이름**을 적는다. 열거자 수를 적으면 어휘가 늘거나
        //   순서가 바뀔 때 옛 파일이 조용히 다른 뜻이 된다 — colorSpace 가
        //   "srgb"/"linear" 인 것과 같은 이유다.
        [[nodiscard]] const char* FilterText(RHIFilterMode mode)
        {
            return RHIFilterMode::Point == mode ? "point" : "linear";
        }

        [[nodiscard]] const char* AddressText(RHIAddressMode mode)
        {
            switch (mode)
            {
            case RHIAddressMode::Clamp:  return "clamp";
            case RHIAddressMode::Border: return "border";
            case RHIAddressMode::Mirror: return "mirror";
            case RHIAddressMode::Wrap:
            default:                     return "wrap";
            }
        }

        [[nodiscard]] bool ParseFilter(const Authoring::ReadNode& node,
            const char* context, RHIFilterMode& out, std::string& outError)
        {
            if (!node.IsScalar())
            {
                outError = std::string(context) + " 값이 스칼라가 아니다";
                return false;
            }
            const std::string text = node.AsString();
            if (text == "point") { out = RHIFilterMode::Point; return true; }
            if (text == "linear") { out = RHIFilterMode::Linear; return true; }
            outError = std::string(context) + " 의 미지 필터다: " + text;
            return false;
        }

        [[nodiscard]] bool ParseAddress(const Authoring::ReadNode& node,
            const char* context, RHIAddressMode& out, std::string& outError)
        {
            if (!node.IsScalar())
            {
                outError = std::string(context) + " 값이 스칼라가 아니다";
                return false;
            }
            const std::string text = node.AsString();
            if (text == "wrap") { out = RHIAddressMode::Wrap; return true; }
            if (text == "clamp") { out = RHIAddressMode::Clamp; return true; }
            if (text == "border") { out = RHIAddressMode::Border; return true; }
            if (text == "mirror") { out = RHIAddressMode::Mirror; return true; }
            static_assert(kRHIAddressModeCount == 4,
                "RHIAddressMode 가 늘었다 — 저작 어휘도 함께 늘려라.");
            outError = std::string(context) + " 의 미지 주소 모드다: " + text;
            return false;
        }

        template <std::size_t Count>
        [[nodiscard]] bool ParseFloats(const Authoring::ReadNode& node,
            const char* context, std::array<float, Count>& out,
            std::string& outError)
        {
            if (!node.IsSequence() || node.Size() != Count)
            {
                outError = std::string(context) + " 값은 float "
                    + std::to_string(Count) + "개 시퀀스여야 한다";
                return false;
            }
            for (std::size_t index = 0; index < Count; ++index)
            {
                const Authoring::ReadNode value = node.At(index);
                if (!value.IsScalar())
                {
                    outError = std::string(context) + " 값에 비스칼라가 있다";
                    return false;
                }
                out[index] = value.As<float>();
            }
            return true;
        }

        void WriteFloats(Authoring::WriteNode node,
			std::initializer_list<float> values)
        {
			node.SetSequence(true);
			for (const float value : values) node.Append().SetScalar(value);
        }

        [[nodiscard]] bool SerializeValue(const MaterialProperty& property,
			Authoring::WriteNode outEntry, std::string& outError)
        {
            if (const auto* value = std::get_if<bool>(&property.value))
            {
				outEntry.Child("bool").SetScalar(*value);
            }
            else if (const auto* value = std::get_if<std::int32_t>(&property.value))
            {
				outEntry.Child("int").SetScalar(*value);
            }
            else if (const auto* value =
                std::get_if<std::uint32_t>(&property.value))
            {
				outEntry.Child("uint").SetScalar(*value);
            }
            else if (const auto* value = std::get_if<float>(&property.value))
            {
				outEntry.Child("float").SetScalar(*value);
            }
            else if (const auto* value =
                std::get_if<math::vector2>(&property.value))
            {
				WriteFloats(outEntry.Child("float2"), { value->x, value->y });
            }
            else if (const auto* value =
                std::get_if<math::vector3>(&property.value))
            {
				WriteFloats(outEntry.Child("float3"),
					{ value->x, value->y, value->z });
            }
            else if (const auto* value =
                std::get_if<math::vector4>(&property.value))
            {
				WriteFloats(outEntry.Child("float4"),
					{ value->x, value->y, value->z, value->w });
            }
            else if (const auto* value =
                std::get_if<std::string>(&property.value))
            {
				outEntry.Child("string").SetScalar(*value);
            }
            else if (const auto* value =
                std::get_if<TextureReference>(&property.value))
            {
				if (!value->coordinates.IsValid()) { outError = "Invalid texture UV coordinates"; return false; }
                const Authoring::WriteNode texture = outEntry.Child("texture");
				texture.SetMap(true);
				texture.Child("guid").SetScalar(GuidText(value->assetId));
				texture.Child("colorSpace").SetScalar(
					TextureColorSpace::Srgb == value->colorSpace
					? "srgb" : "linear");
                const auto& uv = value->coordinates;
                texture.Child("uvSet").SetScalar(uv.set);
                WriteFloats(texture.Child("uvOffset"), {uv.offset[0], uv.offset[1]});
                WriteFloats(texture.Child("uvScale"), {uv.scale[0], uv.scale[1]});
                texture.Child("uvRotation").SetScalar(uv.rotation);
                // ★ W7 — sampler 는 **기본값이 아닐 때만** 적는다. 무조건 적으면
                //   기존 .material 전부가 다음 저장에서 키 네 개를 얻고, 이 코덱은
                //   미지 키를 fail-closed 로 거부하므로 옛 리더가 새 파일을 못 읽는다.
                //   기본값(선형/선형/wrap/wrap)은 패스가 걸던 값과 같아 생략이 곧
                //   같은 뜻이다.
                if (assets::TextureSampler{} != value->sampler)
                {
                    const auto& sampler = value->sampler;
                    texture.Child("samplerFilter").SetScalar(FilterText(sampler.minMag));
                    texture.Child("samplerMip").SetScalar(FilterText(sampler.mip));
                    texture.Child("samplerWrapU").SetScalar(AddressText(sampler.addressU));
                    texture.Child("samplerWrapV").SetScalar(AddressText(sampler.addressV));
                }
            }
            else
            {
                outError = "직렬화할 수 없는 property 값이다: " + property.name;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool DeserializeValue(const Authoring::ReadNode& entry,
            const std::string& name, MaterialPropertyValue& outValue,
            std::string& outError)
        {
            std::size_t valueKeys = 0;
            for (const Authoring::MapEntry pair : entry.Map())
            {
                const std::string key = pair.key.AsString();
                if (key == "name") continue;
                ++valueKeys;
                const Authoring::ReadNode value = pair.value;
                if (key == "bool" || key == "int" || key == "uint"
                    || key == "float" || key == "string")
                {
                    if (!value.IsScalar())
                    {
                        outError = "property 값이 스칼라가 아니다: " + name;
                        return false;
                    }
                    if (key == "bool") outValue = value.As<bool>();
                    else if (key == "int") outValue = value.As<std::int32_t>();
                    else if (key == "uint") outValue = value.As<std::uint32_t>();
                    else if (key == "float") outValue = value.As<float>();
                    else outValue = value.AsString();
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
                    const Authoring::ReadNode colorSpace = value["colorSpace"];
                    if (!colorSpace || !colorSpace.IsScalar())
                    {
                        outError = "texture colorSpace가 없다: " + name;
                        return false;
                    }
                    const std::string colorSpaceText = colorSpace.AsString();
                    if (colorSpaceText == "srgb")
                        reference.colorSpace = TextureColorSpace::Srgb;
                    else if (colorSpaceText == "linear")
                        reference.colorSpace = TextureColorSpace::Linear;
                    else
                    {
                        outError = "미지의 colorSpace다: " + colorSpaceText;
                        return false;
                    }
                    auto& uv = reference.coordinates;
                    if (value["uvSet"]) uv.set = value["uvSet"].As<std::uint32_t>();
                    if (value["uvOffset"] && !ParseFloats(value["uvOffset"], "uvOffset", uv.offset, outError)) return false;
                    if (value["uvScale"] && !ParseFloats(value["uvScale"], "uvScale", uv.scale, outError)) return false;
                    if (value["uvRotation"]) uv.rotation = value["uvRotation"].As<float>();
                    if (!uv.IsValid()) { outError = "Invalid texture UV coordinates"; return false; }
                    auto& sampler = reference.sampler;
                    if (value["samplerFilter"] && !ParseFilter(value["samplerFilter"], "samplerFilter", sampler.minMag, outError)) return false;
                    if (value["samplerMip"] && !ParseFilter(value["samplerMip"], "samplerMip", sampler.mip, outError)) return false;
                    if (value["samplerWrapU"] && !ParseAddress(value["samplerWrapU"], "samplerWrapU", sampler.addressU, outError)) return false;
                    if (value["samplerWrapV"] && !ParseAddress(value["samplerWrapV"], "samplerWrapV", sampler.addressV, outError)) return false;
                    for (const Authoring::MapEntry texturePair : value.Map())
                    {
                        const std::string textureKey =
                            texturePair.key.AsString();
                        if (textureKey != "guid" && textureKey != "colorSpace"
                            && textureKey != "uvSet" && textureKey != "uvOffset"
                            && textureKey != "uvScale" && textureKey != "uvRotation"
                            && textureKey != "samplerFilter" && textureKey != "samplerMip"
                            && textureKey != "samplerWrapU" && textureKey != "samplerWrapV")
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

    bool SerializeMaterialAuthoring(const Material& material,
		Authoring::WriteNode outNode, std::string& outError)
    {
        if (material.blendMode != MaterialBlendMode::Opaque
            && material.blendMode != MaterialBlendMode::Transparent
            && material.blendMode != MaterialBlendMode::Masked)
        { outError = "Unknown material alpha mode"; return false; }
        if (!material.shaderAssetId.IsValid()
            || !IsAssetIdV4(material.shaderAssetId))
        {
            outError = "shaderAssetId가 canonical UUIDv4가 아니다: "
                + material.name;
            return false;
        }

        // fail-closed: 출력 노드를 직접 채우다 중간 검증에서 실패하면 부분 문서가
		// 남는다. 임시 Tree에서 완성한 뒤 성공할 때만 subtree를 교체한다.
		Authoring::WriteDocument staging;
		const Authoring::WriteNode node = staging.Root();
		node.SetMap();
		node.Child("schema").SetScalar(kMaterialAuthoringSchemaVersion);
		node.Child("assetId").SetScalar(GuidText(material.assetId));
		node.Child("shaderAssetId").SetScalar(GuidText(material.shaderAssetId));
		node.Child("name").SetScalar(material.name);
		node.Child("blendMode").SetScalar(
			MaterialBlendMode::Transparent == material.blendMode
			? "transparent" : MaterialBlendMode::Masked == material.blendMode
            ? "masked" : "opaque");

		const Authoring::WriteNode properties = node.Child("properties");
		properties.SetSequence();
        for (const MaterialProperty& property : material.properties)
        {
            if (property.name.empty())
            {
                outError = "빈 property 이름이 있다: " + material.name;
                return false;
            }
			const Authoring::WriteNode entry = properties.Append();
			entry.SetMap();
			entry.Child("name").SetScalar(property.name);
            if (!SerializeValue(property, entry, outError)) return false;
        }

		const Authoring::WriteNode keywords = node.Child("keywords");
		keywords.SetSequence(true);
        for (const std::string& keyword : material.keywords)
        {
            if (keyword.empty())
            {
                outError = "빈 keyword 문자열이 있다: " + material.name;
                return false;
            }
			keywords.Append().SetScalar(keyword);
        }

		const Authoring::WriteNode selections = node.Child("keywordSelections");
		selections.SetSequence(true);
        for (const std::uint16_t selection : material.keywordSelections)
			selections.Append().SetScalar(static_cast<std::uint32_t>(selection));

		outNode.Assign(node);
		if (!outNode.Read()["schema"])
		{
			outError = "material writer staging commit failed: " + outNode.Dump();
			return false;
		}
        outError.clear();
        return true;
    }

    bool DeserializeMaterialAuthoring(const Authoring::ReadNode& node,
        Material& outMaterial, std::string& outError)
    {
        if (!node || !node.IsMap())
        {
            outError = "material 저작 문서가 매핑이 아니다";
            return false;
        }
        const Authoring::ReadNode schema = node["schema"];
        if (!schema || !schema.IsScalar()
            || schema.As<std::uint32_t>() != kMaterialAuthoringSchemaVersion)
        {
			outError = "material 저작 schema 버전이 다르다(raw="
				+ (schema ? schema.AsString() : std::string("<missing>")) + ")";
            return false;
        }

        Material material;
        if (!ParseGuid(node["assetId"], true,
            "assetId", material.assetId,
            outError))
        {
            return false;
        }
        if (!ParseGuid(node["shaderAssetId"], false,
            "shaderAssetId",
            material.shaderAssetId, outError))
        {
            return false;
        }
        const Authoring::ReadNode name = node["name"];
        if (!name || !name.IsScalar())
        {
            outError = "name이 없다";
            return false;
        }
        material.name = name.AsString();

        const Authoring::ReadNode blendMode = node["blendMode"];
        if (!blendMode || !blendMode.IsScalar())
        {
            outError = "blendMode가 없다";
            return false;
        }
        const std::string blendModeText = blendMode.AsString();
        if (blendModeText == "opaque")
            material.blendMode = MaterialBlendMode::Opaque;
        else if (blendModeText == "masked")
            material.blendMode = MaterialBlendMode::Masked;
        else if (blendModeText == "transparent")
            material.blendMode = MaterialBlendMode::Transparent;
        else
        {
            outError = "미지의 blendMode다: " + blendModeText;
            return false;
        }

        const Authoring::ReadNode properties = node["properties"];
        if (!properties || !properties.IsSequence())
        {
            outError = "properties가 시퀀스가 아니다";
            return false;
        }
        for (const Authoring::ReadNode entry : properties)
        {
            if (!entry.IsMap())
            {
                outError = "property 항목이 매핑이 아니다";
                return false;
            }
            const Authoring::ReadNode entryName = entry["name"];
            if (!entryName || !entryName.IsScalar())
            {
                outError = "property name이 없다";
                return false;
            }
            MaterialProperty property;
            property.name = entryName.AsString();
            if (property.name.empty())
            {
                outError = "빈 property 이름이 있다";
                return false;
            }
            if (!DeserializeValue(entry, property.name,
                property.value,
                outError))
            {
                return false;
            }
            material.properties.push_back(std::move(property));
        }

        const Authoring::ReadNode keywords = node["keywords"];
        if (!keywords || !keywords.IsSequence())
        {
            outError = "keywords가 시퀀스가 아니다";
            return false;
        }
        for (const Authoring::ReadNode keyword : keywords)
        {
            if (!keyword.IsScalar())
            {
                outError = "keyword가 스칼라가 아니다";
                return false;
            }
            const std::string keywordText = keyword.AsString();
            if (keywordText.empty())
            {
                outError = "빈 keyword 문자열이 있다";
                return false;
            }
            material.keywords.push_back(keywordText);
        }

        const Authoring::ReadNode selections = node["keywordSelections"];
        if (!selections || !selections.IsSequence())
        {
            outError = "keywordSelections가 시퀀스가 아니다";
            return false;
        }
        for (const Authoring::ReadNode selection : selections)
        {
            if (!selection.IsScalar())
            {
                outError = "keywordSelection이 스칼라가 아니다";
                return false;
            }
            const std::uint32_t value = selection.As<std::uint32_t>();
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
		Authoring::WriteNode outEntry, std::string& outError)
    {
		// 이 API의 계약은 완성 문서 교체가 아니라 기존 property entry에 값 키
		// 하나를 추가하는 것이다. 호출자가 먼저 적은 `name`을 보존해야 한다.
		if (!SerializeValue(property, outEntry, outError)) return false;
		outError.clear();
		return true;
    }

    bool DeserializeMaterialPropertyValue(const Authoring::ReadNode& entry,
        const std::string& name, MaterialPropertyValue& outValue,
        std::string& outError)
    {
        return DeserializeValue(entry, name, outValue, outError);
    }
}
