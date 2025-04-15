#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "../RenderEngine/Mesh.h"
#include "../RenderEngine/Material.h"
#include "../RenderEngine/Texture.h"
#include "../RenderEngine/Skeleton.h"

struct LightMapping 
{
	int lightmapIndex{ -1 };
	int ligthmapResolution{ 0 };
	float lightmapScale{ 1.f };
	Mathf::Vector2 lightmapOffset{ 0,0 };
	Mathf::Vector2 lightmapTiling{ 0,0 };

	LightMapping() meta_default(LightMapping)
	~LightMapping() = default;

	ReflectionField(LightMapping)
	{
		PropertyField
		({
			meta_property(lightmapIndex)
			meta_property(ligthmapResolution)
			meta_property(lightmapScale)
			meta_property(lightmapOffset)
			meta_property(lightmapTiling)
		});

		FieldEnd(LightMapping, PropertyOnly)
	}
};

constexpr uint32 MAX_BONES{ 512 };

//class Mesh;
//class Material;
//class Texture;
//class Skeleton;
class Animator;
class MeshRenderer : public Component, public IRenderable, public Meta::IReflectable<MeshRenderer>
{
public:
	GENERATED_BODY(MeshRenderer)

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

	ReflectionFieldInheritance(MeshRenderer, Component)
	{
		PropertyField
		({
			meta_property(m_Material)
			meta_property(m_Mesh)
			meta_property(m_Animator)
			meta_property(m_LightMapping)
		});

		FieldEnd(MeshRenderer, PropertyOnlyInheritance)
	}

public:
	Material* m_Material{ nullptr };
	Mesh* m_Mesh{ nullptr };
	Animator* m_Animator{ nullptr };
	LightMapping m_LightMapping;

private:
	bool m_IsEnabled{ false };
};

class SpriteRenderer : public Component, public IRenderable, public Meta::IReflectable<SpriteRenderer>
{
public:
	Texture* m_Sprite = nullptr;

public:
	GENERATED_BODY(SpriteRenderer)

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

	ReflectionField(SpriteRenderer)
	{
		PropertyField
		({
			meta_property(m_Sprite)
		});

		FieldEnd(SpriteRenderer, PropertyOnly)
	}

private:
	bool m_IsEnabled = false;
};

class Animator : public Component, public IRenderable, public Meta::IReflectable<Animator>
{
public:
	Skeleton* m_Skeleton{ nullptr };
	float m_TimeElapsed{};
	uint32_t m_AnimIndexChosen{};
	DirectX::XMMATRIX m_FinalTransforms[MAX_BONES]{};

public:
	GENERATED_BODY(Animator)

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

	ReflectionField(Animator)
	{
		PropertyField
		({
			meta_property(m_Skeleton)
			meta_property(m_TimeElapsed)
			meta_property(m_AnimIndexChosen)
		});

		FieldEnd(Animator, PropertyOnly)
	}

private:
	bool m_IsEnabled = false;
};