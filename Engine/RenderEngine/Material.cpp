#include "Material.h"
#include "DataSystem.h"
#include "MaterialPropertyPacker.h"
#include "MaterialGraphShaderMeta.h"
#include "ShaderMeta.h"
#include "ShaderMetaReflection.h"
#include "StandardMaterialProperty.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <type_traits>

namespace
{
    static_assert(sizeof(math::vector2) == sizeof(float) * 2);
    static_assert(sizeof(math::vector3) == sizeof(float) * 3);
    static_assert(sizeof(math::vector4) == sizeof(float) * 4);
    static_assert(sizeof(math::matrix4x4) == sizeof(float) * 16);
    static_assert(std::is_trivially_copyable_v<math::vector2>);
    static_assert(std::is_trivially_copyable_v<math::vector3>);
    static_assert(std::is_trivially_copyable_v<math::vector4>);
    static_assert(std::is_trivially_copyable_v<math::matrix4x4>);

    // packing 구현은 MaterialPropertyPacker(ShaderMeta 계약 쪽)가 정본이다.
    // 여기에 로컬 구현을 되살리면 packer가 둘이 된다 — I5-M1 패리티 게이트의
    // 전제가 깨진다.
    using MaterialPropertyPacker::NumericElementCount;
    using MaterialPropertyPacker::LogicalByteSize;
    using MaterialPropertyPacker::FindBinding;
    using MaterialPropertyPacker::ApplyDefault;
    using MaterialPropertyPacker::ValidateLogicalValue;
    using MaterialPropertyPacker::PackProperty;
}

Material::Material()
{
}

Material::Material(const Material& material)
    : m_name(material.m_name), m_baseColorTexName(material.m_baseColorTexName),
      m_normalTexName(material.m_normalTexName), m_ORM_TexName(material.m_ORM_TexName),
      m_AO_TexName(material.m_AO_TexName), m_EmissiveTexName(material.m_EmissiveTexName),
      m_materialInfo(material.m_materialInfo), m_flowInfo(material.m_flowInfo),
      m_shaderMetaGuid(material.m_shaderMetaGuid), m_propertyValues(material.m_propertyValues),
      m_keywordSelections(material.m_keywordSelections), m_fileGuid(material.m_fileGuid),
      m_renderingMode(material.m_renderingMode), m_doubleSided(material.m_doubleSided),
      m_cbufferValues(material.m_cbufferValues), m_runtimeInstance(material.m_runtimeInstance),
      m_shaderMetaHandle(material.m_shaderMetaHandle), m_textureOwners(material.m_textureOwners),
      m_materialGraphInstance(material.m_materialGraphInstance), m_assetOrigin(material.m_assetOrigin)
{
}

Material::Material(Material&& material) noexcept
{
    std::exchange(m_name, material.m_name);
	m_textureOwners = std::move(material.m_textureOwners);
    std::exchange(m_fileGuid, material.m_fileGuid);
    std::exchange(m_baseColorTexName, material.m_baseColorTexName);
    std::exchange(m_normalTexName, material.m_normalTexName);
    std::exchange(m_ORM_TexName, material.m_ORM_TexName);
    std::exchange(m_AO_TexName, material.m_AO_TexName);
    std::exchange(m_EmissiveTexName, material.m_EmissiveTexName);
    m_materialGuid = std::move(material.m_materialGuid);
    m_renderingMode = std::move(material.m_renderingMode);
    m_doubleSided = material.m_doubleSided;
    m_materialInfo = std::move(material.m_materialInfo);
    m_flowInfo = std::move(material.m_flowInfo);
    m_shaderMetaGuid = std::exchange(material.m_shaderMetaGuid, {});
	m_shaderMetaHandle = std::exchange(material.m_shaderMetaHandle, {});
    m_propertyValues = std::move(material.m_propertyValues);
    m_keywordSelections = std::move(material.m_keywordSelections);
    m_runtimeInstance = std::move(material.m_runtimeInstance);
    m_cbufferValues = std::move(material.m_cbufferValues);
    m_materialGraphInstance = std::move(material.m_materialGraphInstance);
    m_assetOrigin = std::move(material.m_assetOrigin);
}

Material::~Material()
{
}

bool Material::TrySetMaterialGraphParameter(LX::Id parameter, LX::LXSocketValue value, std::string& error)
{
    const material_graph::ParameterOverride edit{parameter, std::move(value)};
    return TrySetMaterialGraphParameters(std::span(&edit, 1), error);
}

bool Material::TrySetMaterialGraphParameters(std::span<const material_graph::ParameterOverride> values, std::string& error)
{
    if (!m_materialGraphInstance)
    {
        error = "Material has no LX graph instance.";
        return false;
    }
    auto description = m_materialGraphInstance->description;
    for (const auto& value : values)
    {
        const auto found = std::ranges::find(description.parameters, value.id, &material_graph::ParameterOverride::id);
        if (found == description.parameters.end())
        {
            description.parameters.push_back(value);
        }
        else
        {
            found->value = value.value;
        }
    }
    const auto textureLoader = [this](const experiment::AssetId& id, LX::LXColorSpace colorSpace,
                                      std::string&) -> own::shared_owner<const Texture> {
        for (const auto& texture : m_materialGraphInstance->textures)
            if (texture.assetId == id && texture.colorSpace == colorSpace)
                return texture.owner;
        return {};
    };
    return material_graph::BuildInstance(m_materialGraphInstance->generation, description, textureLoader,
                                         m_materialGraphInstance, error);
}

own::shared_owner<Material> Material::InstantiateShared(const Material* origin, std::string_view newName)
{
    if (!origin)
    {
        return nullptr;
    }

    auto cloneMaterial = own::make_shared<Material>(*origin);
    const std::string cloneSuffix = "_Clone";
    std::string baseName = newName.empty() ? origin->m_name : std::string(newName);
    if (newName.empty())
    {
        const auto suffix = baseName.rfind(cloneSuffix);
        if (suffix != std::string::npos)
        {
            const auto digits = suffix + cloneSuffix.size();
            if (digits == baseName.size() ||
                std::all_of(baseName.begin() + digits, baseName.end(), [](unsigned char c) { return std::isdigit(c); }))
            {
                baseName.erase(suffix);
            }
        }
        baseName += cloneSuffix;
    }
    cloneMaterial->m_name = std::move(baseName);

    // Runtime clones are caller-owned values, not implicitly published assets.
    // Their local display names do not reserve or probe DataSystem cache keys.
    return cloneMaterial;
}

Material& Material::SetBaseColor(math::vector3 color)
{
    m_materialInfo.m_baseColor = { color.x, color.y, color.z, 1.f };

	return *this;
}

Material& Material::SetBaseColor(float r, float g, float b)
{
	m_materialInfo.m_baseColor = { r, g, b, 1.f };

	return *this;
}

Material& Material::SetMetallic(float metallic)
{
	m_materialInfo.m_metallic = metallic;

	return *this;
}

Material& Material::SetRoughness(float roughness)
{
	m_materialInfo.m_roughness = roughness;

	return *this;
}

Material& Material::ConvertToLinearSpace(bool32 convert)
{
	m_materialInfo.m_convertToLinearSpace = convert;

	return *this;
}

Material& Material::SetWindVector(const math::vector4& windVector)
{
	m_flowInfo.m_windVector = windVector;

	return *this;
}

Material& Material::SetUVScroll(const math::vector2& uvScroll)
{
	m_flowInfo.m_uvScroll = uvScroll;

	return *this;
}

Material& Material::UseBaseColorMap(own::shared_owner<const Texture> texture)
{
	return UseTextureMap(standard_material::property::BaseColorMap,
		std::move(texture));
}

Material& Material::UseNormalMap(own::shared_owner<const Texture> texture)
{
	return UseTextureMap(standard_material::property::NormalMap,
		std::move(texture));
}

Material& Material::UseOccRoughMetalMap(own::shared_owner<const Texture> texture)
{
	return UseTextureMap(standard_material::property::OrmMap,
		std::move(texture));
}

Material& Material::UseAOMap(own::shared_owner<const Texture> texture)
{
	return UseTextureMap(standard_material::property::AoMap,
		std::move(texture));
}

Material& Material::UseEmissiveMap(own::shared_owner<const Texture> texture)
{
	return UseTextureMap(standard_material::property::EmissiveMap,
		std::move(texture));
}

Material& Material::UseTextureMap(
	std::string_view property, own::shared_owner<const Texture> texture)
{
	// Generated graph textures are changed by stable parameter GUID overrides,
    // so this legacy owner-only API cannot detach their accepted generation.
    if (HasMaterialGraph()) return *this;
	if (property.empty()) return *this;
    if (m_runtimeInstance)
    {
        std::string error;
        if (!LX::Runtime::SetTextureOwner(*m_runtimeInstance, property, texture, m_runtimeInstance, error)) return *this;
        SynchronizeCodeRuntime();
    }

	auto found = std::find_if(m_textureOwners.begin(), m_textureOwners.end(),
		[property](const MaterialTextureOwner& candidate)
		{
			return candidate.propertyName == property;
		});
	if (texture)
	{
		if (found == m_textureOwners.end())
		{
			MaterialTextureOwner owner{};
			owner.propertyName = std::string(property);
			owner.textureOwner = std::move(texture);
			m_textureOwners.push_back(std::move(owner));
		}
		else
		{
			found->textureOwner = std::move(texture);
		}
	}
	else if (found != m_textureOwners.end())
	{
		m_textureOwners.erase(found);
	}

	const bool hasTexture = static_cast<bool>(GetTextureMapShared(property));
	if (property == standard_material::property::BaseColorMap)
	{
		m_materialInfo.m_useBaseColor = hasTexture;
	}
	else if (property == standard_material::property::NormalMap)
	{
		m_materialInfo.m_useNormalMap = hasTexture ? kUseNormalMap : 0;
	}
	else if (property == standard_material::property::OrmMap)
	{
		m_materialInfo.m_useOccRoughMetal = hasTexture;
	}
	else if (property == standard_material::property::AoMap)
	{
		m_materialInfo.m_useAOMap = hasTexture;
	}
	else if (property == standard_material::property::EmissiveMap)
	{
		m_materialInfo.m_useEmissive = hasTexture;
	}
	return *this;
}

const own::shared_owner<const Texture>& Material::GetTextureMapShared(
	std::string_view property) const noexcept
{
	const auto owners = GetTextureOwners();
    const auto found = std::find_if(owners.begin(), owners.end(),
		[property](const MaterialTextureOwner& candidate)
		{
			return candidate.propertyName == property;
		});
	if (found != owners.end()) return found->textureOwner;
	static const own::shared_owner<const Texture> empty{};
	return empty;
}

const own::shared_owner<const Texture>& Material::GetBaseColorMapShared() const noexcept
{
	return GetTextureMapShared(standard_material::property::BaseColorMap);
}

const own::shared_owner<const Texture>& Material::GetNormalMapShared() const noexcept
{
	return GetTextureMapShared(standard_material::property::NormalMap);
}

const own::shared_owner<const Texture>& Material::GetOccRoughMetalMapShared() const noexcept
{
	return GetTextureMapShared(standard_material::property::OrmMap);
}

const own::shared_owner<const Texture>& Material::GetAOMapShared() const noexcept
{
	return GetTextureMapShared(standard_material::property::AoMap);
}

const own::shared_owner<const Texture>& Material::GetEmissiveMapShared() const noexcept
{
	return GetTextureMapShared(standard_material::property::EmissiveMap);
}

void Material::ResetTextureRuntime()
{
	m_textureOwners.clear();
	m_materialInfo.m_useBaseColor = false;
	m_materialInfo.m_useNormalMap = 0;
	m_materialInfo.m_useOccRoughMetal = false;
	m_materialInfo.m_useAOMap = false;
	m_materialInfo.m_useEmissive = false;
}

bool Material::ConfigureShaderProperties(const ShaderMeta& meta,
    const ShaderMetaBindingLayout& layout, std::string& outError,
	ShaderMetaHandle shaderMetaHandle)
{
    if (HasMaterialGraph())
    {
        outError = "An LX graph material cannot be configured as a ShaderMeta material.";
        return false;
    }
    if (!shaderMetaHandle.IsValid())
	{
		outError = "Material ShaderMeta cache handle이 invalid다";
		return false;
	}
    if (FileGuid{} == meta.guid)
    {
        outError = "Material ShaderMeta GUID가 nil이다";
        return false;
    }
    if (layout.properties.size() != meta.properties.size())
    {
        outError = "Material ShaderMeta property와 reflection layout 수가 다르다";
        return false;
    }

    std::vector<MaterialPropertyValue> values;
    values.reserve(meta.properties.size());
    for (const ShaderPropertyDesc& desc : meta.properties)
    {
        const ShaderMetaPropertyBinding* binding = FindBinding(layout, desc.name);
        if (!binding || binding->propertyType != desc.type)
        {
            outError = "Material ShaderMeta property binding이 없거나 type이 다르다: "
                + desc.name;
            return false;
        }

        MaterialPropertyValue value;
        const auto old = std::find_if(m_propertyValues.begin(), m_propertyValues.end(),
            [&](const MaterialPropertyValue& candidate)
            {
                return candidate.m_name == desc.name;
            });
        if (old != m_propertyValues.end()) value = *old;
        else if (!ApplyDefault(desc, value, outError)) return false;

        values.push_back(std::move(value));
    }

    std::vector<std::uint16_t> selections(meta.keywords.size(), 0);
    for (std::size_t index = 0;
        index < selections.size() && index < m_keywordSelections.size(); ++index)
    {
        if (m_keywordSelections[index] < meta.keywords[index].values.size())
            selections[index] = m_keywordSelections[index];
    }

    own::shared_owner<const LX::Runtime::ShaderGeneration> shader;
    own::shared_owner<const LX::Runtime::Instance> instance;
    std::vector<MaterialTextureOwner> owners;
    for (const auto& owner : m_textureOwners)
    {
        const auto* binding = FindBinding(layout, owner.propertyName);
        if (binding && binding->propertyType == ShaderPropertyType::Texture2D) owners.push_back(owner);
    }
    if (!LX::Runtime::CreateCodeShader(meta, layout, shaderMetaHandle, shader, outError) ||
        !LX::Runtime::BuildInstance(std::move(shader), values, selections, owners, instance, outError)) return false;
    m_runtimeInstance = std::move(instance);
    m_shaderMetaGuid = meta.guid;
	m_shaderMetaHandle = shaderMetaHandle;
    SynchronizeCodeRuntime();
    outError.clear();
    return true;
}

void Material::ResetShaderRuntime()
{
    m_runtimeInstance.reset();
	m_shaderMetaHandle = {};
}

const ShaderMetaBindingLayout* Material::GetShaderBindingLayout() const
{
    const auto* instance = RuntimeInstance();
    return instance && instance->shader ? &instance->shader->layout : nullptr;
}

const ShaderMeta* Material::GetGeneratedShaderMeta() const
{
    return m_materialGraphInstance && m_materialGraphInstance->generation->cooked.product.materialShader
        ? &m_materialGraphInstance->generation->cooked.product.materialShader->meta : nullptr;
}

std::span<const MaterialPropertyValue> Material::GetShaderPropertyValues() const
{
    if (const auto* instance = RuntimeInstance()) return instance->properties;
    return m_propertyValues;
}

std::span<const std::uint8_t> Material::GetConstantBufferData() const
{
    if (const auto* instance = RuntimeInstance()) return instance->uniforms;
    return {};
}

void Material::SynchronizeCodeRuntime()
{
    if (!m_runtimeInstance) return;
    m_propertyValues = m_runtimeInstance->properties;
    m_keywordSelections = m_runtimeInstance->keywordSelections;
    m_textureOwners = m_runtimeInstance->textureOwners;
    m_cbufferValues.clear();
    const auto& layout = m_runtimeInstance->shader->layout;
    if (!layout.constantBufferName.empty()) m_cbufferValues.emplace(layout.constantBufferName, m_runtimeInstance->uniforms);
}

bool Material::BuildShaderPropertyBlock(const ShaderMeta& meta,
    const ShaderMetaBindingLayout& layout,
    std::vector<std::uint8_t>& outBytes, std::string& outError) const
{
    if (m_materialGraphInstance)
    {
        const auto* generated = GetGeneratedShaderMeta();
        const auto* binding = GetShaderBindingLayout();
        if (!generated || !binding || !meta.generatedMaterial || meta.guid != generated->guid ||
            meta.generatedMaterial->generation != generated->generatedMaterial->generation || layout != *binding)
        {
            outError = "Material property block requires the accepted generated schema and layout.";
            return false;
        }
        outBytes = m_materialGraphInstance->uniforms;
        outError.clear();
        return true;
    }
    if (layout.properties.size() != meta.properties.size()
        || layout.constantBufferName.empty()
        || 0 == layout.constantBufferByteSize)
    {
        outError = "Material draw snapshot의 ShaderMeta layout이 불완전하다";
        return false;
    }

    std::vector<std::uint8_t> bytes(layout.constantBufferByteSize, 0);
    for (const ShaderPropertyDesc& desc : meta.properties)
    {
        const ShaderMetaPropertyBinding* binding = FindBinding(layout, desc.name);
        if (!binding || binding->propertyType != desc.type)
        {
            outError = "Material draw snapshot property binding이 없거나 type이 다르다: "
                + desc.name;
            return false;
        }

        MaterialPropertyValue value;
        const auto existing = std::find_if(m_propertyValues.begin(),
            m_propertyValues.end(), [&](const MaterialPropertyValue& candidate)
            {
                return candidate.m_name == desc.name;
            });
        if (existing != m_propertyValues.end())
        {
            value = *existing;
        }
        else if (!ApplyDefault(desc, value, outError))
        {
            return false;
        }

        // legacy Material은 숫자 property가 없고 MaterialInfo만 가진다. 이름 기반
        // 값이 실제로 없을 때만 세 필드를 호환 입력으로 사용한다. experiment
        // importer가 게시한 논리 property는 언제나 이 fallback보다 우선한다.
        if (existing == m_propertyValues.end())
        {
            if (desc.name == standard_material::property::BaseColor)
            {
                value.m_numericValue = {
                    m_materialInfo.m_baseColor.r,
                    m_materialInfo.m_baseColor.g,
                    m_materialInfo.m_baseColor.b,
                    m_materialInfo.m_baseColor.a,
                };
            }
            else if (desc.name == standard_material::property::Metallic)
            {
                value.m_numericValue = { m_materialInfo.m_metallic };
            }
            else if (desc.name == standard_material::property::Roughness)
            {
                value.m_numericValue = { m_materialInfo.m_roughness };
            }
        }

        if (!ValidateLogicalValue(desc, value, outError)
            || !PackProperty(desc, *binding, value, bytes, outError))
        {
            if (outError.empty())
                outError = "Material draw snapshot property pack 실패: " + desc.name;
            return false;
        }
    }

    outBytes = std::move(bytes);
    outError.clear();
    return true;
}

bool Material::TrySetTextureGuid(std::string_view property, const FileGuid& guid)
{
    const VarView view = FindProperty(property);
    if (!view.binding || ShaderPropertyType::Texture2D != view.binding->propertyType)
        return false;
    if (m_materialGraphInstance)
    {
        const auto& desc = GetGeneratedShaderMeta()->properties[view.propertyIndex];
        if (!desc.exposed || !desc.parameterId) return false;
        auto description = m_materialGraphInstance->description;
        const auto override = std::ranges::find(description.textures, desc.parameterId, &material_graph::TextureOverride::parameter);
        if (override == description.textures.end()) description.textures.push_back({desc.parameterId, experiment::AssetId{guid.m_guid}});
        else override->assetId.value = guid.m_guid;
        std::string error;
        return DataSystem::GetInstance()->ConfigureMaterialGraph(*this, description, error);
    }
    if (!m_runtimeInstance) return false;
    auto value = m_runtimeInstance->properties[view.propertyIndex];
    value.m_textureGuid = guid;
    std::string error;
    if (!LX::Runtime::SetValue(*m_runtimeInstance, value, m_runtimeInstance, error)) return false;
    SynchronizeCodeRuntime();
    return true;
}

bool Material::TryGetTextureGuid(std::string_view property, FileGuid& outGuid) const
{
    const VarView view = FindProperty(property);
    if (!view.binding || ShaderPropertyType::Texture2D != view.binding->propertyType)
        return false;
    outGuid = GetShaderPropertyValues()[view.propertyIndex].m_textureGuid;
    return true;
}

bool Material::TrySetKeywordSelection(std::string_view axis, std::string_view value)
{
    if (!m_runtimeInstance) return false;
    std::string error;
    if (!LX::Runtime::SetKeyword(*m_runtimeInstance, axis, value, m_runtimeInstance, error)) return false;
    SynchronizeCodeRuntime();
    return true;
}

Material::VarView Material::FindVar(std::string_view cb, std::string_view var) const
{
    const auto* layout = GetShaderBindingLayout();
    if (!layout || cb != layout->constantBufferName) return {};
    return FindProperty(var);
}

Material::VarView Material::FindProperty(std::string_view property) const
{
    const auto* layout = GetShaderBindingLayout();
    if (!layout) return {};
    const ShaderMetaPropertyBinding* binding = FindBinding(*layout, property);
    if (!binding) return {};
    const auto values = GetShaderPropertyValues();
    const auto value = std::find_if(values.begin(), values.end(),
        [&](const MaterialPropertyValue& candidate)
        {
            return candidate.m_name == property;
        });
    if (value == values.end()) return {};
    return { binding, static_cast<std::size_t>(value - values.begin()) };
}

bool Material::SplitQualified(std::string_view q, std::string& outCB, std::string& outVar)
{
    auto dot = q.find('.');
    if (dot == std::string_view::npos) return false;
    outCB = std::string(q.substr(0, dot));
    outVar = std::string(q.substr(dot + 1));
    return (!outCB.empty() && !outVar.empty());
}

bool Material::WriteBytes(const VarView& v, const void* src, size_t size)
{
    if (!v.binding || !src || v.propertyIndex >= GetShaderPropertyValues().size()) return false;
    if (size != LogicalByteSize(v.binding->propertyType)
        || size > v.binding->byteSize) return false;
    if (m_materialGraphInstance)
    {
        const auto& desc = GetGeneratedShaderMeta()->properties[v.propertyIndex];
        if (!desc.parameterId || !desc.exposed) return false;
        LX::LXSocketValue value;
        switch (desc.type)
        {
        case ShaderPropertyType::Bool: { std::int32_t input; std::memcpy(&input, src, 4); value = input != 0; break; }
        case ShaderPropertyType::Int: { std::int32_t input; std::memcpy(&input, src, 4); value = std::int64_t(input); break; }
        case ShaderPropertyType::Float: { float input; std::memcpy(&input, src, 4); value = double(input); break; }
          case ShaderPropertyType::Float3: case ShaderPropertyType::Float4:
          {
              std::array<float, 4> input{};
              if (size > sizeof(input)) return false;
              std::memcpy(input.data(), src, size);
            if (desc.type == ShaderPropertyType::Float3) value = std::array<double, 3>{input[0], input[1], input[2]};
            else value = std::array<double, 4>{input[0], input[1], input[2], input[3]};
            break;
        }
        default: return false;
        }
        std::string error;
        return TrySetMaterialGraphParameter(desc.parameterId, std::move(value), error);
    }
    if (!m_runtimeInstance) return false;
    auto value = m_runtimeInstance->properties[v.propertyIndex];
    const std::size_t numericCount = NumericElementCount(v.binding->propertyType);
    if (0 != numericCount)
    {
        value.m_numericValue.resize(numericCount);
        std::memcpy(value.m_numericValue.data(), src, size);
    }
    else if (ShaderPropertyType::Int == v.binding->propertyType)
        std::memcpy(&value.m_integerValue, src, sizeof(value.m_integerValue));
    else if (ShaderPropertyType::Bool == v.binding->propertyType)
    {
        std::int32_t encoded{};
        std::memcpy(&encoded, src, sizeof(encoded));
        value.m_boolValue = 0 != encoded;
    }

    std::string error;
    if (!LX::Runtime::SetValue(*m_runtimeInstance, value, m_runtimeInstance, error)) return false;
    SynchronizeCodeRuntime();
    return true;
}

bool Material::ReadBytes(const VarView& v, void* dst, size_t size) const
{
    if (!v.binding || !dst || size > LogicalByteSize(v.binding->propertyType)) return false;
    if (RuntimeInstance())
    {
        const auto bytes = GetConstantBufferData();
        if (v.binding->byteOffset + size > bytes.size()) return false;
        std::memcpy(dst, bytes.data() + v.binding->byteOffset, size);
        return true;
    }
    auto it = m_cbufferValues.find(v.binding->resourceName);
    if (it == m_cbufferValues.end()) return false;

    const auto& bytes = it->second;
    if (v.binding->byteOffset + size > bytes.size()) return false;
    std::memcpy(dst, bytes.data() + v.binding->byteOffset, size);
    return true;
}

// ───────────────────────────────
// 타입 안전 Setter / Getter
// ───────────────────────────────
bool Material::TrySetFloat(std::string_view cb, std::string_view var, float v)
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Float == view.binding->propertyType
        && WriteBytes(view, &v, sizeof(float));
}
bool Material::TryGetFloat(std::string_view cb, std::string_view var, float& out) const
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Float == view.binding->propertyType
        && ReadBytes(view, &out, sizeof(float));
}

bool Material::TrySetInt(std::string_view cb, std::string_view var, int32_t v)
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Int == view.binding->propertyType
        && WriteBytes(view, &v, sizeof(int32_t));
}
bool Material::TryGetInt(std::string_view cb, std::string_view var, int32_t& out) const
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Int == view.binding->propertyType
        && ReadBytes(view, &out, sizeof(int32_t));
}

bool Material::TrySetBool(std::string_view cb, std::string_view var, bool v)
{
    // HLSL cbuffer bool은 4바이트 정렬을 따르므로 int로 저장
    int32_t iv = v ? 1 : 0;
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Bool == view.binding->propertyType
        && WriteBytes(view, &iv, sizeof(int32_t));
}
bool Material::TryGetBool(std::string_view cb, std::string_view var, bool& out) const
{
    int32_t iv{};
    const VarView view = FindVar(cb, var);
    if (!view.binding || ShaderPropertyType::Bool != view.binding->propertyType
        || !ReadBytes(view, &iv, sizeof(int32_t))) return false;
    out = (iv != 0);
    return true;
}

bool Material::TrySetVector(std::string_view cb, std::string_view var, const math::vector2& v)
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Float2 == view.binding->propertyType
        && WriteBytes(view, &v, sizeof(v));
}
bool Material::TrySetVector(std::string_view cb, std::string_view var, const math::vector3& v)
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Float3 == view.binding->propertyType
        && WriteBytes(view, &v, sizeof(v));
}
bool Material::TrySetVector(std::string_view cb, std::string_view var, const math::vector4& v)
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Float4 == view.binding->propertyType
        && WriteBytes(view, &v, sizeof(v));
}
bool Material::TryGetVector(std::string_view cb, std::string_view var, math::vector4& out) const
{
    // 최대 16바이트의 float4를 읽어서 돌려줌 (var size가 8/12면 앞부분만 유효)
    auto v = FindVar(cb, var);
    if (!v.binding || (ShaderPropertyType::Float2 != v.binding->propertyType
        && ShaderPropertyType::Float3 != v.binding->propertyType
        && ShaderPropertyType::Float4 != v.binding->propertyType)) return false;
    std::memset(&out, 0, sizeof(out));
    size_t n = std::min<size_t>(sizeof(out), LogicalByteSize(v.binding->propertyType));
    return ReadBytes(v, &out, n);
}

bool Material::TrySetMatrix(std::string_view cb, std::string_view var, const math::matrix4x4& m)
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Float4x4 == view.binding->propertyType
        && WriteBytes(view, &m, sizeof(m));
}
bool Material::TryGetMatrix(std::string_view cb, std::string_view var, math::matrix4x4& out) const
{
    const VarView view = FindVar(cb, var);
    return view.binding && ShaderPropertyType::Float4x4 == view.binding->propertyType
        && ReadBytes(view, &out, sizeof(out));
}

bool Material::TrySetValue(std::string_view cb, std::string_view var, const void* src, size_t size)
{
	return WriteBytes(FindVar(cb, var), src, size);
}

// ── Qualified name sugar ("CB.Var") ──
bool Material::TrySetFloat(std::string_view q, float v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetFloat(cb, var, v);
}
bool Material::TryGetFloat(std::string_view q, float& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetFloat(cb, var, out);
}
bool Material::TrySetInt(std::string_view q, int32_t v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetInt(cb, var, v);
}
bool Material::TryGetInt(std::string_view q, int32_t& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetInt(cb, var, out);
}
bool Material::TrySetBool(std::string_view q, bool v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetBool(cb, var, v);
}
bool Material::TryGetBool(std::string_view q, bool& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetBool(cb, var, out);
}
bool Material::TrySetVector(std::string_view q, const math::vector2& v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetVector(cb, var, v);
}
bool Material::TrySetVector(std::string_view q, const math::vector3& v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetVector(cb, var, v);
}
bool Material::TrySetVector(std::string_view q, const math::vector4& v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetVector(cb, var, v);
}
bool Material::TryGetVector(std::string_view q, math::vector4& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetVector(cb, var, out);
}
bool Material::TrySetMatrix(std::string_view q, const math::matrix4x4& m) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetMatrix(cb, var, m);
}
bool Material::TryGetMatrix(std::string_view q, math::matrix4x4& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetMatrix(cb, var, out);
}
