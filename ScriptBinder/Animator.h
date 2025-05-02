#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IUpdatable.h"
#include "Animator.generated.h"
constexpr uint32 MAX_BONES{ 512 };

class Skeleton;
class AnimationController;
class Animator : public Component, public IRenderable, public IUpdatable
{
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
    bool IsEnabled() const override
    {
        return m_IsEnabled;
    }

    void SetEnabled(bool able) override
    {
        m_IsEnabled = able;
    }
    void Update(float tick) override;
    //현재애니 이름반환
    std::string GetcurAnimation();
    void SetAnimation(int index);
    [[Method]]
    void UpdateAnimation();
    void CreateController();
    AnimationController* GetController() { return m_animationController; }

    [[Property]]
    Skeleton* m_Skeleton{ nullptr };
    [[Property]]
    float m_TimeElapsed{};
    [[Property]]
    uint32_t m_AnimIndexChosen{};
    DirectX::XMMATRIX m_FinalTransforms[MAX_BONES]{};
    [[Property]]
    std::string curAniName;
    bool m_isBlend = false;
    float blendT = 0;
    [[Property]]
    bool m_isLoof{ false };
    [[Property]]
    int m_AnimIndex{};
    int nextAnimIndex = -1;
    [[Property]]
    float m_nextTimeElapsed{};
    [[Property]]
    FileGuid m_Motion{};
    XMMATRIX blendtransform;
    [[Property]]
    AnimationController* m_animationController{};
private:
    bool m_IsEnabled = false;
    
};
