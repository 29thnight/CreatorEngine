#pragma once
#include "Core.Minimal.h"
#include "Texture.h"

constexpr bool32 USE_NORMAL_MAP = 1;
constexpr bool32 USE_BUMP_MAP = 2;

struct alignas(16) MaterialInfomation
{
	Mathf::Color4 m_baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
	float		  m_metallic{ 0.0f };
	float		  m_roughness{ 1.0f };
	bool32		  m_useBaseColor{};
	bool32		  m_useOccRoughMetal{};
	bool32		  m_useAOMap{};
	bool32		  m_useEmissive{};
	bool32		  m_useNormalMap{};
	bool32		  m_convertToLinearSpace{};
};

class Material
{
public:
	enum class RenderingMode
	{
		Opaque,
		Transparent,
	};

public:
	Material() = default;
	Material(const Material& material) = delete;
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
	Texture* m_pBaseColor{ nullptr };
	Texture* m_pNormal{ nullptr };
	Texture* m_pOccRoughMetal{ nullptr };
	Texture* m_AOMap{ nullptr };
	Texture* m_pEmissive{ nullptr };

	MaterialInfomation m_materialInfo;
	RenderingMode m_renderingMode{ RenderingMode::Opaque };
};