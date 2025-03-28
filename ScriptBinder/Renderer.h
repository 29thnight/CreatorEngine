#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"

constexpr uint32 MAX_BONES{ 512 };

class Mesh;
class Material;
class Texture;
class Skeleton;
class Animator;

class MeshRenderer : public Component, public IRenderable
{
public:
	Material* m_Material;
	Mesh* m_Mesh;
	Animator* m_Animator = nullptr;

public:
	std::string ToString() const override;
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