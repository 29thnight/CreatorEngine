#include "Material.h"
#include "DataSystem.h"
#include "ShaderMeta.h"
#include "ShaderMetaReflection.h"
#include <array>
#include <cstring>
#include <type_traits>

struct Material::RuntimeSchema
{
    ShaderMetaBindingLayout layout{};
    std::vector<ShaderKeywordAxis> keywords{};
};

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

    std::size_t NumericElementCount(ShaderPropertyType type)
    {
        switch (type)
        {
        case ShaderPropertyType::Float: return 1;
        case ShaderPropertyType::Float2: return 2;
        case ShaderPropertyType::Float3: return 3;
        case ShaderPropertyType::Float4: return 4;
        case ShaderPropertyType::Float4x4: return 16;
        default: return 0;
        }
    }

    std::size_t LogicalByteSize(ShaderPropertyType type)
    {
        const std::size_t numericCount = NumericElementCount(type);
        if (0 != numericCount) return numericCount * sizeof(float);
        if (ShaderPropertyType::Int == type || ShaderPropertyType::Bool == type)
            return sizeof(std::int32_t);
        return 0;
    }

    const ShaderMetaPropertyBinding* FindBinding(
        const ShaderMetaBindingLayout& layout, std::string_view name)
    {
        const auto found = std::find_if(layout.properties.begin(), layout.properties.end(),
            [&](const ShaderMetaPropertyBinding& binding)
            {
                return binding.name == name;
            });
        return found == layout.properties.end() ? nullptr : &*found;
    }

    bool ApplyDefault(const ShaderPropertyDesc& desc, MaterialPropertyValue& outValue,
        std::string& outError)
    {
        outValue = {};
        outValue.m_name = desc.name;

        if (std::holds_alternative<std::monostate>(desc.defaultValue))
        {
            const std::size_t numericCount = NumericElementCount(desc.type);
            if (0 != numericCount) outValue.m_numericValue.assign(numericCount, 0.0f);
            return true;
        }
        if (const auto* value = std::get_if<float>(&desc.defaultValue))
            outValue.m_numericValue = { *value };
        else if (const auto* value = std::get_if<std::array<float, 2>>(&desc.defaultValue))
            outValue.m_numericValue.assign(value->begin(), value->end());
        else if (const auto* value = std::get_if<std::array<float, 3>>(&desc.defaultValue))
            outValue.m_numericValue.assign(value->begin(), value->end());
        else if (const auto* value = std::get_if<std::array<float, 4>>(&desc.defaultValue))
            outValue.m_numericValue.assign(value->begin(), value->end());
        else if (const auto* value = std::get_if<std::array<float, 16>>(&desc.defaultValue))
            outValue.m_numericValue.assign(value->begin(), value->end());
        else if (const auto* value = std::get_if<std::int32_t>(&desc.defaultValue))
            outValue.m_integerValue = *value;
        else if (const auto* value = std::get_if<bool>(&desc.defaultValue))
            outValue.m_boolValue = *value;
        else if (const auto* value = std::get_if<FileGuid>(&desc.defaultValue))
            outValue.m_textureGuid = *value;
        else
        {
            outError = "Material property default type이 schema와 맞지 않는다: " + desc.name;
            return false;
        }
        return true;
    }

    bool ValidateLogicalValue(const ShaderPropertyDesc& desc,
        const MaterialPropertyValue& value, std::string& outError)
    {
        const std::size_t numericCount = NumericElementCount(desc.type);
        if (0 != numericCount && value.m_numericValue.size() != numericCount)
        {
            outError = "Material numeric property 크기가 schema와 맞지 않는다: " + desc.name;
            return false;
        }
        return true;
    }

    bool PackProperty(const ShaderPropertyDesc& desc,
        const ShaderMetaPropertyBinding& binding, const MaterialPropertyValue& value,
        std::vector<std::uint8_t>& bytes, std::string& outError)
    {
        if (ShaderPropertyType::Texture2D == desc.type)
            return RHIShaderResourceKind::Texture == binding.resourceKind;

        const std::size_t payloadSize = LogicalByteSize(desc.type);
        if (RHIShaderResourceKind::ConstantBuffer != binding.resourceKind
            || payloadSize > binding.byteSize
            || binding.byteOffset + payloadSize > bytes.size())
        {
            outError = "Material property binding 범위가 잘못됐다: " + desc.name;
            return false;
        }

        void* destination = bytes.data() + binding.byteOffset;
        if (0 != NumericElementCount(desc.type))
            std::memcpy(destination, value.m_numericValue.data(), payloadSize);
        else if (ShaderPropertyType::Int == desc.type)
            std::memcpy(destination, &value.m_integerValue, payloadSize);
        else if (ShaderPropertyType::Bool == desc.type)
        {
            const std::int32_t encoded = value.m_boolValue ? 1 : 0;
            std::memcpy(destination, &encoded, payloadSize);
        }
        return true;
    }
}

Material::Material()
{
}

Material::Material(const Material& material) :
    m_name(material.m_name),
    m_baseColorTexName(material.m_baseColorTexName),
    m_pBaseColor(material.m_pBaseColor),
    m_normalTexName(material.m_normalTexName),
    m_pNormal(material.m_pNormal),
    m_ORM_TexName(material.m_ORM_TexName),
    m_pOccRoughMetal(material.m_pOccRoughMetal),
    m_AO_TexName(material.m_AO_TexName),
    m_AOMap(material.m_AOMap),
    m_EmissiveTexName(material.m_EmissiveTexName),
    m_pEmissive(material.m_pEmissive),
    m_materialInfo(material.m_materialInfo),
    m_flowInfo(material.m_flowInfo),
    m_shaderMetaGuid(material.m_shaderMetaGuid),
    m_propertyValues(material.m_propertyValues),
    m_keywordSelections(material.m_keywordSelections),
    m_fileGuid(material.m_fileGuid),
    m_renderingMode(material.m_renderingMode),
    m_cbufferValues(material.m_cbufferValues),
	m_dirtyCBs(material.m_dirtyCBs),
    m_runtimeSchema(material.m_runtimeSchema),
	m_shaderMetaHandle(material.m_shaderMetaHandle)
{
}

Material::Material(Material&& material) noexcept
{
    std::exchange(m_name, material.m_name);
    std::exchange(m_pBaseColor, material.m_pBaseColor);
    std::exchange(m_pNormal, material.m_pNormal);
    std::exchange(m_pOccRoughMetal, material.m_pOccRoughMetal);
    std::exchange(m_AOMap, material.m_AOMap);
    std::exchange(m_pEmissive, material.m_pEmissive);
    std::exchange(m_fileGuid, material.m_fileGuid);
    std::exchange(m_baseColorTexName, material.m_baseColorTexName);
    std::exchange(m_normalTexName, material.m_normalTexName);
    std::exchange(m_ORM_TexName, material.m_ORM_TexName);
    std::exchange(m_AO_TexName, material.m_AO_TexName);
    std::exchange(m_EmissiveTexName, material.m_EmissiveTexName);
    m_materialGuid = std::move(material.m_materialGuid);
    m_renderingMode = std::move(material.m_renderingMode);
    m_materialInfo = std::move(material.m_materialInfo);
    m_flowInfo = std::move(material.m_flowInfo);
    m_shaderMetaGuid = std::exchange(material.m_shaderMetaGuid, {});
	m_shaderMetaHandle = std::exchange(material.m_shaderMetaHandle, {});
    m_propertyValues = std::move(material.m_propertyValues);
    m_keywordSelections = std::move(material.m_keywordSelections);
    m_runtimeSchema = std::move(material.m_runtimeSchema);
    m_cbufferValues = std::move(material.m_cbufferValues);
	m_dirtyCBs = std::move(material.m_dirtyCBs);
}

Material::~Material()
{
}

std::shared_ptr<Material> Material::InstantiateShared(const Material* origin, std::string_view newName)
{
	if (!origin)
		return nullptr;

	// Create a new Material instance
	auto cloneMaterial = std::make_shared<Material>(*origin);

	const std::string cloneSuffix = "_Clone";

	// Determine the base name depending on whether a new name was provided
	std::string baseName = newName.empty() ? std::string(origin->m_name) : std::string(newName);

	auto stripCloneSuffix = [&](std::string& name)
	{
		auto pos = name.rfind(cloneSuffix);
		if (pos != std::string::npos)
		{
			auto digitsPos = pos + cloneSuffix.size();
			if (digitsPos == name.size() ||
				std::all_of(name.begin() + digitsPos, name.end(), [](unsigned char c) { return std::isdigit(c); }))
			{
				name.erase(pos);
			}
		}
	};

	// If no name was provided, start with the base name plus the clone suffix
	std::string finalName;
	if (newName.empty())
	{
		stripCloneSuffix(baseName);
		finalName = baseName + cloneSuffix;
	}
	else
	{
		finalName = baseName;
	}

	// Ensure the name is unique and avoid nested clone suffixes
	std::lock_guard<std::mutex> materialCacheGuard(DataSystems->m_materialMutex);
	if (DataSystems->Materials.contains(finalName))
	{
		stripCloneSuffix(baseName);
		finalName = baseName + cloneSuffix;
		int cloneIndex = 0;
		while (DataSystems->Materials.contains(finalName))
		{
			finalName = baseName + cloneSuffix + std::to_string(++cloneIndex);
		}
	}

	cloneMaterial->m_name = finalName;

	// 캐시에도 등록해 에디터·직렬화가 이름으로 찾을 수 있게 한다.
	// 캐시가 정리되더라도 호출자가 반환된 shared_ptr을 보관하는 한 클론은 살아 있다.
	DataSystems->Materials[cloneMaterial->m_name] = cloneMaterial;

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

Material& Material::UseBaseColorMap(Texture* texture)
{
	m_pBaseColor = texture;
	m_materialInfo.m_useBaseColor = true;

	return *this;
}

Material& Material::UseNormalMap(Texture* texture)
{
	m_pNormal = texture;
	m_materialInfo.m_useNormalMap = USE_NORMAL_MAP;

	return *this;
}

Material& Material::UseBumpMap(Texture* texture)
{
	m_pNormal = texture;
	m_materialInfo.m_useNormalMap = USE_BUMP_MAP;
	return *this;
}

Material& Material::UseOccRoughMetalMap(Texture* texture)
{
	m_pOccRoughMetal = texture;
	m_materialInfo.m_useOccRoughMetal = true;

	return *this;
}

Material& Material::UseAOMap(Texture* texture)
{
	m_AOMap = texture;
	m_materialInfo.m_useAOMap = true;

	return *this;
}

Material& Material::UseEmissiveMap(Texture* texture)
{
	m_pEmissive = texture;
	m_materialInfo.m_useEmissive = true;
	
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

bool Material::ConfigureShaderProperties(const ShaderMeta& meta,
    const ShaderMetaBindingLayout& layout, std::string& outError,
	ShaderMetaHandle shaderMetaHandle)
{
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

    auto runtimeSchema = std::make_shared<RuntimeSchema>();
    runtimeSchema->layout = layout;
    runtimeSchema->keywords = meta.keywords;

    std::vector<MaterialPropertyValue> values;
    values.reserve(meta.properties.size());
    std::vector<std::uint8_t> constantBuffer(layout.constantBufferByteSize, 0);
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

        if (!ValidateLogicalValue(desc, value, outError)
            || !PackProperty(desc, *binding, value, constantBuffer, outError))
        {
            if (outError.empty())
                outError = "Material texture property binding 종류가 다르다: " + desc.name;
            return false;
        }
        values.push_back(std::move(value));
    }

    std::vector<std::uint16_t> selections(meta.keywords.size(), 0);
    for (std::size_t index = 0;
        index < selections.size() && index < m_keywordSelections.size(); ++index)
    {
        if (m_keywordSelections[index] < meta.keywords[index].values.size())
            selections[index] = m_keywordSelections[index];
    }

    m_shaderMetaGuid = meta.guid;
    m_propertyValues = std::move(values);
    m_keywordSelections = std::move(selections);
    m_runtimeSchema = std::move(runtimeSchema);
	m_shaderMetaHandle = shaderMetaHandle;
    m_cbufferValues.clear();
    m_dirtyCBs.clear();
    if (!layout.constantBufferName.empty())
    {
        m_cbufferValues.emplace(layout.constantBufferName, std::move(constantBuffer));
        m_dirtyCBs.insert(layout.constantBufferName);
    }
    outError.clear();
    return true;
}

void Material::ResetShaderRuntime()
{
	m_runtimeSchema.reset();
	m_shaderMetaHandle = {};
	m_dirtyCBs.clear();
}

const ShaderMetaBindingLayout* Material::GetShaderBindingLayout() const
{
    return m_runtimeSchema ? &m_runtimeSchema->layout : nullptr;
}

std::span<const std::uint8_t> Material::GetConstantBufferData() const
{
    if (!m_runtimeSchema || m_runtimeSchema->layout.constantBufferName.empty()) return {};
    const auto found = m_cbufferValues.find(m_runtimeSchema->layout.constantBufferName);
    return found == m_cbufferValues.end()
        ? std::span<const std::uint8_t>{}
        : std::span<const std::uint8_t>{ found->second };
}

bool Material::TrySetTextureGuid(std::string_view property, const FileGuid& guid)
{
    const VarView view = FindProperty(property);
    if (!view.binding || ShaderPropertyType::Texture2D != view.binding->propertyType)
        return false;
    m_propertyValues[view.propertyIndex].m_textureGuid = guid;
    return true;
}

bool Material::TryGetTextureGuid(std::string_view property, FileGuid& outGuid) const
{
    const VarView view = FindProperty(property);
    if (!view.binding || ShaderPropertyType::Texture2D != view.binding->propertyType)
        return false;
    outGuid = m_propertyValues[view.propertyIndex].m_textureGuid;
    return true;
}

bool Material::TrySetKeywordSelection(std::string_view axis, std::string_view value)
{
    if (!m_runtimeSchema) return false;
    for (std::size_t axisIndex = 0; axisIndex < m_runtimeSchema->keywords.size(); ++axisIndex)
    {
        const ShaderKeywordAxis& keyword = m_runtimeSchema->keywords[axisIndex];
        if (keyword.name != axis) continue;
        const auto found = std::find(keyword.values.begin(), keyword.values.end(), value);
        if (found == keyword.values.end()) return false;
        m_keywordSelections[axisIndex] = static_cast<std::uint16_t>(
            std::distance(keyword.values.begin(), found));
        return true;
    }
    return false;
}

Material::VarView Material::FindVar(std::string_view cb, std::string_view var) const
{
    if (!m_runtimeSchema || cb != m_runtimeSchema->layout.constantBufferName) return {};
    return FindProperty(var);
}

Material::VarView Material::FindProperty(std::string_view property) const
{
    if (!m_runtimeSchema) return {};
    const ShaderMetaPropertyBinding* binding = FindBinding(m_runtimeSchema->layout, property);
    if (!binding) return {};
    const auto value = std::find_if(m_propertyValues.begin(), m_propertyValues.end(),
        [&](const MaterialPropertyValue& candidate)
        {
            return candidate.m_name == property;
        });
    if (value == m_propertyValues.end()) return {};
    return { binding, static_cast<std::size_t>(value - m_propertyValues.begin()) };
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
    if (!v.binding || !src || v.propertyIndex >= m_propertyValues.size()) return false;
    if (size != LogicalByteSize(v.binding->propertyType)
        || size > v.binding->byteSize) return false;
    auto it = m_cbufferValues.find(v.binding->resourceName);
    if (it == m_cbufferValues.end()) return false;

    auto& bytes = it->second;
    if (v.binding->byteOffset + size > bytes.size()) return false;
    std::memcpy(bytes.data() + v.binding->byteOffset, src, size);

    MaterialPropertyValue& value = m_propertyValues[v.propertyIndex];
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

    m_dirtyCBs.insert(v.binding->resourceName);
    return true;
}

bool Material::ReadBytes(const VarView& v, void* dst, size_t size) const
{
    if (!v.binding || !dst || size > LogicalByteSize(v.binding->propertyType)) return false;
    auto it = m_cbufferValues.find(v.binding->resourceName);
    if (it == m_cbufferValues.end()) return false;

    const auto& bytes = it->second;
    if (v.binding->byteOffset + size > bytes.size()) return false;
    std::memcpy(dst, bytes.data() + v.binding->byteOffset, size);
    return true;
}

// ��������������������������������������������������������������
// Ÿ�� ���� Setter / Getter
// ��������������������������������������������������������������
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
    // HLSL cbuffer bool�� 4����Ʈ ������ �����Ƿ� int�� ����
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
    // �ִ� 16����Ʈ�� float4�� �о ������ (var size�� 8/12�� �պκи� ��ȿ)
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

// ���� Qualified name sugar ("CB.Var") ����
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

void Material::TrySetMaterialInfo()
{
    TrySetVector("PBRMaterial", "gAlbedo", m_materialInfo.m_baseColor.rgba());
	TrySetFloat("PBRMaterial", "gMetallic", m_materialInfo.m_metallic);
	TrySetFloat("PBRMaterial", "gRoughness", m_materialInfo.m_roughness);

	TrySetInt("PBRMaterial", "gUseAlbedoMap", m_materialInfo.m_useBaseColor);
    TrySetInt("PBRMaterial", "gUseOccMetalRough", m_materialInfo.m_useOccRoughMetal);
    TrySetInt("PBRMaterial", "gUseAoMap", m_materialInfo.m_useAOMap);
    TrySetInt("PBRMaterial", "gUseEmmisive", m_materialInfo.m_useEmissive);
    TrySetInt("PBRMaterial", "gNormalState", m_materialInfo.m_useNormalMap);
    TrySetInt("PBRMaterial", "gConvertToLinear", m_materialInfo.m_convertToLinearSpace);

	TrySetFloat("PBRMaterial", "gIOR", m_materialInfo.m_IOR);
}

