#pragma once
#include "Ownership.h"
#include <mathematics/vector3.hpp>
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "LightMapping.h"
#include "BillboardType.h"
#include "Texture.h"

class [[reflgen::reflect]] SpriteRenderer : public meta::identity<SpriteRenderer, Component>
{
   friend struct reflgen::access;
   public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

public:
    SpriteRenderer() = default;

   virtual void OnInitialized() override;
   void OnAddedToScene() override;
   void OnRemovingFromScene() override;
   virtual void OnUninitializing() override;

   void SetSprite(const own::shared_owner<const Texture>& ptr);
   void OnDeserialized(); // CT6-d: 스프라이트 텍스처 로드(구 팩토리 분기)


   const own::shared_owner<const Texture>& GetSprite() const { return m_Sprite; }
   void SetBillboardType(BillboardType type) { m_billboardType = type; PublishRenderProxyDirty(ProxyDirty::Payload); }
   BillboardType GetBillboardType() const noexcept { return m_billboardType; }
   void SetBillboardAxis(const math::vector3& axis) { m_billboardAxis = axis; PublishRenderProxyDirty(ProxyDirty::Payload); }
   const math::vector3& GetBillboardAxis() const noexcept { return m_billboardAxis; }

   bool IsEnableDepth() const { return m_enableDepth; }
   void SetEnableDepth(bool enable) { m_enableDepth = enable; PublishRenderProxyDirty(ProxyDirty::Visibility); }
   int GetOrderInLayer() const { return m_orderInLayer; }

private:
	friend class ComponentFactory;
    std::string m_SpritePath{};
    int m_orderInLayer{ 0 };
    math::vector3 m_billboardAxis{ 0.f, 1.f, 0.f };

    [[reflgen::ignore]]
    own::shared_owner<const Texture> m_Sprite = nullptr;

    BillboardType m_billboardType{ BillboardType::None };
	bool m_enableDepth{ false };
};
