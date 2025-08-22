#include "Material.h"
#include "DataSystem.h"

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
	m_materialInfo(material.m_materialInfo),
	m_fileGuid(material.m_fileGuid),
	m_baseColorTexName(material.m_baseColorTexName),
	m_normalTexName(material.m_normalTexName),
	m_ORM_TexName(material.m_ORM_TexName),
	m_AO_TexName(material.m_AO_TexName),
	m_EmissiveTexName(material.m_EmissiveTexName),
	m_flowInfo(material.m_flowInfo),
	m_shaderPSO(material.m_shaderPSO),
	m_renderingMode(material.m_renderingMode),
	m_shaderPSOGuid(material.m_shaderPSOGuid)
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
	m_shaderPSOGuid = std::move(material.m_shaderPSOGuid);
	m_renderingMode = std::move(material.m_renderingMode);
	m_materialInfo = std::move(material.m_materialInfo);
	m_flowInfo = std::move(material.m_flowInfo);
}

Material::~Material()
{
}

Material* Material::Instantiate(const Material* origin, std::string_view newName)
{
	return InstantiateShared(origin, newName).get();
}

std::shared_ptr<Material> Material::InstantiateShared(const Material* origin, std::string_view newName)
{
	if (!origin)
		return nullptr;

	// Create a new Material instance
	auto cloneMaterial = std::make_shared<Material>(*origin);

	// 수정된 코드
	if (newName.empty())
	{
		cloneMaterial->m_name = origin->m_name + "_Clone";
	}
	else
	{
		cloneMaterial->m_name = std::string(newName);
	}

	if(DataSystems->Materials.find(cloneMaterial->m_name) == DataSystems->Materials.end())
	{
		DataSystems->Materials[cloneMaterial->m_name] = cloneMaterial;
	}
	else
	{
		int cloneIndex = 1;
		while(DataSystems->Materials.find(cloneMaterial->m_name) != DataSystems->Materials.end())
		{
			cloneMaterial->m_name += "_Clone" + std::to_string(cloneIndex++);
		}
		DataSystems->Materials[cloneMaterial->m_name] = cloneMaterial;
	}

        DataSystems->SaveMaterial(cloneMaterial.get());

	return cloneMaterial;
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

Material& Material::SetWindVector(const Mathf::Vector4& windVector)
{
	m_flowInfo.m_windVector = windVector;

	return *this;
}

Material& Material::SetUVScroll(const Mathf::Vector2& uvScroll)
{
	m_flowInfo.m_uvScroll = uvScroll;

	return *this;
}

void Material::SetShaderPSO(std::shared_ptr<ShaderPSO> pso)
{
	if (pso)
	{
		m_shaderPSO = pso;
		m_shaderPSOGuid = pso->GetShaderPSOGuid();
	}
	else
	{
		m_shaderPSO.reset();
		m_shaderPSOGuid = {};
	}
}

std::shared_ptr<ShaderPSO> Material::GetShaderPSO() const
{
	return m_shaderPSO ? m_shaderPSO : nullptr;
}
