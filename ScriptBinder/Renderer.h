#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"

struct LightMapping 
{
	int lightmapIndex{ -1 };
	int ligthmapResolution{ 0 };
	float lightmapScale{ 1.f };
	Mathf::Vector2 lightmapOffset{ 0,0 };
	Mathf::Vector2 lightmapTiling{ 0,0 };

	LightMapping() meta_default(LightMapping)
	~LightMapping() = default;

	ReflectionField(LightMapping, PropertyOnly)
	{
		PropertyField
		({
			meta_property(lightmapIndex)
			meta_property(ligthmapResolution)
			Meta::MakeProperty("lightmapScale", &LightMapping::lightmapScale),
			meta_property(lightmapOffset)
			meta_property(lightmapTiling)
		});

		ReturnReflectionPropertyOnly(LightMapping)
	}
};

constexpr uint32 MAX_BONES{ 512 };

class Mesh;
class Material;
class Texture;
class Skeleton;
class Animator;

class MeshRenderer : public Component, public IRenderable, public Meta::IReflectable<MeshRenderer>
{
public:
	Material* m_Material{ nullptr };
	Mesh* m_Mesh{ nullptr };
	Animator* m_Animator{ nullptr };

	LightMapping m_LightMapping;
public:
	MeshRenderer() = default;
	~MeshRenderer() = default;

	std::string ToString() const override
	{
		return std::string("MeshRenderer");
	}
	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}
	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}

	ReflectionField(MeshRenderer, PropertyOnly)
	{
		PropertyField
		({
			meta_property(m_Material)
			meta_property(m_Mesh)
			meta_property(m_Animator)
			meta_property(m_LightMapping)
		});
		ReturnReflectionPropertyOnly(MeshRenderer)
	}

private:
	bool m_IsEnabled{ false };
};

class SpriteRenderer : public Component, public IRenderable, public Meta::IReflectable<SpriteRenderer>
{
public:
	Texture* m_Sprite = nullptr;

public:
	SpriteRenderer() = default;
	~SpriteRenderer() = default;

	std::string ToString() const override
	{
		return std::string("SpriteRenderer");
	}
	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}
	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}

	ReflectionField(SpriteRenderer, PropertyOnly)
	{
		PropertyField
		({
			meta_property(m_Sprite)
		});
		ReturnReflectionPropertyOnly(SpriteRenderer)
	}

private:
	bool m_IsEnabled = false;
};

class Animator : public Component, public IRenderable
{
public:
	Skeleton* m_Skeleton{ nullptr };
	float m_TimeElapsed{};
	uint32_t m_AnimIndexChosen{};
	DirectX::XMMATRIX m_FinalTransforms[MAX_BONES]{};

public:
	Animator() meta_default(Animator)
	~Animator() = default;

public:
	std::string ToString() const override
	{
		return std::string("Animator");
	}
	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}
	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}

	ReflectionField(Animator, PropertyOnly)
	{
		PropertyField
		({
			meta_property(m_Skeleton)
			meta_property(m_TimeElapsed)
			meta_property(m_AnimIndexChosen)
		});
		ReturnReflectionPropertyOnly(Animator)
	}

private:
	bool m_IsEnabled = false;
};