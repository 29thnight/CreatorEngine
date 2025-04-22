#pragma once
#include "MaterialInfomation.h"
#include "Texture.h"
#include "Material.generated.h"

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
	Material() = default;
	Material(const Material& material) = default;
	Material(Material&& material) noexcept;

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

public:
    [[Property]]
	std::string m_name{};
	Texture* m_pBaseColor{ nullptr };
	Texture* m_pNormal{ nullptr };
	Texture* m_pOccRoughMetal{ nullptr };
	Texture* m_AOMap{ nullptr };
	Texture* m_pEmissive{ nullptr };
    [[Property]]
	MaterialInfomation m_materialInfo;
    [[Property]]
	FileGuid m_fileGuid{};
    [[Property]]
	MaterialRenderingMode m_renderingMode{ MaterialRenderingMode::Opaque };
};

