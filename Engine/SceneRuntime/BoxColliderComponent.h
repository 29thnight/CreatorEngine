#pragma once  
#include "Component.h"
#include "Scene.h"  
#include "SceneManager.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"

class BoxColliderComponent : public meta::identity<BoxColliderComponent, Component>, public ICollider
{
   public:
   // CT4 파일럿: generated.h + Serializable/Property 어노테이션을 P2996 유사
   // 매크로 프리 표기로 대체. 멤버 순서 = 구 generated.h(골든 diff 0의 전제).
   // 주의: 주석에도 이중 대괄호 어노테이션 원문을 쓰지 말 것 — 생성기가
   // regex_search로 줄을 훑는다.
       // 마찰·반발 계수의 range는 속성 표기의 살아있는 예시다 — 어댑터는
       // 무시하고(골든 무영향), CT6 인스펙터가 슬라이더 한계로 소비한다.
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_boxExtent>,
           meta::field<&Self::m_posOffset>,
           meta::field<&Self::m_rotOffset>,
           meta::field<&Self::staticFriction>.with(
               meta::range(0.0f, 1.0f)),
           meta::field<&Self::dynamicFriction>.with(
               meta::range(0.0f, 1.0f)),
           meta::field<&Self::restitution>.with(
               meta::range(0.0f, 1.0f)),
           meta::field<&Self::density>);
   }
public:

    BoxColliderComponent()
   {
        m_Info.boxExtent = { 1.0f, 1.0f, 1.0f };
		m_boxExtent = m_Info.boxExtent;
        m_type = EColliderType::COLLISION; // 기본값 설정
   } 
   virtual ~BoxColliderComponent() = default;

   void OnInitialized() override  
    {  
        auto scene = GetOwner()->m_ownerScene;  
        if (scene)  
        {  
            if (m_boxExtent != math::vector3{})
            {
                m_Info.boxExtent = { m_boxExtent.x, m_boxExtent.y, m_boxExtent.z };
            }
            else {
                m_Info.boxExtent = { 0.001f, 0.001f ,0.001f };
            }
            scene->CollectColliderComponent(this);  
        }  
   }

   void OnUninitializing() override  
   {  
       auto scene = GetOwner()->m_ownerScene;  
       if (scene)  
       {  
           scene->UnCollectColliderComponent(this);  
       }  
   }

   math::vector3 m_boxExtent{ 1.0f, 1.0f, 1.0f };
   math::vector3 m_posOffset{};
   math::quaternion m_rotOffset{};

   float staticFriction = 0.5f;	//정적 물체 마찰 계수
   float dynamicFriction = 0.4f;	//동적 물체 마찰 계수
   float restitution = 0.3f;	//탄성 계수
   float density = 10.0f;	//밀도


   math::vector3 GetExtents()
   {  
      if (m_boxExtent != math::vector3{})
      {  
          m_Info.boxExtent = { m_boxExtent.x, m_boxExtent.y, m_boxExtent.z };
	  }
	  
      return m_Info.boxExtent;  
   }

   void SetExtents(const math::vector3& extents)
   {  
       m_Info.boxExtent = extents;  
       m_boxExtent = m_Info.boxExtent;
   }  
   EColliderType GetColliderType() const override
   {
       return m_type;
   }

   void SetColliderType(EColliderType type) override
   {
       m_type = type;
   }

   BoxColliderInfo GetBoxInfo()
   {
	   if (m_boxExtent != math::vector3{})
	   {
		   m_Info.boxExtent = { m_boxExtent.x, m_boxExtent.y, m_boxExtent.z };
	   }
       else {
           m_Info.boxExtent = { 0.001f, 0.001f ,0.001f };
       }

       // 임시 콜리젼 레이어
       m_Info.colliderInfo.layerNumber = GetOwner()->m_collisionType;

       m_Info.colliderInfo.staticFriction = staticFriction;
       m_Info.colliderInfo.dynamicFriction = dynamicFriction;
       m_Info.colliderInfo.restitution = restitution;
       m_Info.colliderInfo.density = density;


	   return m_Info;
   }

   void SetBoxInfoMation(const BoxColliderInfo& info)  
   {  
       m_Info = info;  
	   m_boxExtent = m_Info.boxExtent;
   }  

   float GetStaticFriction() const
   {
	   return m_Info.colliderInfo.staticFriction;
   }

   void SetStaticFriction(float staticFriction)
   {
	   m_Info.colliderInfo.staticFriction = staticFriction;
   }

   float GetDynamicFriction() const
   {
	   return m_Info.colliderInfo.dynamicFriction;
   }

   void SetDynamicFriction(float dynamicFriction)
   {
	   m_Info.colliderInfo.dynamicFriction = dynamicFriction;
   }

   float   GetRestitution() const
   {
	   return m_Info.colliderInfo.restitution;
   }

   void SetRestitution(float restitution)
   {
	   m_Info.colliderInfo.restitution = restitution;
   }

   float GetDensity() const
   {
	   return m_Info.colliderInfo.density;
   }

   void SetDensity(float density)
   {
	   m_Info.colliderInfo.density = density;
   }
   
   //=========================================================

    // ICollider을(를) 통해 상속됨
    void SetPositionOffset(math::vector3 pos) override;

    math::vector3 GetPositionOffset() override;

    void SetRotationOffset(math::quaternion rotation) override;

    math::quaternion GetRotationOffset() override;
    
    BoxColliderInfo m_Info;
private:  

    void OnTriggerEnter(ICollider* other) override;

    void OnTriggerStay(ICollider* other) override;

    void OnTriggerExit(ICollider* other) override;

    void OnCollisionEnter(ICollider* other) override;

    void OnCollisionStay(ICollider* other) override;

    void OnCollisionExit(ICollider* other) override;

    EColliderType m_type;
	unsigned int m_collsionCount = 0;
};
