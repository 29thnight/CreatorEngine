#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "LightMapping.h"
#include "../RenderEngine/Texture.h"
#include "SpriteRenderer.generated.h"

class SpriteRenderer : public Component
{
public:
    [[Property]]
    Texture* m_Sprite = nullptr;

public:
   ReflectSpriteRenderer
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(SpriteRenderer)
};
