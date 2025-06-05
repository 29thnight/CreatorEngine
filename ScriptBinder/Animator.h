#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IUpdatable.h"
#include "IAwakable.h"
#include "IOnDistroy.h"
#include "AnimationController.h"
#include "Animator.generated.h"
constexpr uint32 MAX_BONES{ 512 };

class Skeleton;
class AnimationController;
class Animator : public Component, public IUpdatable, public IAwakable, public IOnDistroy
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
        for (auto Controller : m_animationControllers)
        {
            delete Controller;
        }
    }

    void Awake() override;
    void Update(float tick) override;
    void OnDistroy() override;
    void SetAnimation(int index);
    [[Method]]
    void UpdateAnimation();
    void CreateController(std::string name);
    [[Method]]
    void CreateController_UI();
    AnimationController* GetController(std::string name);
    bool UsesMultipleControllers() { return m_animationControllers.size() >= 2; }
    [[Property]]
    Skeleton* m_Skeleton{ nullptr };
    float m_TimeElapsed{};
    [[Property]]
    uint32_t m_AnimIndexChosen{};
    DirectX::XMMATRIX m_FinalTransforms[MAX_BONES]{};
    bool m_isBlend = false;
    float blendT = 0;
    [[Property]]
    int m_AnimIndex{};
    int nextAnimIndex = -1;
    float m_nextTimeElapsed{};
    [[Property]]
    FileGuid m_Motion{};
    XMMATRIX blendtransform;

    [[Property]]
    std::vector<AnimationController*> m_animationControllers{};
    [[Property]]
    std::vector<ConditionParameter> Parameters;

    template<typename T>
    void AddParameter(const std::string valuename, T value, ValueType vType)
    {
        for (auto& parm : Parameters)
        {
            if (parm.name == valuename)
                return;
        }
        Parameters.push_back(ConditionParameter(value, vType, valuename));
    }

    template<typename T>
    void SetParameter(const std::string valuename, T Value)
    {
        for (auto& param : Parameters)
        {
            if (param.name == valuename)
            {
                param.UpdateParameter(Value);
            }
        }
    }
  
private:
    bool m_IsEnabled = false;
    
};
