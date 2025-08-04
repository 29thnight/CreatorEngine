#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
//#include "IUpdatable.h"
//#include "IAwakable.h"
//#include "IOnDestroy.h"
#include "IRegistableEvent.h"
#include "AnimationController.h"
#include "Animator.generated.h"
constexpr uint32 MAX_BONES{ 512 };

class Skeleton;
class AnimationController;
class Socket;
class Animator : public Component, public RegistableEvent<Animator>, public std::enable_shared_from_this<Animator>
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
        m_animationControllers.clear();
        for (auto& param : Parameters)
        {
            delete param; // 하나씩 해제
        }
        Parameters.clear(); // 벡터 비우기
    }

    void Awake() override;
    void Update(float tick) override;
    void OnDestroy() override;
    void SetAnimation(int index);
    [[Method]]
    void UpdateAnimation();
    void CreateController(std::string name);
    void CreateController_UI();
    void DeleteController(int index);
    void DeleteController(std::string controllerName);
    AnimationController* GetController(std::string name);
    bool UsesMultipleControllers() { return m_animationControllers.size() >= 2; }

    Socket* MakeSocket(std::string_view socketName,std::string_view boneName);
    Socket* FindSocket(std::string_view socketName);
    [[Property]]
    Skeleton* m_Skeleton{ nullptr };
    float m_TimeElapsed{};
    [[Property]]
    uint32_t m_AnimIndexChosen{};
    DirectX::XMMATRIX m_localTransforms[MAX_BONES]{};
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
    std::vector<std::shared_ptr<AnimationController>> m_animationControllers{}; 
    [[Property]]
    std::vector<ConditionParameter*> Parameters;

    template<typename T>
    void AddParameter(const std::string valuename, T value, ValueType vType)
    {
        for (auto& parm : Parameters)
        {
            if (parm->name == valuename)
                return;
        }
        ConditionParameter* newParameter = new ConditionParameter(value, vType, valuename);
        Parameters.push_back(newParameter);
    }
    void DeleteParameter(int index);

    void AddDefaultParameter(ValueType vType)
    {
        std::string baseName;
        switch (vType)
        {
        case ValueType::Float:
            baseName = "NewFloat";
            break;
        case ValueType::Int:
            baseName = "NewInt";
            break;
        case ValueType::Bool:
            baseName = "NewBool";
            break;
        case ValueType::Trigger:
            baseName = "NewTrigger";
            break;
        }
        std::string valueName = baseName;
        int index = 0;
        bool isDuplicate = true;
        while (isDuplicate)
        {
            isDuplicate = false;
            for (auto& parm : Parameters)
            {
                if (parm->name == valueName)
                {
                    isDuplicate = true;
                    valueName = baseName + std::to_string(++index);
                    break;
                }
            }
        }
        ConditionParameter* newParameter = new ConditionParameter(0, vType, valueName);
        Parameters.push_back(newParameter);
    }

    template<typename T>
    void SetParameter(const std::string valuename, T Value)
    {
        if (Parameters.empty()) return;
        for (auto& param : Parameters)
        {
            if (param->name == valuename)
            {
                param->UpdateParameter(Value);
            }
        }
    }
  
    ConditionParameter* FindParameter(std::string valueName);
private:
    bool m_IsEnabled = false;
    
};
