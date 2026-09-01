#pragma once
#include "AuthoringNodeView.h"
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
//#include "IUpdatable.h"
//#include "IAwakable.h"
//#include "IOnDestroy.h"
#include "AnimationController.h"
#include "KeyFrameEvent.h" // I5-D4e-2: 클립 오버라이드 소유
#include <mathematics/matrix4x4.hpp>
#include <optional>
#include <type_traits>
constexpr uint32 MAX_BONES{ 512 };

// I5-D4e-2 — 클립별 이벤트·루프 오버라이드. 소유는 씬(Animator)이다(D0a 판정):
// legacy조차 모델 자산에 직렬화한 적이 없고(.asset 캐시 포맷에 이벤트 없음) 씬
// YAML이 유일한 영속이었는데, postLoad가 그것을 공유 자산
// (m_Skeleton->m_animations)에 재주입해 같은 스켈레톤을 공유하는 Animator 간
// 오염(마지막 로드 승자)을 만들었다. 이제 재생 루프 판정·발화·에디터 편집·
// 직렬화가 전부 이 구조를 정본으로 본다 — 공유 자산은 불변이다.
struct AnimatorClipOverride final
{
	int clipIndex{ -1 };
	std::optional<bool> loopOverride{};
	std::vector<KeyFrameEvent> events{};
};

class Skeleton;
class AnimationController;
class Socket;
namespace YAML { class Node; } // CT6-d
namespace experiment { class Model; } // I5-D4e-1: 재생 데이터 핸들(shared_ptr 보관용)

// K2: enable_shared_from_this 제거 — AnimationJob은 이제 shared_ptr을 빌리지
// 않고 this를 프레임-로컬 raw 포인터로만 관찰한다(Awake/OnDestroy 참조).
class Animator : public meta::identity<Animator, Component>
{
    public:
    static consteval auto reflect()
    {
        // I6-B1 — legacy Skeleton 서브트리는 더 이상 씬에 쓰지 않는다.
        // 리플렉션이 포인터를 따라 적던 것은 클립 이름·m_isLoop·
        // m_keyFrameEvent·m_rootTransform인데, **재로드가 실제로 읽는 것은
        // 클립별 (isLoop, events) 뿐**이고 나머지는 자산에서 다시 유도되는
        // 값이다. 그 둘은 D4e-2가 이미 Animator 소유(m_clipOverrides)로
        // 옮겼으므로, 표기만 소유를 따라가면 된다 — 쓰기는 새 정본으로,
        // 읽기는 구 씬 서브트리 폴백을 존치한다(OnDeserialized 참조).
        return meta::schema<Self>(
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
    // I5-D4e-1: 본문은 cpp로 — shared_ptr<const experiment::Model> 멤버가
    // 전방선언 타입이라 헤더 inline 소멸이 불완전 타입을 인스턴스화한다.
    virtual ~Animator();

    void OnInitialized() override;
    void OnUninitializing() override;

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
    // 저작 게시는 Editor Host가 소유한다. 여기서는 JSON payload만 만들고 Player에는
    // handler가 없어 정상적으로 실패한다.
    bool SerializeControllers(std::string _jsonName);
    void DeserializeControllers(std::string _filename);
    void SetUseLayer(int layerindex,bool _useLayer);
    Entity* FindBoneRecursive(Entity* parent, const std::string& boneName);
    Socket* MakeSocket(std::string_view socketName,std::string_view boneName, Entity* object);
    Socket* FindSocket(std::string_view socketName);

    // CT6-d: 스켈레톤·파라미터·컨트롤러 그래프 복원(구 팩토리 분기 이동)
    void OnDeserialized(const Authoring::NodeView& node); // D3-a-4

    // I5-D4e-2 — 씬 표기는 기존 형상(m_Skeleton.m_animations[i].m_isLoop/
    // m_keyFrameEvent)을 유지하되, 리플렉션이 적은 공유 자산 값을 Animator
    // 소유 오버라이드로 교체한다(reader 구세대 호환·스키마 무변경).
    void OnAfterSerialize(YAML::Node& node);

    // I5-D4e-2 — 클립 오버라이드 표면. 재생(AnimationJob)·발화·에디터가 쓴다.
    // 구현은 AnimationEventBridge.cpp(CLR 경계 파일 — 구 Animation:: 이벤트
    // 표면의 이주지).
    AnimatorClipOverride* FindClipOverride(int clipIndex);
    const AnimatorClipOverride* FindClipOverride(int clipIndex) const;
    AnimatorClipOverride& EnsureClipOverride(int clipIndex);
    bool IsClipLooping(int clipIndex) const; // 오버라이드 → 자산(experiment→legacy) 폴백
    void SetClipLooping(int clipIndex, bool looping);
    // I5-D5b — 클립 목록의 열거 창구. D4e-2가 편집(루프·이벤트)을 Animator
    // 소유로 옮겼지만 에디터의 **열거·이름**은 여전히 공유 자산
    // (m_Skeleton->m_animations)을 직접 훑고 있었다 — 인덱스 축이 두 출처로
    // 갈리면 편집 정본과 표시 대상이 어긋난다. experiment가 정본, legacy는
    // 폴백(Assimp 모델). outViaExperiment는 게이트 관측 창구.
    [[nodiscard]] std::size_t GetClipCount(bool* outViaExperiment = nullptr) const;
    // 범위 밖이면 빈 문자열. 이름은 오버라이드 대상이 아니다(자산 값).
    [[nodiscard]] std::string GetClipName(int clipIndex,
        bool* outViaExperiment = nullptr) const;
    // 클립의 키프레임 수 = **유니크 키 시각 개수**(legacy 임포터 정의가 정본 —
    // experiment::clip::CountUniqueKeyTimes). 이벤트 저작이 frameKey 상한과
    // key(0~1 진행률) 환산에 쓴다 — 두 로드 경로가 다른 값을 주면 같은 자산이
    // 경로에 따라 다른 시점에 발화한다(D5b 실측).
    [[nodiscard]] std::size_t GetClipFrameCount(int clipIndex,
        bool* outViaExperiment = nullptr) const;
    void AddClipEvent(int clipIndex);                 // 이름 유일화 신규(구 Animation::AddEvent())
    void DeleteClipEvent(int clipIndex, int eventIndex);
    // 발화 — 트리거 매칭 계수를 돌려준다(CLR 미준비여도 계수는 정확하다 —
    // 게이트가 큐 없이 판정하는 창구).
    std::size_t InvokeClipEvents(int clipIndex, float currentProgress,
        float previousProgress);

    // I5-D4e-3 — 본 해석·마스크 생성의 창구. Scene 본 전파와 AvatarMask
    // 생성이 legacy Skeleton(FindBone·m_serial·Bone* 트리)을 직접 만지던
    // 지점을 여기로 모은다 — experiment가 정본, legacy는 폴백(Assimp 모델).
    // 세대 키는 m_serial 그대로다: experiment 모델은 항상 역브리지 legacy
    // 스켈레톤과 짝으로 교체되므로 그 일련번호가 두 경로 공용 세대다.
    // I6-B2 — 본 캐시 무효화의 신원. experiment 핸들이 있으면 그 generation이
    // 정본이고, 없을 때만 legacy Skeleton::m_serial로 떨어진다. 두 축은 번호
    // 공간이 겹치지 않는다(experiment::Model::Generation 주석 참조).
    [[nodiscard]] uint64 GetSkeletonSerial(bool* outViaExperiment = nullptr) const;
    // 이름→본 인덱스(1:1 계약으로 두 경로 동일 값). 실패는 -1.
    // outViaExperiment: 실제로 experiment 해석을 탔는가 — 게이트 관측 창구.
    [[nodiscard]] int ResolveBoneIndex(const std::string& boneName,
        bool* outViaExperiment = nullptr) const;
    // AvatarMask의 BoneMask 트리 생성 — m_BoneMasks 순서가 저장분 인덱스
    // 대응(ReCreateMask)이라 legacy MakeBoneMask와 같은 DFS 선순을 재현한다.
    // legacy 폴백에서만 MarkRegionSkeleton(공유 자산 region 태깅 — 이름
    // 파생이라 멱등)을 유지한다. outViaExperiment는 게이트 관측 창구.
    BoneMask* BuildAvatarBoneMasks(AvatarMask& mask,
        bool* outViaExperiment = nullptr);

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
    math::matrix4x4 m_localTransforms[MAX_BONES]{};
    math::matrix4x4 m_FinalTransforms[MAX_BONES]{};
    static_assert(std::is_same_v<
        std::remove_extent_t<decltype(m_localTransforms)>, math::matrix4x4>);
    static_assert(std::is_same_v<
        std::remove_extent_t<decltype(m_FinalTransforms)>, math::matrix4x4>);
    float blendT = 0;
    int m_AnimIndex{};
    int nextAnimIndex = -1;
    float m_nextTimeElapsed{};
    FileGuid m_Motion{};
    std::vector<Socket*> socketvec;
    std::vector<std::shared_ptr<AnimationController>> m_animationControllers{}; 
    std::vector<ConditionParameter*> Parameters;
    std::mutex m_paramMutex;

    bool m_isBlend = false;
private:
    bool m_IsEnabled = false;

public:
    // I5-D4e-1 — 재생 데이터의 experiment 핸들. m_Motion(모델 GUID — 역브리지
    // 폴백 규약)으로 EnsureExperimentAnimationBinding이 채우고, AnimationJob
    // 틱이 이것이 있으면 experiment 경로(단일 순회)를, 없으면 legacy 경로를
    // 탄다. 비직렬화 — 영속 신원은 m_Motion이 진다.
    std::shared_ptr<const experiment::Model> m_experimentModel{};
    // 본별 BoneRegion 파생 캐시(uint8 저장 — 헤더가 Skeleton.h의 enum을 열지
    // 않기 위한 불투명 표현). AvatarMask humanoid 레이어 판정이 소비한다.
    std::vector<std::uint8_t> m_experimentBoneRegions{};
    // [anim.tick] 경로 관측을 애니메이터당 1회로 줄이는 플래그.
    bool m_tickPathLogged{ false };
    void EnsureExperimentAnimationBinding();

    // I5-D4e-2 — 클립별 이벤트·루프 오버라이드(위 구조 주석 참조). 영속은
    // OnAfterSerialize가 기존 씬 표기(m_Skeleton 서브트리)에 되입힌다.
    std::vector<AnimatorClipOverride> m_clipOverrides{};

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
