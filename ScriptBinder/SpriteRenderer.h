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

    //ReflectionFieldInheritance(SpriteRenderer, Component)
    //{
    //    PropertyField
    //    ({
    //        meta_property(m_Sprite)
    //    });

    //    FieldEnd(SpriteRenderer, PropertyOnlyInheritance)
    //}

private:
    [[Property]]
    bool m_IsEnabled = false;
};
