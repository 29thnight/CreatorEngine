#pragma once
#include "ClassProperty.h"
#include "../RenderEngine/LocalPose.h"
#include "../RenderEngine/BoneRegion.h"
#include "../RenderEngine/ClipSamplingCursor.h"
#include "AnimTaskList.h"
#include <mathematics/matrix4x4.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Animator;
class AnimationController;
class AnimationState;
class AniTransition;
class ScriptComponent;
namespace assets { struct ModelAnimationTrack; }

// Authored masks can change through public Editor fields; cache their exact
// shape so the bone-indexed weights are rebuilt when those fields change.
struct AnimatorLayerMaskCache final
{
    struct NamedBone final
    {
        bool m_present{};
        std::string m_name{};
        bool m_enabled{};
        float m_weight{ 1.f };
    };

    bool m_valid{};
    bool m_hasMask{};
    bool m_isHumanoid{};
    bool m_useAll{};
    bool m_useUpper{};
    bool m_useLower{};
    std::vector<NamedBone> m_namedBones{};
    std::vector<float> m_weights{};
};

struct AnimatorIKBinding final
{
    std::uint64_t skeletonSerial{};
    std::string startName{};
    std::string middleName{};
    std::string endName{};
    std::uint32_t start{ animation::invalid_task };
    std::uint32_t middle{ animation::invalid_task };
    std::uint32_t end{ animation::invalid_task };
};

struct AnimatorIKSnapshot final
{
    std::uint32_t start{};
    std::uint32_t middle{};
    std::uint32_t end{};
    math::vector3 target{};
    math::vector3 pole{};
    float weight{};
    bool required{};
};

struct AnimatorBoneTransformBinding final
{
    std::uint64_t skeletonSerial{};
    std::string boneName{};
    std::uint32_t bone{ animation::invalid_task };
};

struct AnimatorBoneTransformSnapshot final
{
    std::uint32_t bone{};
    math::vector3 translationOffset{};
    math::quaternion rotationOffset{ 0.f, 0.f, 0.f, 1.f };
    math::vector3 scaleMultiplier{ 1.f, 1.f, 1.f };
    float weight{};
    bool required{};
};

struct AnimInstanceHandle final
{
    std::uint32_t slot{ UINT32_MAX };
    std::uint32_t generation{};
};

struct ControllerPlayback final
{
    AnimationState* currentState{};
    AnimationState* nextState{};
    AniTransition* currentTransition{};
    int animationIndex{};
    int nextAnimationIndex{ -1 };
    bool needBlend{};
    bool isBlending{};
    bool endAnimation{};
    float timeElapsed{};
    float nextTimeElapsed{};
    float currentProgress{};
    float previousCurrentProgress{};
    float nextProgress{};
    float previousNextProgress{};
    float blendingTime{};
};

struct AnimatorPlaybackControl final
{
    float blendT{};
    int nextClipIndex{ -1 };
    bool isBlending{};
    float stopTimer{};
    float stoppedDuration{};
};

struct AnimInstance final
{
    static constexpr std::size_t kControllerSlotPageSize = 4;

    struct ControllerSlot final
    {
        std::uint64_t id{};
        std::weak_ptr<AnimationController> owner{};
        ControllerPlayback playback{};
    };

    std::uint32_t slot{};
    std::vector<math::matrix4x4> localTransforms{};
    std::vector<math::matrix4x4> finalTransforms{};
#if CE_DEVELOPMENT && !CE_SHIPPING
    // Transient diagnostic owner; never serialized or set by ordinary playback.
    // The fixture publishes its palette on GT after the animation batch joins.
    bool diagnosticPoseOverride{};
#endif
    Animation::LocalPose pose{};
    std::uint32_t selectedClipIndex{};
    // Editor-only clip preview bypasses the controller graph without changing it.
    bool editorPreviewActive{};
    bool editorPreviewPlaying{};
    bool editorPreviewPaused{};
    bool captureTaskExecution{};
    std::vector<std::uint32_t> executedTaskIndices{};
    float timeElapsed{};
    float nextTimeElapsed{};
    AnimatorPlaybackControl control{};
    std::vector<math::matrix4x4> poseGlobals{};
    // Reused parent-first reachability scratch for IK subtree recomputation.
    std::vector<std::uint8_t> ikAffectedBones{};
    float optionalIKWeight{ 1.f };
    // Local-space inertial offsets bridge a snapped L4 pose back to full blending.
    std::vector<Animation::AdditiveDelta> blendRecoveryOffsets{};
    Animation::LocalPose blendRecoverySourcePose{};
    float blendRecoveryElapsed{};
    bool blendRecoveryPending{};
    bool blendRecoveryActive{};
    bool l4SnappedBlend{};
    std::vector<Animation::LocalTransform> bindLocals{};
    std::uint64_t bindSkeletonSerial{};
    std::uint32_t activeBoneCount{};
    std::uint32_t evaluatedBoneSamples{};
    Animation::LocalPose l6PreviousPose{};
    Animation::LocalPose l6LatestPose{};
    std::vector<math::matrix4x4> l6PreviousLocals{};
    std::vector<math::matrix4x4> l6LatestLocals{};
    std::vector<math::matrix4x4> l6PreviousFinals{};
    std::vector<math::matrix4x4> l6LatestFinals{};
    std::vector<math::matrix4x4> l6PreviousGlobals{};
    std::vector<math::matrix4x4> l6LatestGlobals{};
    float l6PublishedAlpha{ 1.f };
    std::uint8_t l6Phase{};
    std::uint8_t l6NextPhase{};
    std::uint8_t l6Interval{ 2 };
    bool l6Seeded{};
    bool l6Evaluate{ true };
    int l6ClipIndex{ -1 };
    int l6ControllerSlot{ -1 };
    // Number of clip samples actually executed by the most recent pose pass.
    std::uint32_t executedPoseSamples{};
    std::uint64_t poseStorageGrowths{};
    std::size_t workerPoseBufferCount{};
    std::uintptr_t workerPosePoolIdentity{};
    std::array<const void*, 3> workerCurrentStorage{};
    std::vector<std::array<Animation::ClipSamplingCursor, 2>> samplingCursors{};
    animation::task_list taskList{};
    std::vector<AnimatorIKBinding> ikBindings{};
    std::vector<AnimatorIKSnapshot> ikSnapshots{};
    std::vector<AnimatorBoneTransformBinding> boneTransformBindings{};
    std::vector<AnimatorBoneTransformSnapshot> boneTransformSnapshots{};
    float significance{ 1.f };
    float projectedHeight{ 1.f };
    animation::quality_stage qualityStage{ animation::quality_stage::l0 };
    std::uint16_t budgetPromotionFrames{};
    // -1 selects the camera policy. Product probes may force L0-L6 or L7.
    int qualityOverride{ -1 };
    std::vector<std::span<const assets::ModelAnimationTrack* const>> layerTrackTables{};
    std::vector<AnimatorLayerMaskCache> layerMaskCaches{};
    std::vector<std::uint8_t> layerSelected{};
    std::vector<std::size_t> eventOrderScratch{};
    std::vector<ScriptComponent*> eventScriptsScratch{};
    std::vector<std::uint8_t> boneRegions{};
    bool tickPathLogged{};
    // Dense lookup order is independent of authored controller layer order.
    // Page records keep in-progress transitions stable while layers grow.
    std::vector<std::unique_ptr<ControllerSlot[]>> controllerPages{};
    std::vector<ControllerSlot*> freeControllerSlots{};
    std::vector<ControllerSlot*> controllerSlots{};
    std::unordered_map<std::uint64_t, std::size_t> controllerSlotById{};

    [[nodiscard]] ControllerPlayback& GetControllerPlayback(
        std::uint64_t id, std::weak_ptr<AnimationController> owner)
    {
        if (const auto found = controllerSlotById.find(id); found != controllerSlotById.end())
            return controllerSlots[found->second]->playback;

        if (freeControllerSlots.empty())
        {
            auto page = std::make_unique<ControllerSlot[]>(kControllerSlotPageSize);
            auto* records = page.get();
            freeControllerSlots.reserve((controllerPages.size() + 1) * kControllerSlotPageSize);
            controllerPages.push_back(std::move(page));
            for (std::size_t slot = kControllerSlotPageSize; slot > 0; --slot)
                freeControllerSlots.push_back(records + slot - 1);
        }

        ControllerSlot* record = freeControllerSlots.back();
        const auto index = controllerSlots.size();
        controllerSlots.push_back(record);
        try
        {
            controllerSlotById.emplace(id, index);
        }
        catch (...)
        {
            controllerSlots.pop_back();
            throw;
        }
        freeControllerSlots.pop_back();
        record->id = id;
        record->owner = std::move(owner);
        return record->playback;
    }

    void PruneExpiredControllerPlayback()
    {
        for (std::size_t index = 0; index < controllerSlots.size();)
        {
            if (!controllerSlots[index]->owner.expired()) { ++index; continue; }
            ControllerSlot* retired = controllerSlots[index];
            controllerSlotById.erase(retired->id);
            if (index + 1 != controllerSlots.size())
            {
                controllerSlots[index] = controllerSlots.back();
                controllerSlotById.find(controllerSlots[index]->id)->second = index;
            }
            controllerSlots.pop_back();
            *retired = ControllerSlot{};
            freeControllerSlots.push_back(retired);
        }
    }

    void ResizePose(std::size_t boneCount)
    {
        const std::size_t paletteCount = (std::min)(boneCount, static_cast<std::size_t>(kMaxBones));
        localTransforms.resize(paletteCount, math::matrix4x4::identity());
        finalTransforms.resize(paletteCount, math::matrix4x4::identity());
        pose.Resize(boneCount);
    }

    [[nodiscard]] std::size_t GetAllocatedPoseBufferCount() const noexcept
    {
        return std::size_t(pose.has_storage())
            + std::size_t(l6PreviousPose.has_storage())
            + std::size_t(l6LatestPose.has_storage())
            + std::size_t(blendRecoverySourcePose.has_storage());
    }
};

// PHASE(SceneGraphRedesignPlan) 트랙 C3 — Animator 가상 Update 오버라이드의 시스템 이관.
//
// ── 무엇을 대신하는가 ──
//
// 예전에는 Animator가 Component::Update를 오버라이드해 Lifecycle::Registry의
// 오버라이드 감지 마스크(옛 Bit_Update — C3 완결로 철거)에 걸리고, Scene::RegisterComponent가
// SystemSchedule::SubscribeImplicit(component, Phase::Update)로 Scene 하나뿐인
// m_schedule.UpdateList()에 다른 컴포넌트(스크립트 포함)와 함께 섞어 넣었다.
// 여기서는 Animator 전용 조밀 std::vector<Animator*>를 따로 두고 한 번에
// 순회한다 — RenderScene::RegisterAnimator/AnimationJob의 스키닝용 등록부,
// AnimationJob의 잡 추적과는 목적이 다른 "로직 틱" 전용 등록부다(혼동 금지 —
// 그쪽은 건드리지 않는다).
//
// ── 등록/해지 훅 선택 근거 ──
//
// OnInitialized/OnUninitializing(Component.h 8훅 축, 컴포넌트당 1회 게이트)이 아니라
// OnAddedToScene/OnRemovingFromScene(6단계 축, 신설, 게이트 없음)에 건다.
// Animator::OnInitialized/OnUninitializing은 RenderScene 등록용으로 그대로 남기고(과제
// 지시대로 손대지 않는다), 이 시스템 등록은 별도 훅 쌍을 쓴다 — 이유는
// DDOL(DontDestroyOnLoad) 오브젝트가 씬을 건널 때다. OnInitialized는
// Component::State_Initialized 비트로 컴포넌트 평생 1회만 불리므로(
// Scene::DrainPendingPhases), DDOL 재부착 시 다시 불리지 않는다 —
// 만약 이 시스템 등록을 OnInitialized에 걸면 "최초 생성 씬"의 등록부에서만 존재하고
// 새 씬으로 넘어간 뒤에는 영원히 틱을 못 받는(추적에서 영구 이탈하는) 결함이
// 새로 생긴다. 반대로 OnAddedToScene/OnRemovingFromScene은 게이트가 없어
// 씬에 들고 날 때마다(최초 생성 때도, DDOL Detach/Attach 때도) 매번 불린다
// (Scene::DetachEntityHierarchy·AttachExistingEntity·
// AttachExistingEntityHierarchy가 각각 무조건 호출, Scene.cpp 확인).
// 실제 파괴 경로(FlushPendingDestroy)도 OnUninitializing 직전에
// OnRemovingFromScene을 먼저 부르므로, 이 시스템에서 빠지는 시점이 항상
// 실 파괴보다 먼저다 — 죽은 포인터를 틱할 창이 없다.
//
// ── 실행 시점(호출 위치)은 이 시스템의 소관 밖 ──
//
// Scene::Update가 일반 Update 페이즈(RegistryTick(m_schedule.UpdateList(), ...))
// 직후 이 Update(tick)를 불러야 실행 순서가 보존된다 — 17개 Animator 보유
// 프리팹 실측(Dynamic_CPP/Assets/Prefabs) 전부에서 스크립트(ModuleBehavior)가
// 루트 오브젝트에, Animator가 그 자식 오브젝트에 있고 루트가 먼저
// 인스턴스화·등록되므로, 옛 m_updateList 안에서도 스크립트 Update가 Animator
// Update보다 항상 먼저 돌았다(트리거 파라미터를 스크립트가 Update 안에서
// 세우고 Animator가 같은 프레임에 그 트리거를 소비 후 리셋하는 순서와 부합).
// Scene.cpp는 트랙 S2가 동시 편집 중이라 이 파일에서 직접 배선하지 않는다
// (배선 위치는 통합 보고서 참고).
class AnimatorSystem : public Singleton<AnimatorSystem>
{
    friend class Singleton<AnimatorSystem>;
    AnimatorSystem() = default;
    ~AnimatorSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — SystemSchedule의 암묵 경로와 달리
    // 이 등록부는 프레임마다 실제 로직(컨트롤러 Update+트리거 리셋)을 도는
    // 조밀 벡터라, 중복이 그대로 들어가면 같은 Animator가 한 프레임에 두 번
    // 틱해 트리거가 조기 소비되는 실질 오류가 된다(SystemSchedule처럼 "관측
    // 불변" 유지 목적의 중복 허용과는 사정이 다르다).
    void Register(Animator* animator);
    // 등록되어 있지 않으면 조용히 무시한다.
    void Unregister(Animator* animator);

    AnimInstanceHandle CreateInstance();
    void DestroyInstance(AnimInstanceHandle handle);
    [[nodiscard]] AnimInstance* ResolveInstance(AnimInstanceHandle handle) noexcept;

    // 등록된 Animator 전부를 한 번에 틱한다. 옛 Animator::Update와 동일한 가드
    // (owner 없음/파괴 표시/비활성 스킵)를 이 시스템이 대신 적용한다 — 예전에는
    // Scene::RegistryTick이 공통으로 해주던 가드였다.
    void Update(float tick);

    size_t GetCount() const noexcept { return m_animators.size(); }

private:
    void RequireOwnerThread() const;
    static constexpr std::size_t kInstancePageSize = 64;
    struct InstanceSlot final
    {
        std::uint32_t denseIndex{};
        std::uint32_t generation{ 1 };
        bool occupied{};
    };
    std::mutex m_instanceMutex{};
    std::vector<InstanceSlot> m_instanceSlots{};
    std::vector<std::uint32_t> m_freeInstanceSlots{};
    // Records are contiguous within each page and never relocate. The dense
    // pointer order can swap/pop without invalidating a worker's record address.
    std::vector<std::unique_ptr<AnimInstance[]>> m_instancePages{};
    std::vector<AnimInstance*> m_freeInstanceRecords{};
    std::vector<AnimInstance*> m_instances{};
    std::vector<Animator*> m_animators;
    const std::thread::id m_ownerThreadId{ std::this_thread::get_id() };
};

static auto AnimatorSystems = AnimatorSystem::GetInstance();
