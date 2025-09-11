#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "LightMapping.h"
#include "SpriteRenderer.generated.h"
#include "Texture.h"

class SpriteRenderer : public Component, public RegistableEvent<SpriteRenderer>
{
public:
   ReflectSpriteRenderer
    [[Serializable(Inheritance:Component)]]
    GENERATED_BODY(SpriteRenderer)

   virtual void Awake() override;
   virtual void OnDestroy() override;

   void SetSprite(const std::shared_ptr<Texture>& ptr);
   void DeserializeSprite(const std::shared_ptr<Texture>& ptr);

   const std::shared_ptr<Texture>& GetSprite() const { return m_Sprite; }
   void SetVertexShaderName(const std::string& name) { m_VertexShaderName = name; }
   void SetPixelShaderName(const std::string& name) { m_PixelShaderName = name; }
   const std::string& GetVertexShaderName() const { return m_VertexShaderName; }
   const std::string& GetPixelShaderName() const { return m_PixelShaderName; }

private:
    [[Property]]
    std::string m_SpritePath{};
    [[Property]]
	std::string m_VertexShaderName{ "VertexShader" };
    [[Property]]
	std::string m_PixelShaderName{ "Sprite" };
    [[Property]]
    int m_orderInLayer{ 0 };

    std::shared_ptr<Texture> m_Sprite = nullptr;
};
