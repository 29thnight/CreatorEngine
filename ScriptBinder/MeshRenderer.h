#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "LightMapping.h"
#include "../RenderEngine/Mesh.h"
#include "../RenderEngine/Material.h"
#include "MeshRenderer.generated.h"

class Animator;
class MeshRenderer : public Component, public IRenderable
{
public:
   ReflectMeshRenderer
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(MeshRenderer)

    bool IsEnabled() const override
    {
        return m_IsEnabled;
    }

    void SetEnabled(bool able) override
    {
        m_IsEnabled = able;
    }

public:
    [[Property]]
    Material* m_Material{ nullptr };
    [[Property]]
    Mesh* m_Mesh{ nullptr };
    [[Property]]
    Animator* m_Animator{ nullptr };
    [[Property]]
    LightMapping m_LightMapping;

private:
    [[Property]]
    bool m_IsEnabled{ false };
};
