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
        socketvec.clear();
    }
    virtual ~Animator()
    {
        m_animationControllers.clear();

        {
            std::unique_lock lock(m_paramMutex);
            for (auto& param : Parameters)
            {
                delete param; // 하나씩 해제
            }
            Parameters.clear(); // 벡터 비우기
        }

        for (auto& socket : socketvec)
        {
            delete socket;
        }
        socketvec.clear();
    }

    void Awake() override;
    void Update(float tick) override;
    void OnDestroy() override;
    void SetAnimation(int index);
    [[Method]]
    void UpdateAnimation();
    void CreateController(std::string name);
    std::shared_ptr<AnimationController> CreateController_UI();
    std::shared_ptr<AnimationController> CreateController_UINoAni();
    void DeleteController(int index);
    void DeleteController(std::string controllerName);
    AnimationController* GetController(std::string name);
    bool UsesMultipleControllers() { return m_animationControllers.size() >= 2; }
    void SerializeControllers(std::string _jsonName);
    void DeserializeControllers(std::string _filename);
    void SetUseLayer(int layerindex,bool _useLayer);
    GameObject* FindBoneRecursive(GameObject* parent, const std::string& boneName);
    Socket* MakeSocket(std::string_view socketName,std::string_view boneName, GameObject* object);
    Socket* FindSocket(std::string_view socketName);
    bool HasSocket() { return !socketvec.empty(); };
    void ClearControllersAndParams();
    template<typename T>
    void AddParameter(const std::string valuename, T value, ValueType vType);
    void DeleteParameter(int index);
    ConditionParameter* AddDefaultParameter(ValueType vType);
    template<typename T>
    void SetParameter(const std::string valuename, T Value);
    ConditionParameter* FindParameter(std::string valueName);

public:
    [[Property]]
    Skeleton* m_Skeleton{ nullptr };
    float m_TimeElapsed{};
    [[Property]]
    uint32_t m_AnimIndexChosen{};
    DirectX::XMMATRIX m_localTransforms[MAX_BONES]{};
    DirectX::XMMATRIX m_FinalTransforms[MAX_BONES]{};
    float blendT = 0;
    [[Property]]
    int m_AnimIndex{};
    int nextAnimIndex = -1;
    float m_nextTimeElapsed{};
    [[Property]]
    FileGuid m_Motion{};
    XMMATRIX blendtransform;
    std::vector<Socket*> socketvec;
    [[Property]]
    std::vector<std::shared_ptr<AnimationController>> m_animationControllers{}; 
    [[Property]]
    std::vector<ConditionParameter*> Parameters;
    std::mutex m_paramMutex;

    bool m_isBlend = false;
private:
    bool m_IsEnabled = false;

public:
    float m_stopTimer = 0.f;
	float m_stopDuration = 0.f;
    void StopAnimation(float duration)
    {
        m_stopTimer = duration;
        m_stopDuration = 0.f;
	}
};

template<typename T>
inline void Animator::AddParameter(const std::string valuename, T value, ValueType vType)
{
    std::unique_lock lock(m_paramMutex);
    for (auto& parm : Parameters)
    {
        if (parm->name == valuename)
            return;
    }
    ConditionParameter* newParameter = new ConditionParameter(value, vType, valuename);
    Parameters.push_back(newParameter);
}

template<typename T>
inline void Animator::SetParameter(const std::string valuename, T Value)
{
    std::unique_lock lock(m_paramMutex);
    if (Parameters.empty()) return;
    for (auto& param : Parameters)
    {
        if (param->name == valuename)
        {
            param->UpdateParameter(Value);
        }
    }
}
