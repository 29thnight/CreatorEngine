#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "../RenderEngine/Mesh.h"
#include "../RenderEngine/Material.h"
#include "../RenderEngine/Texture.h"
#include "../RenderEngine/Skeleton.h"

constexpr uint32 MAX_BONES{ 512 };

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

class Animator;
class MeshRenderer : public Component, public IRenderable
{
public:
	GENERATED_BODY(MeshRenderer)

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

class SpriteRenderer : public Component, public IRenderable
{
public:
	Texture* m_Sprite = nullptr;

public:
	GENERATED_BODY(SpriteRenderer)

	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}

	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}

	ReflectionFieldInheritance(SpriteRenderer, Component)
	{
		PropertyField
		({
			meta_property(m_Sprite)
		});

		FieldEnd(SpriteRenderer, PropertyOnlyInheritance)
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
	GENERATED_BODY(Animator)

	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}

	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}

	ReflectionFieldInheritance(Animator, Component)
	{
		PropertyField
		({
			meta_property(m_Skeleton)
			meta_property(m_TimeElapsed)
			meta_property(m_AnimIndexChosen)
		});

		FieldEnd(Animator, PropertyOnlyInheritance)
	}

private:
	bool m_IsEnabled = false;
};