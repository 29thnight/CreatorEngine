#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "../RenderEngine/Skeleton.h"
#include "Animator.generated.h"

constexpr uint32 MAX_BONES{ 512 };

class AnimationController;
class Animator : public Component, public IRenderable
{
public:
    [[Property]]
    Skeleton* m_Skeleton{ nullptr };
    [[Property]]
    float m_TimeElapsed{};
    [[Property]]
    uint32_t m_AnimIndexChosen{};
    DirectX::XMMATRIX m_FinalTransforms[MAX_BONES]{};
    [[Property]]
    std::string curAniName;
    

    //현재애니 이름반환
    std::string GetcurAnimation();
   
    bool m_isBlend = false;
    float blendT = 0;
    [[Property]]
    bool m_isLoof{ false };
    [[Property]]
    int m_AnimIndex{};
    int nextAnimIndex = -1;
    [[Property]]
    float m_nextTimeElapsed{};
    void SetAnimation(int index);
    [[Method]]
    void UpdateAnimation();
    [[Property]]
    FileGuid m_Motion{};
    
    [[Property]]
    AnimationController* m_animationController{};
    void CreateController();
    AnimationController* GetController() { return m_animationController; }
public:
   ReflectAnimator
    [[Serializable(Inheritance:Component)]]
   Animator() 
   {
       m_name = "Animator"; m_typeID = TypeTrait::GUIDCreator::GetTypeID<Animator>();
   } 
   virtual ~Animator()
   {
       if (m_animationController)
       {
           delete m_animationController;
       }
   }

    XMMATRIX blendtransform;
    bool IsEnabled() const override
    {
        return m_IsEnabled;
    }

    void SetEnabled(bool able) override
    {
        m_IsEnabled = able;
    }

private:
    bool m_IsEnabled = false;
    
};
