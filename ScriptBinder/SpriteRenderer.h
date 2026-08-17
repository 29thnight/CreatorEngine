#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "LightMapping.h"
#include "BillboardType.h"
#include "Texture.h"

class SpriteRenderer : public meta::identity<SpriteRenderer, Component>
{
public:
   static consteval auto describe()
   {
       return meta::describe<SpriteRenderer>(
           meta::base<Component>(),
           meta::member<&SpriteRenderer::m_SpritePath>(),
           meta::member<&SpriteRenderer::m_orderInLayer>(),
           meta::member<&SpriteRenderer::m_billboardAxis>(),
           meta::member<&SpriteRenderer::m_billboardType>(),
           meta::member<&SpriteRenderer::m_enableDepth>());
   }
    SpriteRenderer() = default;

   virtual void Awake() override;
   virtual void OnDestroy() override;

   void SetSprite(const std::shared_ptr<Texture>& ptr);
   void OnDeserialized(); // CT6-d: 스프라이트 텍스처 로드(구 팩토리 분기)


   const std::shared_ptr<Texture>& GetSprite() const { return m_Sprite; }
   void SetBillboardType(BillboardType type) { m_billboardType = type; }
   BillboardType GetBillboardType() const noexcept { return m_billboardType; }
   void SetBillboardAxis(const Mathf::Vector3& axis) { m_billboardAxis = axis; }
   const Mathf::Vector3& GetBillboardAxis() const noexcept { return m_billboardAxis; }

   bool IsEnableDepth() const { return m_enableDepth; }
   void SetEnableDepth(bool enable) { m_enableDepth = enable; }
   int GetOrderInLayer() const { return m_orderInLayer; }

private:
	friend class ComponentFactory;
    std::string m_SpritePath{};
    int m_orderInLayer{ 0 };
    Mathf::Vector3 m_billboardAxis{ 0.f, 1.f, 0.f };
    std::shared_ptr<Texture> m_Sprite = nullptr;
    BillboardType m_billboardType{ BillboardType::None };
	bool m_enableDepth{ false };
};
