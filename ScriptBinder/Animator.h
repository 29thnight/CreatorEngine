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

// K2: enable_shared_from_this ?œê±° ??AnimationJob?€ ?´ì œ shared_ptr??ë¹Œë¦¬ì§€
// ?Šê³  thisë¥??„ë ˆ??ë¡œì»¬ raw ?¬ì¸?°ë¡œë§?ê´€ì°°í•œ??Awake/OnDestroy ì°¸ì¡°).
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
                delete param; // ÇÏ³ª¾¿ ÇØÁ¦
            }
            Parameters.clear(); // º¤ÅÍ ºñ¿ì±â
        }

        for (auto& socket : socketvec)
        {
            delete socket;
        }
        socketvec.clear();
    }

    void OnInitialized() override;
    void OnUninitializing() override;

    // ?¸ë™ C3: ê°€??Update ?¤ë²„?¼ì´?œë? ê±·ì–´?´ê³  AnimatorSystem(ì¡°ë? ë²¡í„°,
    // ?„ìš© ???¼ë¡œ ??²¼?????±ë¡/?´ì??????¸ì…/?´íƒˆ ?…ìœ¼ë¡??œë‹¤(DDOL ?ˆì „,
    // ê·¼ê±°??AnimatorSystem.h ì£¼ì„). Awake/OnDestroy??RenderScene ?±ë¡?©ìœ¼ë¡?
    // ê·¸ë?ë¡??”ë‹¤(?¸ë™ ë²”ìœ„ ë°???AnimationJob ?¤í‚¤???±ë¡ë¶€?€ ?¼ë™ ê¸ˆì?).
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
    Entity* FindBoneRecursive(Entity* parent, const std::string& boneName);
    Socket* MakeSocket(std::string_view socketName,std::string_view boneName, Entity* object);
    Socket* FindSocket(std::string_view socketName);

    // CT6-d: ?¤ì¼ˆ?ˆí†¤Â·?Œë¼ë¯¸í„°Â·ì»¨íŠ¸ë¡¤ëŸ¬ ê·¸ë˜??ë³µì›(êµ??©í† ë¦?ë¶„ê¸° ?´ë™)
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
