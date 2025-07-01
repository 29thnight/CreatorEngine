#include "Material.h"

Material::Material()
{
}

Material::Material(const Material& material) :
	m_name(material.m_name),
	m_pBaseColor(material.m_pBaseColor),
	m_pNormal(material.m_pNormal),
	m_pOccRoughMetal(material.m_pOccRoughMetal),
	m_AOMap(material.m_AOMap),
	m_pEmissive(material.m_pEmissive),
	m_materialInfo(material.m_materialInfo)
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

	m_materialInfo = material.m_materialInfo;
}

Material::~Material()
{
}

Material& Material::SetBaseColor(Mathf::Color3 color)
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
