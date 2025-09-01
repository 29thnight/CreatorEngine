#pragma once
#include "MaterialInfomation.h"
#include "MaterialFlowInformation.h"
#include "Texture.h"
#include "ShaderPSO.h"
#include "Material.generated.h"
#include <unordered_map>
#include <vector>
#include <cstdint>

enum class MaterialRenderingMode
{
	Opaque,
	Transparent,
};

AUTO_REGISTER_ENUM(MaterialRenderingMode);

class Material
{
public:
   ReflectMaterial
    [[Serializable]]
	Material();
	Material(const Material& material);
	Material(Material&& material) noexcept;
	~Material();

	bool operator==(const Material& other) const
	{
		return m_materialGuid == other.m_materialGuid;
	}

	static Material* Instantiate(const Material* origin, std::string_view newName = {});
	static std::shared_ptr<Material> InstantiateShared(const Material* origin, std::string_view newName = {});

//initialize material chainable functions
public:
	Material& SetBaseColor(Mathf::Color3 color);
	Material& SetBaseColor(float r, float g, float b);
	Material& SetMetallic(float metallic);
	Material& SetRoughness(float roughness);

public:
	Material& UseBaseColorMap(Texture* texture);
	Material& UseNormalMap(Texture* texture);
	Material& UseBumpMap(Texture* texture);
	Material& UseOccRoughMetalMap(Texture* texture);
	Material& UseAOMap(Texture* texture);
	Material& UseEmissiveMap(Texture* texture);
	Material& ConvertToLinearSpace(bool32 convert);
	Material& SetWindVector(const Mathf::Vector4& windVector);
	Material& SetUVScroll(const Mathf::Vector2& uvScroll);

	void SetShaderPSO(std::shared_ptr<ShaderPSO> pso);
	std::shared_ptr<ShaderPSO> GetShaderPSO() const;

public:
    [[Property]]
	std::string m_name{};
	[[Property]]
	std::string m_baseColorTexName{};
	Texture* m_pBaseColor{ nullptr };
	[[Property]]
	std::string m_normalTexName{};
	Texture* m_pNormal{ nullptr };
	[[Property]]
	std::string m_ORM_TexName{};
	Texture* m_pOccRoughMetal{ nullptr };
	[[Property]]
	std::string m_AO_TexName{};
	Texture* m_AOMap{ nullptr };
	[[Property]]
	std::string m_EmissiveTexName{};
	Texture* m_pEmissive{ nullptr };
    [[Property]]
	MaterialInfomation m_materialInfo;
	[[Property]]
	MaterialFlowInformation m_flowInfo;
    [[Property]]
	FileGuid m_fileGuid{};
    [[Property]]
	MaterialRenderingMode m_renderingMode{ MaterialRenderingMode::Opaque };
	HashedGuid m_materialGuid{ make_guid() };
        std::shared_ptr<ShaderPSO> m_shaderPSO{ nullptr };
        [[Property]]
        FileGuid m_shaderPSOGuid{};
        const std::unordered_map<std::string, ShaderPSO::CBEntry>* m_cbMeta{ nullptr };
        [[Property]]
        std::unordered_map<std::string, std::vector<uint8_t>> m_cbufferValues{};

};

