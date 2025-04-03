#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"

struct LightMapping {
	int lightmapIndex{ -1 };
	int ligthmapResolution{ 0 };
	Mathf::Vector2 lightmapOffset{ 0,0 };
	Mathf::Vector2 lightmapTiling{ 0,0 };
};

constexpr uint32 MAX_BONES{ 512 };

class Mesh;
class Material;
class Texture;
class Skeleton;
class Animator;

class MeshRenderer : public Component, public IRenderable
{
public:
	Material* m_Material{ nullptr };
	Mesh* m_Mesh{ nullptr };
	Animator* m_Animator{ nullptr };

	LightMapping m_LightMapping;
public:
	MeshRenderer() meta_default(MeshRenderer)
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

	static const Meta::Type& Reflect()
	{
		static const Meta::MetaProperties<1> properties
		{
			Meta::MakeProperty("m_Material", &MeshRenderer::m_Material)
		};

		static const Meta::Type type
		{
			"MeshRenderer",
			properties
		};

		return type;
	}

private:
	bool m_IsEnabled{ false };
};

class SpriteRenderer : public Component, public IRenderable
{
public:
	Texture* m_Sprite = nullptr;

public:
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

private:
	bool m_IsEnabled = false;
};

class Animator : public Component, public IRenderable
{
public:
	Skeleton* m_Skeleton = nullptr;
	float m_TimeElapsed = 0;
	uint32_t m_AnimIndexChosen = 0;
	DirectX::XMMATRIX m_FinalTransforms[MAX_BONES];

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

private:
	bool m_IsEnabled = false;
};