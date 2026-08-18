#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
//#include "IUpdatable.h"
//#include "IAwakable.h"
//#include "IOnDestroy.h"
#include "AnimationController.h"
constexpr uint32 MAX_BONES{ 512 };

class Skeleton;
class AnimationController;
class Socket;
namespace YAML { class Node; } // CT6-d

// K2: enable_shared_from_this 제거 — AnimationJob은 이제 shared_ptr을 빌리지
// 않고 this를 프레임-로컬 raw 포인터로만 관찰한다(Awake/OnDestroy 참조).
class Animator : public meta::identity<Animator, Component>
{
    public:
    static consteval auto reflect()
    {
        return meta::schema<Self>(
            meta::field<&Self::m_Skeleton>,
            meta::field<&Self::m_AnimIndexChosen>,
            meta::field<&Self::m_AnimIndex>,
            meta::field<&Self::m_Motion>,
            meta::field<&Self::m_animationControllers>,
            meta::field<&Self::Parameters>,
            meta::method<&Self::UpdateAnimation>);
    }
public:
    Animator()
    {
        socketvec.clear();
    }
    virtual ~Animator()
    {
        m_animationControllers.clear();

        {
            std::unique_lock lock(m_paramMutex);
            for (auto& param : Parameters)
            {
                delete param; // �ϳ��� ����
            }
            Parameters.clear(); // ���� ����
        }

        for (auto& socket : socketvec)
        {
            delete socket;
        }
        socketvec.clear();
    }

    void Awake() override;
    void OnDestroy() override;

    // 트랙 C3: 가상 Update 오버라이드를 걷어내고 AnimatorSystem(조밀 벡터,
    // 전용 틱)으로 옮겼다 — 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전,
    // 근거는 AnimatorSystem.h 주석). Awake/OnDestroy는 RenderScene 등록용으로
    // 그대로 둔다(트랙 범위 밖 — AnimationJob 스키닝 등록부와 혼동 금지).
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;
    void SetAnimation(int index);
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

    // CT6-d: 스켈레톤·파라미터·컨트롤러 그래프 복원(구 팩토리 분기 이동)
    void OnDeserialized(const YAML::Node& node);

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
    Skeleton* m_Skeleton{ nullptr };
    float m_TimeElapsed{};
    uint32_t m_AnimIndexChosen{};
    DirectX::XMMATRIX m_localTransforms[MAX_BONES]{};
    DirectX::XMMATRIX m_FinalTransforms[MAX_BONES]{};
    float blendT = 0;
    int m_AnimIndex{};
    int nextAnimIndex = -1;
    float m_nextTimeElapsed{};
    FileGuid m_Motion{};
    XMMATRIX blendtransform;
    std::vector<Socket*> socketvec;
    std::vector<std::shared_ptr<AnimationController>> m_animationControllers{}; 
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
