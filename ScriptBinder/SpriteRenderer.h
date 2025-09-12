#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "LightMapping.h"
#include "BillboardType.h"
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
   void SetCustomPSOName(const std::string& name) { m_CustomPSOName = name; }
   const std::string& GetCustomPSOName() const { return m_CustomPSOName; }
   void SetBillboardType(BillboardType type) { m_billboardType = type; }
   BillboardType GetBillboardType() const noexcept { return m_billboardType; }
   void SetBillboardAxis(const Mathf::Vector3& axis) { m_billboardAxis = axis; }
   const Mathf::Vector3& GetBillboardAxis() const noexcept { return m_billboardAxis; }

private:
    [[Property]]
    std::string m_SpritePath{};
    [[Property]]
    std::string m_CustomPSOName{};
    [[Property]]
    int m_orderInLayer{ 0 };
   [[Property]]
   BillboardType m_billboardType{ BillboardType::None };
   [[Property]]
   Mathf::Vector3 m_billboardAxis{ 0.f, 1.f, 0.f };

    std::shared_ptr<Texture> m_Sprite = nullptr;
};
