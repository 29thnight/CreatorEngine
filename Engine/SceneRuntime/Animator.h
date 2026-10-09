#pragma once
#include "Ownership.h"
#include "AuthoringNodeView.h"
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
//#include "IUpdatable.h"
//#include "IAwakable.h"
//#include "IOnDestroy.h"
#include "AnimationController.h"
#include "AnimatorSystem.h"
#include "KeyFrameEvent.h" // I5-D4e-2: 클립 오버라이드 소유
#include "../RenderEngine/LocalPose.h"
#include "../RenderEngine/ClipSamplingCursor.h"
#include "../RenderEngine/AssetDepot/AssetRequest.h"
#include <array>
#include <mathematics/matrix4x4.hpp>
#include <optional>
#include <span>
#include <type_traits>
#include "BoneRegion.h" // kMaxBones·BoneRegion

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

// Scene-authored target. Gameplay may update the world-space target and pole
// before the animation frame; workers receive a resolved value snapshot.
struct [[reflgen::reflect]] TwoBoneIKConstraint final
{

    std::string StartBone{};
    std::string MiddleBone{};
    std::string EndBone{};
    math::vector3 TargetWorld{};
    math::vector3 PoleWorld{ 0.f, 1.f, 0.f };
    float Weight{ 1.f };
    bool Enabled{ false };
    bool Required{ false };
};

// A local-space procedural correction applied after clip and layer evaluation.
struct [[reflgen::reflect]] BoneTransformConstraint final
{

    std::string Bone{};
    math::vector3 TranslationOffset{};
    math::quaternion RotationOffset{ 0.f, 0.f, 0.f, 1.f };
    math::vector3 ScaleMultiplier{ 1.f, 1.f, 1.f };
    float Weight{ 1.f };
    bool Enabled{};
    bool Required{};
};

class AnimationController;
class Socket;
class ScriptComponent;
namespace assets // PHASE 3.75 MBC8: typed 재생 정본(shared_owner 보관용)
{
    class ModelAssetGeneration;
    struct ModelSkeletonAsset;
    struct ModelAnimationAsset;
    struct ModelAnimationTrack;
    struct ModelAnimationDescriptor;
    struct ModelSkeletonPayload;
    struct ModelAnimationPayload;
    struct ModelMeshDescriptor;
}

// One frame's exact immutable owners. Sparse clip indices refer to the descriptor's
// summary order; unselected siblings never become resident through this view.
struct AnimatorAnimationClipPin final
{
    int clipIndex{ -1 };
    own::shared_owner<const assets::ModelAnimationPayload> payload{};
};

struct AnimatorAnimationBinding final
{
    own::shared_owner<const assets::ModelAnimationDescriptor> descriptor{};
    own::shared_owner<const assets::ModelSkeletonPayload> skeleton{};
    std::vector<AnimatorAnimationClipPin> clips{};
    // CEMCv11 compatibility only. A v3 binding never owns this aggregate.
    own::shared_owner<const assets::ModelAssetGeneration> legacyGeneration{};
    std::uint64_t skeletonSerial{};
    bool ready{};

    [[nodiscard]] const assets::ModelSkeletonAsset* Skeleton() const noexcept;
};

// Diagnostics distinguish independent v3 payloads from the transitional
// CEMCv11 aggregate. Existing Generation's numeric token is preserved.
enum class AnimatorDataPath : std::uint8_t
{
    None,
    Generation,
    Granular,
};

// K2: enable_shared_from_this 제거 — AnimationJob은 이제 shared_ptr을 빌리지
// 않고 this를 프레임-로컬 raw 포인터로만 관찰한다(OnInitialized/OnUninitializing 참조).
class [[reflgen::reflect]] Animator : public meta::identity<Animator, Component>
{
    public:
public:
    Animator();
    // I5-D4e-1: 본문은 cpp로 — shared_ptr<const experiment::Model> 멤버가
    // 전방선언 타입이라 헤더 inline 소멸이 불완전 타입을 인스턴스화한다.
    virtual ~Animator();

    // 트랙 C3: 가상 Update 오버라이드를 걷어내고 AnimatorSystem(조밀 벡터,
    // 전용 틱)으로 옮겼다. AnimationJob 등록도 씬 편입/이탈 훅을 쓴다:
    // DDOL 재부착에는 OnInitialized가 다시 호출되지 않기 때문이다.
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;
    void OnPropertyChanged(std::string_view propertyName,
        Meta::PropertyChangeSource source) override;
    void SetAnimation(int index);

    [[reflgen::reflect, creator::hide_in_inspector]]
    void UpdateAnimation();

    void CreateController(std::string name);
    std::shared_ptr<AnimationController> CreateController_UI();
    std::shared_ptr<AnimationController> CreateController_UINoAni();
    void DeleteController(int index);
    void DeleteController(std::string controllerName);
    AnimationController* GetController(std::string name);
    [[nodiscard]] bool IsDirectEditorPreview() const;
    [[nodiscard]] bool UsesMultipleControllers() const;
    void SetUseLayer(int layerindex,bool _useLayer);
    Entity* FindBoneRecursive(Entity* parent, const std::string& boneName);
    Socket* MakeSocket(std::string_view socketName,std::string_view boneName, Entity* object);
    Socket* FindSocket(std::string_view socketName);

    // CT6-d: 스켈레톤·파라미터·컨트롤러 그래프 복원(구 팩토리 분기 이동)
    void OnDeserialized(const Authoring::NodeView& node); // D3-a-4

    // Preserve the existing m_AnimIndexChosen YAML key while runtime selection
    // lives in the system-owned instance.
    void OnBeforeSerialize();

    // I5-D4e-2 — 씬 표기는 기존 형상(m_Skeleton.m_animations[i].m_isLoop/
    // m_keyFrameEvent)을 유지하되, 리플렉션이 적은 공유 자산 값을 Animator
    // 소유 오버라이드로 교체한다(reader 구세대 호환·스키마 무변경).
    void OnAfterSerialize(const Authoring::MutableNodeView& node);

    // I5-D4e-2 — 클립 오버라이드 표면. 재생(AnimationJob)·발화·에디터가 쓴다.
    // 구현은 AnimationEventBridge.cpp(CLR 경계 파일 — 구 Animation:: 이벤트
    // 표면의 이주지).
    AnimatorClipOverride* FindClipOverride(int clipIndex);
    const AnimatorClipOverride* FindClipOverride(int clipIndex) const;
    AnimatorClipOverride& EnsureClipOverride(int clipIndex);
    bool IsClipLooping(int clipIndex) const; // 오버라이드 → generation 자산값
    void SetClipLooping(int clipIndex, bool looping);
    // I5-D5b — 클립 목록의 열거 창구. D4e-2가 편집(루프·이벤트)을 Animator
    // 소유로 옮겼지만 에디터의 **열거·이름**은 여전히 공유 자산
    // (m_Skeleton->m_animations)을 직접 훑고 있었다 — 인덱스 축이 두 출처로
    // 갈리면 편집 정본과 표시 대상이 어긋난다. experiment가 정본, legacy는
    // 폴백(Assimp 모델). outViaExperiment는 게이트 관측 창구.
    [[nodiscard]] std::size_t GetClipCount(bool* outViaExperiment = nullptr,
        AnimatorDataPath* outPath = nullptr) const;
    // 범위 밖이면 빈 문자열. 이름은 오버라이드 대상이 아니다(자산 값).
    [[nodiscard]] std::string GetClipName(int clipIndex,
        bool* outViaExperiment = nullptr, AnimatorDataPath* outPath = nullptr) const;
    // 클립의 키프레임 수 = **유니크 키 시각 개수**(legacy 임포터 정의가 정본 —
    // experiment::clip::CountUniqueKeyTimes). 이벤트 저작이 frameKey 상한과
    // key(0~1 진행률) 환산에 쓴다 — 두 로드 경로가 다른 값을 주면 같은 자산이
    // 경로에 따라 다른 시점에 발화한다(D5b 실측).
    [[nodiscard]] std::size_t GetClipFrameCount(int clipIndex,
        bool* outViaExperiment = nullptr, AnimatorDataPath* outPath = nullptr) const;
    void AddClipEvent(int clipIndex);                 // 이름 유일화 신규(구 Animation::AddEvent())
    void DeleteClipEvent(int clipIndex, int eventIndex);
    // 발화 — 트리거 매칭 계수를 돌려준다(CLR 미준비여도 계수는 정확하다 —
    // 게이트가 큐 없이 판정하는 창구).
    // Unwrapped interval: multiple loops and reverse playback remain observable.
    std::size_t InvokeClipEvents(int clipIndex, double currentProgress,
        double previousProgress);

    // I5-D4e-3 — 본 해석·마스크 생성의 창구. Scene 본 전파와 AvatarMask
    // 생성이 legacy Skeleton(FindBone·m_serial·Bone* 트리)을 직접 만지던
    // 지점을 여기로 모은다 — experiment가 정본, legacy는 폴백(Assimp 모델).
    // 세대 키는 m_serial 그대로다: experiment 모델은 항상 역브리지 legacy
    // 스켈레톤과 짝으로 교체되므로 그 일련번호가 두 경로 공용 세대다.
    // I6-B2 — 본 캐시 무효화의 신원. experiment 핸들이 있으면 그 generation이
    // 정본이고, 없을 때만 legacy Skeleton::m_serial로 떨어진다. 두 축은 번호
    // 공간이 겹치지 않는다(experiment::Model::Generation 주석 참조).
    [[nodiscard]] uint64 GetSkeletonSerial(bool* outViaExperiment = nullptr,
        AnimatorDataPath* outPath = nullptr) const;
    // 이름→본 인덱스(1:1 계약으로 두 경로 동일 값). 실패는 -1.
    // outViaExperiment: 실제로 experiment 해석을 탔는가 — 게이트 관측 창구.
    // I6-B3 — 진단 표면이 legacy 스켈레톤을 직접 훑지 않게 하는 창구 둘.
    // 본 계수와 인덱스→이름은 지금까지 `m_Skeleton->m_bones`를 직접 읽는
    // 것 말고는 길이 없었고, 그것이 진단을 은퇴의 인질로 잡고 있었다.
    // 진단 창구 — 클립 길이. 두 축의 단위가 다르다(experiment durationTicks vs
    // legacy m_duration)는 것 자체가 관측 대상이라 값 변환을 하지 않는다.
    [[nodiscard]] double GetClipDuration(int clipIndex,
        bool* outViaExperiment = nullptr, AnimatorDataPath* outPath = nullptr) const;
    [[nodiscard]] std::size_t GetBoneCount(bool* outViaExperiment = nullptr,
        AnimatorDataPath* outPath = nullptr) const;
    [[nodiscard]] std::string GetBoneName(int boneIndex,
        bool* outViaExperiment = nullptr, AnimatorDataPath* outPath = nullptr) const;

    [[nodiscard]] int ResolveBoneIndex(const std::string& boneName,
        bool* outViaExperiment = nullptr, AnimatorDataPath* outPath = nullptr) const;
    // AvatarMask의 BoneMask 트리 생성 — m_BoneMasks 순서가 저장분 인덱스
    // 대응(ReCreateMask)이라 legacy MakeBoneMask와 같은 DFS 선순을 재현한다.
    // legacy 폴백에서만 MarkRegionSkeleton(공유 자산 region 태깅 — 이름
    // 파생이라 멱등)을 유지한다. outViaExperiment는 게이트 관측 창구.
    BoneMask* BuildAvatarBoneMasks(AvatarMask& mask,
        bool* outViaExperiment = nullptr, AnimatorDataPath* outPath = nullptr);
    // Converts a saved seven-region mask to named bone weights once the model
    // generation is bound. New masks are authored directly as bone weights.
    bool ConvertLegacyAvatarMask(AvatarMask& mask);

    // v3 skeleton and selected resident clips, or the CEMCv11 fallback.
    // Clip enumeration reads metadata; TypedClip never loads a missing sibling.
    [[nodiscard]] const assets::ModelSkeletonAsset* TypedSkeleton() const noexcept;
    [[nodiscard]] std::size_t TypedClipCount() const noexcept;
    [[nodiscard]] const assets::ModelAnimationAsset* TypedClip(int clipIndex) const noexcept;
    [[nodiscard]] AnimatorDataPath GetSkeletonPath() const noexcept;
    [[nodiscard]] const assets::ModelAnimationDescriptor* TypedDescriptor() const noexcept;
    // Owner-thread polling/submission only, before either animation worker batch.
    [[nodiscard]] AnimatorAnimationBinding CaptureAnimationBinding();

    bool HasSocket() { return !socketvec.empty(); };
    void ClearControllersAndParams();
    template<typename T>
    void AddParameter(const std::string valuename, T value, ValueType vType);
    void DeleteParameter(int index);
    ConditionParameter* AddDefaultParameter(ValueType vType);
    template<typename T>
    void SetParameter(const std::string valuename, T Value);
    ConditionParameter* FindParameter(std::string_view valueName);
    std::size_t FindParameterIndex(std::string_view valueName) const;
    ConditionParameter* ParameterAt(std::size_t index) const noexcept;
    [[nodiscard]] std::uint64_t ParameterVersion() const noexcept { return m_parameterVersion; }
    void NotifyParameterLayoutChanged() noexcept { ++m_parameterVersion; }

public:
    // I6-B1 — legacy Skeleton 서브트리는 더 이상 씬에 쓰지 않는다.
    // 리플렉션이 포인터를 따라 적던 것은 클립 이름·m_isLoop·
    // m_keyFrameEvent·m_rootTransform인데, **재로드가 실제로 읽는 것은
    // 클립별 (isLoop, events) 뿐**이고 나머지는 자산에서 다시 유도되는
    // 값이다. 그 둘은 D4e-2가 이미 Animator 소유(m_clipOverrides)로
    // 옮겼으므로, 표기만 소유를 따라가면 된다 — 쓰기는 새 정본으로,
    // 읽기는 구 씬 서브트리 폴백을 존치한다(OnDeserialized 참조).
    //
    // Reflection-only compatibility mirror. Runtime readers use GetSelectedClipIndex().
    [[reflgen::hidden]]
    uint32_t m_AnimIndexChosen{};

    [[nodiscard]] AnimInstance& GetInstance() noexcept;
    [[nodiscard]] const AnimInstance& GetInstance() const noexcept;
    [[nodiscard]] uint32_t GetSelectedClipIndex() const noexcept { return GetInstance().selectedClipIndex; }
    void SetSelectedClipIndex(uint32_t index) noexcept { GetInstance().selectedClipIndex = index; }
    [[nodiscard]] AnimatorPlaybackControl& GetPlaybackControl() noexcept { return GetInstance().control; }
    [[nodiscard]] const AnimatorPlaybackControl& GetPlaybackControl() const noexcept { return GetInstance().control; }

    [[reflgen::hidden]]
    int m_AnimIndex{};

    FileGuid m_Motion{};

    // World-space approximation used when estimating on-screen character size.
    [[reflgen::hidden]]
    float m_QualityRadius{ 1.f };

    // Authored parent-first prefix. Zero disables skeletal detail reduction.
    [[reflgen::hidden]]
    std::uint32_t m_LowDetailBoneCount{};

    // Gameplay attachments such as hitboxes may require every-frame L0.
    [[reflgen::hidden]]
    bool m_ForceFullQuality{ false };

    [[reflgen::hidden]]
    std::vector<TwoBoneIKConstraint> m_TwoBoneIKConstraints{};

    [[reflgen::hidden]]
    std::vector<BoneTransformConstraint> m_BoneTransformConstraints{};

    [[reflgen::ignore]]
    std::vector<Socket*> socketvec;

    std::vector<std::shared_ptr<AnimationController>> m_animationControllers{}; 
    std::vector<ConditionParameter*> Parameters;

    [[reflgen::ignore]]
    mutable std::mutex m_paramMutex;

    [[reflgen::ignore]]
    std::uint64_t m_parameterVersion{ 1 };

private:
    [[reflgen::ignore]]
    bool m_IsEnabled = false;

    [[reflgen::ignore]]
    AnimInstanceHandle m_instance{};

public:
    // Explicitly transitional CEMCv11 fallback. v3 uses independent payload pins.
    [[reflgen::ignore]]
    own::shared_owner<const assets::ModelAssetGeneration> m_modelGeneration{};

    // Polls v3 preparation without waiting; only the explicit CEMCv11 fallback
    // retains the transitional aggregate load. Buffers belong to the instance.
    void EnsureAnimationBinding();
    void BindModelGeneration(own::shared_owner<const assets::ModelAssetGeneration> generation);
    bool BindModelDescriptor(own::shared_owner<const assets::ModelAnimationDescriptor> descriptor,
        own::shared_owner<const assets::ModelSkeletonPayload> skeleton);
    // Render bridge guard for the transitional CEMCv11 geometry consumer. No I/O
    // or new aggregate pin: compare the already-owned geometry skin contract.
    [[nodiscard]] bool IsSkinBindingCompatible(const assets::ModelAssetGeneration& geometry) const noexcept;
    [[nodiscard]] bool IsSkinBindingCompatible(const assets::ModelMeshDescriptor& geometry) const noexcept;

private:
    struct RequestedAnimationClip final
    {
        int clipIndex{ -1 };
        own::shared_owner<const assets::ModelAnimationPayload> payload{};
        AssetDepot::AssetRequest<assets::ModelAnimationPayload> request{};
        bool requested{};
    };

    struct DeferredAvatarMask final
    {
        struct Bone final
        {
            std::string name{};
            bool enabled{ true };
            float weight{ 1.f };
        };
        std::weak_ptr<AnimationController> controller{};
        std::vector<Bone> bones{};
    };

    [[reflgen::ignore]]
    FileGuid m_animationBindingMotion{};
    [[reflgen::ignore]]
    bool m_animationBindingInitialized{};
    [[reflgen::ignore]]
    bool m_granularAnimationBinding{};
    [[reflgen::ignore]]
    own::shared_owner<const assets::ModelAnimationDescriptor> m_animationDescriptor{};
    [[reflgen::ignore]]
    own::shared_owner<const assets::ModelSkeletonPayload> m_skeletonPayload{};
    [[reflgen::ignore]]
    AssetDepot::AssetRequest<assets::ModelAnimationDescriptor> m_descriptorRequest{};
    [[reflgen::ignore]]
    AssetDepot::AssetRequest<assets::ModelSkeletonPayload> m_skeletonRequest{};
    [[reflgen::ignore]]
    bool m_skeletonRequested{};
    [[reflgen::ignore]]
    std::uint64_t m_poseSkeletonSerial{};
    [[reflgen::ignore]]
    std::vector<RequestedAnimationClip> m_requestedClips{};
    [[reflgen::ignore]]
    std::vector<DeferredAvatarMask> m_deferredAvatarMasks{};

    void ResetAnimationPose();
    void RestoreDeferredAvatarMasks();

public:
    // I5-D4e-2 — 클립별 이벤트·루프 오버라이드(위 구조 주석 참조). 영속은
    // OnAfterSerialize가 기존 씬 표기(m_Skeleton 서브트리)에 되입힌다.
    [[reflgen::ignore]]
    std::vector<AnimatorClipOverride> m_clipOverrides{};

    void StopAnimation(float duration)
    {
		auto& control = GetPlaybackControl();
		control.stopTimer = duration;
		control.stoppedDuration = 0.f;
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
    NotifyParameterLayoutChanged();
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
