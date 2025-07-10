#pragma once  
#include "Component.h"  
#include "IAwakable.h"
#include "IOnDistroy.h"
#include "../physics/PhysicsCommon.h"  
#include "../Physics/ICollider.h"
#include "BoxColliderComponent.generated.h"

class BoxColliderComponent : public Component, public ICollider, public IAwakable, public IOnDistroy
{  
public:  
   ReflectBoxColliderComponent
	[[Serializable(Inheritance:Component)]]
    BoxColliderComponent() 
   {
        m_name = "BoxColliderComponent"; m_typeID = TypeTrait::GUIDCreator::GetTypeID<BoxColliderComponent>();
		m_type = EColliderType::COLLISION;
        m_Info.boxExtent = { 1.0f, 1.0f, 1.0f };
		m_boxExtent = m_Info.boxExtent;
   } 
   virtual ~BoxColliderComponent() = default;

   void Awake() override  
    {  
        auto scene = SceneManagers->GetActiveScene();  
        if (scene)  
        {  
            if (m_boxExtent != DirectX::SimpleMath::Vector3::Zero)
            {
                m_Info.boxExtent = { m_boxExtent.x, m_boxExtent.y, m_boxExtent.z };
            }
            else {
                m_Info.boxExtent = { 0.001f, 0.001f ,0.001f };
            }
            scene->CollectColliderComponent(this);  
        }  
   }

   void OnDistroy() override  
   {  
       auto scene = SceneManagers->GetActiveScene();  
       if (scene)  
       {  
           scene->UnCollectColliderComponent(this);  
       }  
   }

   [[Property]]  
   DirectX::SimpleMath::Vector3 m_boxExtent{ 1.0f, 1.0f, 1.0f };
   [[Property]]
   DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };  
   [[Property]]  
   DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };  

   DirectX::SimpleMath::Vector3 GetExtents()
   {  
      if (m_boxExtent != DirectX::SimpleMath::Vector3::Zero)  
      {  
          m_Info.boxExtent = { m_boxExtent.x, m_boxExtent.y, m_boxExtent.z };
	  }
	  
      return m_Info.boxExtent;  
   }

   void SetExtents(const DirectX::SimpleMath::Vector3& extents)  
   {  
       m_Info.boxExtent = extents;  
       m_boxExtent = m_Info.boxExtent;
   }  


   EColliderType GetColliderType() const  
   {  
	   return m_type;
   }  

   void SetColliderType(EColliderType type)
   {
	   m_type = type;
   }

   BoxColliderInfo GetBoxInfo()
   {
	   if (m_boxExtent != DirectX::SimpleMath::Vector3::Zero)
	   {
		   m_Info.boxExtent = { m_boxExtent.x, m_boxExtent.y, m_boxExtent.z };
	   }
       else {
           m_Info.boxExtent = { 0.001f, 0.001f ,0.001f };
       }

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
    void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override;

    DirectX::SimpleMath::Vector3 GetPositionOffset() override;

    void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override;

    DirectX::SimpleMath::Quaternion GetRotationOffset() override;

    /*void SetIsTrigger(bool isTrigger) override;
    bool GetIsTrigger() override;*/
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
