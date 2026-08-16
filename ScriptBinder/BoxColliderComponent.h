#pragma once  
#include "Component.h"
#include "Scene.h"  
#include "SceneManager.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"

class BoxColliderComponent : public Component, public ICollider
{
public:
   // CT4 파일럿: generated.h + Serializable/Property 어노테이션을 명시 메타 한
   // 표기로 대체. 멤버 목록 순서 = 구 generated.h의 프로퍼티 순서(골든 diff 0의
   // 전제). 주의: 주석에도 이중 대괄호 어노테이션 원문을 쓰지 말 것 — 생성기가
   // regex_search로 줄을 훑는다.
   ReflectionMetaFieldInheritance(BoxColliderComponent, Component,
       ct_property(m_boxExtent),
       ct_property(m_posOffset),
       ct_property(m_rotOffset),
       ct_property(staticFriction),
       ct_property(dynamicFriction),
       ct_property(restitution),
       ct_property(density))

    BoxColliderComponent()
   {
        m_name = "BoxColliderComponent"; m_typeID = TypeTrait::GUIDCreator::GetTypeID<BoxColliderComponent>();
        m_Info.boxExtent = { 1.0f, 1.0f, 1.0f };
		m_boxExtent = m_Info.boxExtent;
        m_type = EColliderType::COLLISION; // �⺻�� ����
   } 
   virtual ~BoxColliderComponent() = default;

   void Awake() override  
    {  
        auto scene = GetOwner()->m_ownerScene;  
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

   void OnDestroy() override  
   {  
       auto scene = GetOwner()->m_ownerScene;  
       if (scene)  
       {  
           scene->UnCollectColliderComponent(this);  
       }  
   }

   DirectX::SimpleMath::Vector3 m_boxExtent{ 1.0f, 1.0f, 1.0f };
   DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };  
   DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };  

   float staticFriction = 0.5f;	//���� ��ü ���� ���
   float dynamicFriction = 0.4f;	//���� ��ü ���� ���
   float restitution = 0.3f;	//ź�� ���
   float density = 10.0f;	//�е�


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
	   if (m_boxExtent != DirectX::SimpleMath::Vector3::Zero)
	   {
		   m_Info.boxExtent = { m_boxExtent.x, m_boxExtent.y, m_boxExtent.z };
	   }
       else {
           m_Info.boxExtent = { 0.001f, 0.001f ,0.001f };
       }

       // �ӽ� �ݸ��� ���̾�
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

    // ICollider��(��) ���� ��ӵ�
    void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override;

    DirectX::SimpleMath::Vector3 GetPositionOffset() override;

    void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override;

    DirectX::SimpleMath::Quaternion GetRotationOffset() override;
    
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
