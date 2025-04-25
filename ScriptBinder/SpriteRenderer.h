#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "LightMapping.h"
#include "../RenderEngine/Texture.h"
#include "SpriteRenderer.generated.h"

class SpriteRenderer : public Component, public IRenderable
{
public:
    [[Property]]
    Texture* m_Sprite = nullptr;

public:
   ReflectSpriteRenderer
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(SpriteRenderer)

    bool IsEnabled() const override
    {
        return m_IsEnabled;
    }

    void SetEnabled(bool able) override
    {
        m_IsEnabled = able;
    }

	bool IsGizmoEnabled() const
	{
		return m_IsGizmoEnabled;
	}

    SpriteRenderer& SetGizmoEnabled(bool able)
    {
		m_IsGizmoEnabled = able;

		return *this;
    }

private:
    [[Property]]
    bool m_IsEnabled = false;
	bool m_IsGizmoEnabled = false;
};
