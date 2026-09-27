#include "AnimationScheduler.h"
#include "ProfileScope.h"
#include "AnimationDiagnostics.h"
#include "JobScheduler.h"
#include "RenderScene.h"
#include "BoneRegion.h"
#include "SceneManager.h"
#include "Scene.h"
#include "CameraComponent.h"
#include "CameraSystem.h"
#include "Benchmark.hpp"
#include "AnimationController.h"
#include "AnimationPlayback.h"
#include "AnimationBudget.h"
#include "RuntimeSettings.h"
#include "TwoBoneIK.h"
#include "Animator.h"
#include "Socket.h"
#include "Assets/ModelAssetGeneration.h"   // PHASE 3.75 MBC8
#include "Assets/ModelAnimationSampler.h"  // PHASE 3.75 MBC8
#include <limits>
#include <stdexcept>
#include <span>
#include <cmath>
#include <atomic> // D34b: 루트 본 부재 1회 경고
#include <exception>
#include <utility>
#include "ModelConsumptionDiagnostics.h" // MBC10: 틱 경로 관측(읽기 전용 계수)
#include <mathematics/transform.hpp>
#include <mathematics/bounds.hpp>
#include <mathematics/scalar.hpp>
#include "Mathematics.Intersect.h"
#include <algorithm>
#include <optional>
#include <unordered_map>

namespace
{
    thread_local AnimationFrameMetrics* g_animationSample = nullptr;
    // enkiTS workers (and the thread helping a wait) retain these buffers
    // across Animator jobs. A job never yields while a pose is borrowed.
    struct AnimationWorkerPosePool final
    {
        Animation::LocalPose current{};
        Animation::LocalPose next{};
        Animation::LocalPose layer{};

        [[nodiscard]] std::size_t allocated_count() const noexcept
        {
            return std::size_t(current.has_storage())
                + std::size_t(next.has_storage())
                + std::size_t(layer.has_storage());
        }
    };
    thread_local AnimationWorkerPosePool g_workerPosePool;
    // Each Animator owns one timing slot. The owner reads it after both batches join.
    struct alignas(64) AnimationWorkerTiming
    {
        AnimationMeasurementScope::Clock::time_point m_updateBegin{}, m_updateEnd{};
        AnimationMeasurementScope::Clock::time_point m_executeBegin{}, m_executeEnd{};
        bool m_evaluated = false;
    };

    struct AnimationWork final
    {
        Animator* animator{};
        std::shared_ptr<const assets::ModelAssetGeneration> generation{};
        animation::quality_observation observation{};
        animation::quality_stage maximumStage{ animation::quality_stage::l0 };
        animation::quality_stage previousStage{ animation::quality_stage::l0 };
        std::array<double, animation::task_kind_count> taskTimesUs{};
        std::array<std::uint32_t, animation::task_kind_count> taskCounts{};
        double predictedUs{};
        double measuredUs{};
        double interpolatedUs{};
        bool budgetEligible{};
        bool transitioning{};
        bool forcedQuality{};
        bool requiredQuality{};
        bool interpolated{};
        bool prepared{};
    };
    thread_local AnimationWork* g_timedWork = nullptr;

    const char* TaskBufferOwner(const animation::task& item, bool layered)
    {
        using animation::task_kind;
        switch (item.kind)
        {
        case task_kind::sample_clip:
            return item.sample_slot == 1 ? "worker next" : "worker current";
        case task_kind::blend: return "worker current";
        case task_kind::materialize: return layered ? "worker layer" : "instance pose";
        case task_kind::make_additive: return "worker layer";
        case task_kind::output:
            return item.output_slot == animation::worker_current_output
                ? "worker current -> instance pose" : "instance pose";
        default: return "instance pose";
        }
    }

    struct QualityCamera final
    {
        CameraComponent* component{};
        FrameCameraSnapshot frame{};
        std::optional<math::bounding_frustum> frustum{};
        float orthographicWidth{ 1.f };
        float orthographicHeight{ 1.f };
    };

    animation::quality_observation ObserveQuality(Animator& animator,
        std::unordered_map<Scene*, std::vector<QualityCamera>>& cache)
    {
        if (!SceneManagers->IsGameStart())
            return { 1.f, true, false, true };
        Entity* owner = animator.GetOwner();
        Scene* scene = owner ? owner->GetScene() : nullptr;
        if (!scene) return { 1.f, true, false, false };
        auto [found, inserted] = cache.try_emplace(scene);
        if (inserted)
        {
            for (CameraComponent* component : scene->Cameras().GetRegisteredCameras())
            {
                if (!component || !component->IsEnabled()) continue;
                Entity* cameraOwner = component->GetOwner();
                if (!cameraOwner || cameraOwner->IsDestroyMark()
                    || cameraOwner->GetScene() != scene) continue;
                found->second.push_back({ component,
                    component->CaptureFrameSnapshot(), component->TryGetFrustum(),
                    component->GetCamera()->m_viewWidth,
                    component->GetCamera()->m_viewHeight });
            }
        }
        if (found->second.empty()) return { 1.f, true, false, false };

        const math::vector3 center = owner->Transform_().GetWorldPosition();
        const math::vector3 scale = owner->Transform_().GetWorldScale();
        const float radius = (std::max)(.01f, std::isfinite(animator.m_QualityRadius)
            ? animator.m_QualityRadius : 1.f)
            * (std::max)({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z) });
        const math::aabb bounds{ center, math::vector3{ radius, radius, radius } };
        animation::quality_observation result{};
        result.has_camera = true;
        for (const QualityCamera& camera : found->second)
        {
            const math::vector3 toCenter = center - camera.frame.eyePosition;
            const float forward = math::dot(toCenter, camera.frame.forward);
            const bool inFront = forward + radius > 0.f;
            bool visible = inFront && (!camera.frustum
                || math::intersects(*camera.frustum, bounds));
            if (camera.frame.isOrthographic)
            {
                visible = forward + radius >= camera.frame.nearPlane
                    && forward - radius <= camera.frame.farPlane
                    && std::abs(math::dot(toCenter, camera.frame.right))
                        <= (std::max)(camera.orthographicWidth, .001f) * .5f + radius
                    && std::abs(math::dot(toCenter, camera.frame.up))
                        <= (std::max)(camera.orthographicHeight, .001f) * .5f + radius;
            }
            if (!visible) continue;
            result.visible = true;
            const float distance = camera.component->CalculateLODDistance(center);
            float height = 1.f;
            if (camera.frame.isOrthographic)
                height = 2.f * radius / (std::max)(camera.orthographicHeight, .001f);
            else
            {
                const float tangent = std::tan(math::radians(camera.frame.fov) * .5f);
                if (std::isfinite(tangent) && tangent > .001f)
                    height = radius / ((std::max)(distance - radius, .001f) * tangent);
            }
            result.projected_height = (std::max)(result.projected_height,
                std::clamp(height, 0.f, 1.f));
        }
        return result;
    }

    void SnapshotTwoBoneIK(Animator& animator)
    {
        auto& instance = animator.GetInstance();
        instance.ikSnapshots.clear();
        const auto* skeleton = animator.TypedSkeleton();
        Entity* owner = animator.GetOwner();
        if (!skeleton || !owner || instance.qualityStage == animation::quality_stage::l7)
            return;
        const auto& authored = animator.m_TwoBoneIKConstraints;
        if (authored.empty()) return;
        instance.ikBindings.resize(authored.size());
        instance.ikSnapshots.reserve(authored.size());
        const std::uint64_t serial = animator.GetSkeletonSerial();
        const math::matrix4x4 inverseOwner = math::inverse(
            owner->Transform_().GetWorldMatrix());
        for (std::size_t index = 0; index < authored.size(); ++index)
        {
            const auto& constraint = authored[index];
            if (!constraint.Enabled || !std::isfinite(constraint.Weight)
                || constraint.Weight <= 0.f
                || !animation::finite(constraint.TargetWorld)
                || !animation::finite(constraint.PoleWorld)) continue;
            auto& binding = instance.ikBindings[index];
            if (binding.skeletonSerial != serial
                || binding.startName != constraint.StartBone
                || binding.middleName != constraint.MiddleBone
                || binding.endName != constraint.EndBone)
            {
                binding.skeletonSerial = serial;
                binding.startName = constraint.StartBone;
                binding.middleName = constraint.MiddleBone;
                binding.endName = constraint.EndBone;
                const int start = animator.ResolveBoneIndex(binding.startName);
                const int middle = animator.ResolveBoneIndex(binding.middleName);
                const int end = animator.ResolveBoneIndex(binding.endName);
                const bool valid = start >= 0 && middle >= 0 && end >= 0
                    && static_cast<std::size_t>(start) < skeleton->bones.size()
                    && static_cast<std::size_t>(middle) < skeleton->bones.size()
                    && static_cast<std::size_t>(end) < skeleton->bones.size()
                    && static_cast<std::size_t>(start) < kMaxBones
                    && static_cast<std::size_t>(middle) < kMaxBones
                    && static_cast<std::size_t>(end) < kMaxBones
                    && start < middle && middle < end
                    && skeleton->bones[middle].parent == static_cast<std::uint32_t>(start)
                    && skeleton->bones[end].parent == static_cast<std::uint32_t>(middle);
                binding.start = valid ? static_cast<std::uint32_t>(start)
                    : animation::invalid_task;
                binding.middle = valid ? static_cast<std::uint32_t>(middle)
                    : animation::invalid_task;
                binding.end = valid ? static_cast<std::uint32_t>(end)
                    : animation::invalid_task;
            }
            if (binding.start == animation::invalid_task) continue;
            const math::vector3 target = math::transform_point(
                constraint.TargetWorld, inverseOwner);
            const math::vector3 pole = math::transform_point(
                constraint.PoleWorld, inverseOwner);
            if (!animation::finite(target) || !animation::finite(pole)) continue;
            const float weight = std::clamp(constraint.Weight, 0.f, 1.f);
            if (weight <= 0.f) continue;
            instance.ikSnapshots.push_back({ binding.start, binding.middle,
                binding.end, target, pole, weight, constraint.Required });
        }
    }

    void SnapshotBoneTransforms(Animator& animator)
    {
        auto& instance = animator.GetInstance();
        instance.boneTransformSnapshots.clear();
        const auto* skeleton = animator.TypedSkeleton();
        if (!skeleton || instance.qualityStage == animation::quality_stage::l7) return;
        const auto& authored = animator.m_BoneTransformConstraints;
        instance.boneTransformBindings.resize(authored.size());
        instance.boneTransformSnapshots.reserve(authored.size());
        const std::uint64_t serial = animator.GetSkeletonSerial();
        for (std::size_t index = 0; index < authored.size(); ++index)
        {
            const auto& constraint = authored[index];
            if (!constraint.Enabled || !std::isfinite(constraint.Weight)
                || constraint.Weight <= 0.f
                || !animation::finite(constraint.TranslationOffset)
                || !animation::finite(constraint.ScaleMultiplier)
                || !std::isfinite(constraint.RotationOffset.x)
                || !std::isfinite(constraint.RotationOffset.y)
                || !std::isfinite(constraint.RotationOffset.z)
                || !std::isfinite(constraint.RotationOffset.w)
                || constraint.RotationOffset.x * constraint.RotationOffset.x
                    + constraint.RotationOffset.y * constraint.RotationOffset.y
                    + constraint.RotationOffset.z * constraint.RotationOffset.z
                    + constraint.RotationOffset.w * constraint.RotationOffset.w < 1.e-8f)
                continue;
            auto& binding = instance.boneTransformBindings[index];
            if (binding.skeletonSerial != serial || binding.boneName != constraint.Bone)
            {
                binding.skeletonSerial = serial;
                binding.boneName = constraint.Bone;
                const int bone = animator.ResolveBoneIndex(binding.boneName);
                binding.bone = bone >= 0
                    && static_cast<std::size_t>(bone) < skeleton->bones.size()
                    && static_cast<std::size_t>(bone) < kMaxBones
                    ? static_cast<std::uint32_t>(bone) : animation::invalid_task;
            }
            if (binding.bone == animation::invalid_task) continue;
            const float weight = std::clamp(constraint.Weight, 0.f, 1.f);
            if (weight <= 0.f) continue;
            instance.boneTransformSnapshots.push_back({ binding.bone,
                constraint.TranslationOffset, math::normalize(constraint.RotationOffset),
                constraint.ScaleMultiplier, weight, constraint.Required });
        }
    }
    void CommitQuality(Animator& animator, animation::quality_stage selected,
        animation::quality_observation observation, float deltaTime,
        bool forcedQuality)
    {
        auto& instance = animator.GetInstance();
        if (instance.qualityStage == animation::quality_stage::l7
            && selected != animation::quality_stage::l7)
            selected = animation::quality_stage::l0;
        if (instance.qualityStage == animation::quality_stage::l6
            && selected < animation::quality_stage::l6 && instance.l6Seeded
            && instance.l6PreviousPose.GetCount() == instance.pose.GetCount()
            && instance.l6LatestPose.GetCount() == instance.pose.GetCount())
        {
            // Matrix interpolation is cheap at L6. Reconstruct its published
            // local pose once on promotion, then start inertial recovery from
            // the last visible image rather than the latest raw sample.
            for (std::size_t bone = 0; bone < instance.pose.GetCount(); ++bone)
            {
                if (bone < instance.localTransforms.size())
                {
                    if (const auto value = math::decompose(instance.localTransforms[bone]))
                    {
                        instance.pose.SetTransform(bone, { value->translation,
                            value->rotation, value->scale });
                        continue;
                    }
                }
                instance.pose.SetTransform(bone, Animation::Blend(
                    instance.l6PreviousPose.GetTransform(bone),
                    instance.l6LatestPose.GetTransform(bone),
                    instance.l6PublishedAlpha));
            }
        }
        // The first bind-local reduction is blended from the last published
        // pose. Hold L5 until that bridge finishes before reducing tick rate.
        if (!forcedQuality && selected == animation::quality_stage::l6
            && (instance.qualityStage < animation::quality_stage::l5
                || (instance.qualityStage == animation::quality_stage::l5
                    && instance.blendRecoveryActive
                    && instance.blendRecoveryElapsed < .15f)))
            selected = animation::quality_stage::l5;
        if ((instance.qualityStage == animation::quality_stage::l4
                && instance.l4SnappedBlend
                && selected < animation::quality_stage::l4)
            || (instance.qualityStage >= animation::quality_stage::l5
                && instance.qualityStage < animation::quality_stage::l7
                && selected < animation::quality_stage::l5)
            || (!forcedQuality && instance.qualityStage < animation::quality_stage::l5
                && selected == animation::quality_stage::l5))
        {
            instance.blendRecoveryPending = true;
            instance.blendRecoveryActive = false;
            instance.blendRecoveryElapsed = 0.f;
        }
        else if (instance.blendRecoveryActive && std::isfinite(deltaTime))
            instance.blendRecoveryElapsed += (std::max)(deltaTime, 0.f);
        else if (selected >= animation::quality_stage::l4)
            instance.blendRecoveryPending = false;
        instance.significance = observation.visible ? observation.projected_height : 0.f;
        instance.projectedHeight = observation.projected_height;
        if (selected == animation::quality_stage::l6)
        {
            const std::uint8_t interval = observation.projected_height < .005f ? 4 : 2;
            if (instance.qualityStage != animation::quality_stage::l6
                || instance.l6Interval != interval)
            {
                instance.l6NextPhase = 0;
                instance.l6Seeded = false;
            }
            instance.l6Interval = interval;
            instance.l6Phase = instance.l6NextPhase;
            instance.l6Evaluate = instance.l6Phase == 0;
            instance.l6NextPhase = static_cast<std::uint8_t>(
                (instance.l6NextPhase + 1) % interval);
        }
        else
        {
            instance.l6NextPhase = 0;
            instance.l6Seeded = false;
            instance.l6Evaluate = true;
        }
        instance.qualityStage = selected;
        instance.optionalIKWeight = observation.editor_preview
            || (!observation.has_camera && instance.qualityOverride < 0)
            || animator.m_ForceFullQuality
            ? 1.f : animation::advance_optional_ik_weight(
                instance.optionalIKWeight, selected, deltaTime);
        if (selected == animation::quality_stage::l7
            || (selected == animation::quality_stage::l6 && !instance.l6Evaluate))
        {
            instance.executedPoseSamples = 0;
            instance.evaluatedBoneSamples = 0;
        }
    }
}

struct AnimationScheduler::Scratch final
{
    std::vector<AnimationWork> work;
    std::vector<AnimationWorkerTiming> timings;
    std::vector<std::size_t> budgetOrder;
    std::vector<std::size_t> executeIndices;
};

namespace
{
    std::size_t AnimationChunkCount(std::size_t items)
    {
        if (!items) return 0;
        const auto workers = (std::max)(std::size_t{ 1 }, ce::get_thread_pool().size());
        return (std::min)(items, workers * 2);
    }
}

AnimationMeasurementScope::AnimationMeasurementScope(AnimationFrameMetrics& sample)
    : m_previous(g_animationSample)
{
    sample = {};
    g_animationSample = &sample;
}
AnimationMeasurementScope::~AnimationMeasurementScope() { g_animationSample = m_previous; }
AnimationFrameMetrics* AnimationMeasurementScope::Current() { return g_animationSample; }

AnimationScheduler::AnimationScheduler(SceneManager& owner)
    : m_owner(owner), m_taskCostModel(std::make_unique<animation::task_cost_model>()),
      m_scratch(std::make_unique<Scratch>())
{
	m_sceneLoadedHandle = m_owner.sceneLoadedEvent.AddRaw(this, &AnimationScheduler::PrepareAnimation);
    m_AnimationUpdateHandle = m_owner.InternalAnimationUpdateEvent.AddRaw(this, &AnimationScheduler::Update);
	m_sceneUnloadedHandle = m_owner.sceneUnloadedEvent.AddRaw(this, &AnimationScheduler::CleanUp);
}

AnimationScheduler::~AnimationScheduler()
{
	m_owner.sceneLoadedEvent.Remove(m_sceneLoadedHandle);
	m_owner.InternalAnimationUpdateEvent.Remove(m_AnimationUpdateHandle);
    m_owner.sceneUnloadedEvent.Remove(m_sceneUnloadedHandle);
}

void AnimationScheduler::RequireOwnerThread() const
{
    if (std::this_thread::get_id() != m_ownerThreadId)
        throw std::logic_error("AnimationScheduler registration and Update require the owner thread");
}

void AnimationScheduler::Finalize()
{
	RequireOwnerThread();
	{
		std::lock_guard<std::mutex> lock(m_animatorMutex);
		m_animators.clear();
		m_snapshotAnimators.clear();
	}
	{
		std::lock_guard<std::mutex> lock(m_hudMutex);
		m_hudPublished.reset();
	}
}

void AnimationScheduler::RegisterAnimator(Animator* animator)
{
	if (nullptr == animator) return;
	RequireOwnerThread();
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	m_animators[animator->GetInstanceID()] = animator;
}

void AnimationScheduler::UnregisterAnimator(Animator* animator)
{
	if (nullptr == animator) return;
	RequireOwnerThread();
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	m_animators.erase(animator->GetInstanceID());
}

size_t AnimationScheduler::GetAnimatorCount() const
{
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	return m_animators.size();
}

void AnimationScheduler::RequestHudCapture(std::uint64_t animatorId) noexcept
{
    m_hudTargetAnimatorId.store(animatorId, std::memory_order_relaxed);
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        AnimationMeasurementScope::Clock::now().time_since_epoch()).count();
    m_hudRequestedAtNs.store(now, std::memory_order_release);
}

std::shared_ptr<const AnimationHudSnapshot> AnimationScheduler::GetHudSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_hudMutex);
    return m_hudPublished;
}

void AnimationScheduler::SnapshotAnimators()
{
	// K2: 프레임-로컬 raw 포인터 스냅샷.
	//
	// 안전 근거 — 잡 실행 창과 컴포넌트 소멸 창은 겹치지 않는다:
	// SceneManager::GameLogic이 InternalAnimationUpdateEvent를 Broadcast하면
	// AnimationScheduler::Update가 이 스냅샷으로 스레드 풀에 작업을 흘리고
	// 이 프레임의 job_handle.wait()로 완결된다. 실제 소멸(Scene::EndFramePass →
	// FlushPendingDestroy → DestroyComponents의 component.reset())은 같은 게임
	// 스레드의 그 뒤(EditorMain::Update의 DisableOrEnable)에서만 일어나므로,
	// 여기서 담아 스레드 풀 람다에 넘기는 raw 포인터는 그 잡이 완료될 때까지
	// 항상 살아 있다. 그래도 이번 프레임에 파괴 예약된 항목은 미리 걸러 낸다.
	m_snapshotAnimators.clear();
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	m_snapshotAnimators.reserve(m_animators.size());
	for (const auto& [instanceID, animator] : m_animators)
	{
		if (nullptr == animator || animator->IsDestroyMark()) continue;
		m_snapshotAnimators.push_back(animator);
	}
}

void AnimationScheduler::Update(float deltaTime)
{
    RequireOwnerThread();
    ++m_updateSequence;
    const auto requestedAt = m_hudRequestedAtNs.load(std::memory_order_acquire);
    const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        AnimationMeasurementScope::Clock::now().time_since_epoch()).count();
    const bool captureHud = requestedAt > 0 && nowNs >= requestedAt
        && nowNs - requestedAt <= 500'000'000;
    const std::uint64_t requestedAnimatorId = captureHud
        ? m_hudTargetAnimatorId.load(std::memory_order_relaxed) : 0;
    bool detailedAssigned = false;
    using Clock = AnimationMeasurementScope::Clock;
    auto* sample = AnimationMeasurementScope::Current();
    const auto begin = sample ? Clock::now() : Clock::time_point{};
    const AnimationBudgetSettings settings = RuntimeSettings::TryGet()
        ? RuntimeSettings::Get().GetAnimationBudgetSettings()
        : AnimationBudgetSettings{};
    SnapshotAnimators();
    const auto& currentAnimators = m_snapshotAnimators;
    // Build stable captures before either job group is submitted. The owner
    // thread cannot detach an Animator until both groups have joined.
    auto& work = m_scratch->work;
    work.clear();
    work.reserve(currentAnimators.size());
    std::unordered_map<Scene*, std::vector<QualityCamera>> qualityCameras;
    for (Animator* animator : currentAnimators)
    {
        if (nullptr == animator || !animator->IsEnabled()) continue;
        const bool hasExpiredController = std::any_of(animator->m_animationControllers.begin(),
            animator->m_animationControllers.end(),
            [](const std::shared_ptr<AnimationController>& controller) { return !controller; });
        if (hasExpiredController) continue;
        auto& instance = animator->GetInstance();
        instance.captureTaskExecution = captureHud && !detailedAssigned
            && (requestedAnimatorId == 0
                || requestedAnimatorId == animator->GetInstanceID());
        if (instance.captureTaskExecution)
        {
            instance.executedTaskIndices.clear();
            detailedAssigned = true;
        }
        const auto observation = ObserveQuality(*animator, qualityCameras);
        animation::quality_stage selected = animation::choose_quality_hysteretic(
            observation, instance.qualityStage, settings.hysteresis);
        const bool hasRequiredIK = std::any_of(
            animator->m_TwoBoneIKConstraints.begin(),
            animator->m_TwoBoneIKConstraints.end(),
            [](const TwoBoneIKConstraint& constraint)
            {
                return constraint.Enabled && constraint.Required
                    && std::isfinite(constraint.Weight) && constraint.Weight > 0.f;
            });
        const bool hasRequiredBoneTransform = std::any_of(
            animator->m_BoneTransformConstraints.begin(),
            animator->m_BoneTransformConstraints.end(),
            [](const BoneTransformConstraint& constraint)
            {
                return constraint.Enabled && constraint.Required
                    && std::isfinite(constraint.Weight) && constraint.Weight > 0.f;
            });
        const bool forcedQuality = !observation.editor_preview
            && instance.qualityOverride >= 0;
        if (animator->m_ForceFullQuality || hasRequiredIK || hasRequiredBoneTransform)
            selected = animation::quality_stage::l0;
        else if (forcedQuality)
        {
            const int forced = instance.qualityOverride;
            selected = forced == 7 ? animation::quality_stage::l7
                : static_cast<animation::quality_stage>(std::clamp(forced, 0, 6));
        }
        else if (selected >= animation::quality_stage::l5
            && selected != animation::quality_stage::l7)
        {
            const auto* skeleton = animator->TypedSkeleton();
            if (!skeleton || animator->m_LowDetailBoneCount == 0
                || animator->m_LowDetailBoneCount >= skeleton->bones.size())
                selected = animation::quality_stage::l4;
        }
        if (!forcedQuality && selected >= animation::quality_stage::l4
            && selected != animation::quality_stage::l7)
        {
            const bool transitioning = instance.control.isBlending
                || std::any_of(animator->m_animationControllers.begin(),
                    animator->m_animationControllers.end(),
                    [](const std::shared_ptr<AnimationController>& controller)
                    { return controller && controller->useController && controller->IsBlending(); });
            if (transitioning)
                selected = static_cast<animation::quality_stage>(
                    static_cast<int>(selected) - 1);
        }
        const auto previousStage = instance.qualityStage;
        instance.significance = observation.visible ? observation.projected_height : 0.f;
        instance.projectedHeight = observation.projected_height;
        instance.qualityStage = selected == animation::quality_stage::l7
            ? selected : animation::quality_stage::l0;
        SnapshotTwoBoneIK(*animator);
        SnapshotBoneTransforms(*animator);
        AnimationWork item{};
        item.animator = animator;
        item.generation = animator->m_modelGeneration;
        item.observation = observation;
        item.maximumStage = selected;
        item.previousStage = previousStage;
        item.budgetEligible = !forcedQuality && !observation.editor_preview
            && observation.has_camera && observation.visible
            && !animator->m_ForceFullQuality && !hasRequiredIK
            && !hasRequiredBoneTransform;
        item.transitioning = instance.control.isBlending
            || std::any_of(animator->m_animationControllers.begin(),
                animator->m_animationControllers.end(),
                [](const std::shared_ptr<AnimationController>& controller)
                { return controller && controller->useController && controller->IsBlending(); });
        item.forcedQuality = forcedQuality;
        item.requiredQuality = hasRequiredIK || hasRequiredBoneTransform;
        work.push_back(std::move(item));
    }
    auto& timings = m_scratch->timings;
    if (sample)
    {
        timings.resize(work.size());
        std::fill(timings.begin(), timings.end(), AnimationWorkerTiming{});
    }

    const auto updateJobCount = AnimationChunkCount(work.size());
    const auto updateReady = sample ? Clock::now() : Clock::time_point{};
    auto updateCompletion = ce::get_job_scheduler().submit_indexed(updateJobCount,
        [this, &work, &timings, sample, deltaTime, updateJobCount](std::size_t chunk)
    {
        ce::profile_scope _profile{ ce::marker<"AnimationScheduler.Update">() };
        std::exception_ptr failure;
        for (std::size_t index = chunk; index < work.size(); index += updateJobCount)
        {
            AnimationWork* item = &work[index];
            AnimationWorkerTiming* timing = sample ? &timings[index] : nullptr;
            try
            {
                if (timing) timing->m_updateBegin = Clock::now();
                const assets::ModelSkeletonAsset* typedSkeleton =
                    item->generation ? item->generation->Skeleton() : nullptr;
                if (!item->animator->GetInstance().tickPathLogged)
                {
                    item->animator->GetInstance().tickPathLogged = true;
                    ModelConsumptionDiagnostics::NoteTickPath(nullptr != typedSkeleton);
                }
                if (nullptr != typedSkeleton)
                {
                    auto& control = item->animator->GetPlaybackControl();
                    const auto& instance = item->animator->GetInstance();
                    float deltaT = SceneManagers->IsGameStart()
                        || instance.editorPreviewPlaying ? deltaTime : 0.f;
                    if (control.stopTimer > 0.f)
                    {
                        control.stoppedDuration += deltaT;
                        control.stopTimer -= deltaT;
                        deltaT = 0.f;
                        if (control.stopTimer <= 0.f)
                        {
                            deltaT = control.stoppedDuration;
                            control.stoppedDuration = 0.f;
                        }
                    }
                    PrepareGeneration(*item->animator, *item->generation, deltaT);
                    item->prepared = true;
                }
                if (timing) timing->m_updateEnd = Clock::now();
            }
            catch (...)
            {
                if (!failure) failure = std::current_exception();
            }
        }
        if (failure) std::rethrow_exception(failure);
    });
    const auto updateSubmitted = sample ? Clock::now() : Clock::time_point{};
    updateCompletion.wait();
    const auto updateJoined = sample ? Clock::now() : Clock::time_point{};

    // This is the inter-Animator barrier: every event/time recipe is
    // complete before any pose math starts. A failed first group prevents the
    // second submission, and both groups finish before scene publication.
    double remainingUs = settings.cpuBudgetMs * 1000.;
    if (sample) sample->m_budgetUs = remainingUs;
    const auto estimate = [&](AnimationWork& item, animation::quality_stage stage)
    {
        const auto& instance = item.animator->GetInstance();
        const auto* skeleton = item.generation ? item.generation->Skeleton() : nullptr;
        if (!skeleton) return 0.;
        const auto fullBones = static_cast<std::uint32_t>(skeleton->bones.size());
        const auto lowBones = item.animator->m_LowDetailBoneCount > 0
            && item.animator->m_LowDetailBoneCount < fullBones
            ? item.animator->m_LowDetailBoneCount : fullBones;
        const std::uint8_t interval = item.observation.projected_height < .005f ? 4 : 2;
        return m_taskCostModel->recipe_cost(instance.taskList, stage,
            fullBones, lowBones, item.transitioning,
            instance.control.blendT >= .5f, interval);
    };
    const auto commit = [&](AnimationWork& item, animation::quality_stage stage)
    {
        auto& instance = item.animator->GetInstance();
        instance.qualityStage = item.previousStage;
        CommitQuality(*item.animator, stage, item.observation, deltaTime,
            item.forcedQuality);
        item.predictedUs = item.prepared ? estimate(item, instance.qualityStage) : 0.;
        if (item.prepared)
            FinalizeGenerationQuality(*item.animator, *item.generation);
        if (work.size() > 1 && item.budgetEligible
            && item.previousStage != animation::quality_stage::l6
            && instance.qualityStage == animation::quality_stage::l6)
        {
            // All newly visible instances need one real sample, then spread
            // later L6 evaluations across the interval instead of bunching
            // every actor on the same frame.
            instance.l6NextPhase = static_cast<std::uint8_t>(
                (1u + instance.slot % instance.l6Interval) % instance.l6Interval);
        }
        remainingUs -= item.predictedUs;
        if (sample)
        {
            sample->m_predictedPoseUs += item.predictedUs;
            sample->m_budgetEligible += item.budgetEligible ? 1 : 0;
            sample->m_budgetDegraded += item.budgetEligible
                && instance.qualityStage != animation::quality_stage::l0 ? 1 : 0;
            ++sample->m_qualityStageCounts[static_cast<std::size_t>(instance.qualityStage)];
        }
    };
    // Reserve mandatory/editor/forced poses first. They cannot be degraded to
    // hide an overrun; the eligible set receives the remaining budget.
    for (auto& item : work)
        if (!item.budgetEligible || !item.prepared)
            commit(item, item.maximumStage);
    auto& budgetOrder = m_scratch->budgetOrder;
    budgetOrder.clear();
    for (std::size_t index = 0; index < work.size(); ++index)
        if (work[index].budgetEligible && work[index].prepared)
            budgetOrder.push_back(index);
    std::stable_sort(budgetOrder.begin(), budgetOrder.end(), [&](std::size_t a, std::size_t b)
    {
        return work[a].animator->GetInstance().significance
            > work[b].animator->GetInstance().significance;
    });
    for (const std::size_t index : budgetOrder)
    {
        auto& item = work[index];
        auto& instance = item.animator->GetInstance();
        animation::quality_stage chosen = item.maximumStage;
        if (m_taskCostModel->calibrated())
        {
            const auto choice = animation::choose_budget_stage(item.maximumStage,
                item.previousStage, remainingUs, instance.budgetPromotionFrames,
                settings.promotionGraceFrames, settings.hysteresis,
                [&](animation::quality_stage stage) { return estimate(item, stage); });
            chosen = choice.stage;
        }
        commit(item, chosen);
    }
    auto& executeIndices = m_scratch->executeIndices;
    executeIndices.clear();
    executeIndices.reserve(work.size());
    for (std::size_t index = 0; index < work.size(); ++index)
    {
        AnimationWork* item = &work[index];
        if (!item->prepared) continue;
        if (item->animator->GetInstance().qualityStage == animation::quality_stage::l7)
            continue;
        executeIndices.push_back(index);
    }
    const auto executeJobCount = AnimationChunkCount(executeIndices.size());
    const auto executeReady = sample ? Clock::now() : Clock::time_point{};
    auto executeCompletion = ce::get_job_scheduler().submit_indexed(executeJobCount,
        [this, &work, &timings, &executeIndices, sample, executeJobCount](std::size_t chunk)
    {
        ce::profile_scope _profile{ ce::marker<"AnimationScheduler.Execute">() };
        std::exception_ptr failure;
        for (std::size_t position = chunk; position < executeIndices.size(); position += executeJobCount)
        {
            AnimationWork* item = &work[executeIndices[position]];
            AnimationWorkerTiming* timing = sample ? &timings[executeIndices[position]] : nullptr;
            try
            {
                if (timing) timing->m_executeBegin = Clock::now();
                struct TimingBinding final
                {
                    AnimationWork* previous;
                    explicit TimingBinding(AnimationWork* current) : previous(g_timedWork)
                    { g_timedWork = current; }
                    ~TimingBinding() { g_timedWork = previous; }
                } binding{ item };
                if (item->animator->GetInstance().qualityStage == animation::quality_stage::l6
                    && !item->animator->GetInstance().l6Evaluate)
                {
                    const auto interpolationBegin = Clock::now();
                    InterpolateGeneration(*item->animator, *item->generation);
                    item->interpolatedUs = AnimationMeasurementScope::Microseconds(
                        interpolationBegin, Clock::now());
                    item->interpolated = true;
                }
                else ExecuteGeneration(*item->animator, *item->generation);
                if (timing)
                {
                    timing->m_executeEnd = Clock::now();
                    timing->m_evaluated = true;
                }
            }
            catch (...)
            {
                if (!failure) failure = std::current_exception();
            }
        }
        if (failure) std::rethrow_exception(failure);
    });
    const auto executeSubmitted = sample ? Clock::now() : Clock::time_point{};
    executeCompletion.wait();
    const auto joined = sample ? Clock::now() : Clock::time_point{};
    for (auto& item : work)
    {
        const auto* skeleton = item.generation ? item.generation->Skeleton() : nullptr;
        if (!item.prepared || !skeleton) continue;
        const auto bones = item.animator->GetInstance().activeBoneCount;
        for (std::size_t kind = 0; kind < animation::task_kind_count; ++kind)
        {
            if (!item.taskCounts[kind]) continue;
            m_taskCostModel->observe(static_cast<animation::task_kind>(kind),
                item.taskTimesUs[kind] / item.taskCounts[kind], bones,
                static_cast<std::uint32_t>(skeleton->bones.size()));
            item.measuredUs += item.taskTimesUs[kind];
        }
        if (item.interpolated)
        {
            m_taskCostModel->observe_interpolation(item.interpolatedUs,
                static_cast<std::uint32_t>(skeleton->bones.size()));
            item.measuredUs = item.interpolatedUs;
        }
        if (sample) sample->m_measuredPoseUs += item.measuredUs;
    }
    if (sample) sample->m_budgetOverrun = sample->m_measuredPoseUs > sample->m_budgetUs ? 1 : 0;
    if (sample)
    {
        sample->m_jobs = updateJobCount;
        sample->m_updatePassJobs = updateJobCount;
        sample->m_executePassJobs = executeJobCount;
        sample->m_prepareUs = AnimationMeasurementScope::Microseconds(begin, updateReady)
            + AnimationMeasurementScope::Microseconds(updateJoined, executeReady);
        sample->m_submitUs = AnimationMeasurementScope::Microseconds(updateReady, updateSubmitted)
            + AnimationMeasurementScope::Microseconds(executeReady, executeSubmitted);
        sample->m_updatePassWaitUs = AnimationMeasurementScope::Microseconds(updateSubmitted, updateJoined);
        sample->m_executePassWaitUs = AnimationMeasurementScope::Microseconds(executeSubmitted, joined);
        sample->m_waitUs = sample->m_updatePassWaitUs + sample->m_executePassWaitUs;
        auto first = joined, last = begin;
        for (std::size_t i = 0; i < work.size(); ++i)
        {
            sample->m_updatePassWorkerSumUs += AnimationMeasurementScope::Microseconds(
                timings[i].m_updateBegin, timings[i].m_updateEnd);
            if (timings[i].m_evaluated)
                sample->m_executePassWorkerSumUs += AnimationMeasurementScope::Microseconds(
                    timings[i].m_executeBegin, timings[i].m_executeEnd);
            sample->m_evaluatedAnimators += timings[i].m_evaluated ? 1 : 0;
            if (timings[i].m_evaluated)
                sample->m_poseStorageGrowths += work[i].animator->GetInstance().poseStorageGrowths;
            first = std::min(first, timings[i].m_updateBegin);
            last = std::max(last, timings[i].m_evaluated
                ? timings[i].m_executeEnd : timings[i].m_updateEnd);
        }
        sample->m_workerSumUs = sample->m_updatePassWorkerSumUs
            + sample->m_executePassWorkerSumUs;
        if (updateJobCount) sample->m_workerSpanUs = AnimationMeasurementScope::Microseconds(first, last);
    }
    if (captureHud)
    {
        auto hud = std::make_shared<AnimationHudSnapshot>();
        hud->frame = m_updateSequence;
        hud->registered = currentAnimators.size();
        hud->budgetUs = settings.cpuBudgetMs * 1000.;
        hud->animators.reserve(work.size());
        for (const auto& item : work)
        {
            const auto& instance = item.animator->GetInstance();
            const auto stage = instance.qualityStage;
            const bool evaluated = item.prepared && stage != animation::quality_stage::l7;
            hud->evaluated += evaluated ? 1 : 0;
            hud->degraded += stage != animation::quality_stage::l0 ? 1 : 0;
            ++hud->stages[static_cast<std::size_t>(stage)];
            hud->predictedUs += item.predictedUs;
            hud->measuredUs += item.measuredUs;
            const char* reason = item.observation.editor_preview ? "Editor preview"
                : item.animator->m_ForceFullQuality ? "Always full quality"
                : item.requiredQuality ? "Required correction"
                : item.forcedQuality ? "Quality override"
                : !item.observation.has_camera ? "No camera"
                : !item.observation.visible ? "Outside camera"
                : item.budgetEligible && stage > item.maximumStage ? "CPU budget"
                : stage != animation::quality_stage::l0 ? "Significance"
                : "Full quality";
            hud->animators.push_back({
                item.animator->GetInstanceID(),
                item.animator->GetOwner() ? item.animator->GetOwner()->ToString() : "Animator",
                stage, reason, item.predictedUs, item.measuredUs,
                item.observation.projected_height, item.observation.visible,
                evaluated, item.interpolated });

            if (!instance.captureTaskExecution || !item.prepared) continue;
            hud->selectedAnimatorId = item.animator->GetInstanceID();
            hud->workerPosePool = instance.workerPosePoolIdentity;
            hud->workerCurrentStorage = reinterpret_cast<std::uintptr_t>(
                instance.workerCurrentStorage[0]);
            hud->instancePoseStorage = reinterpret_cast<std::uintptr_t>(
                instance.pose.GetTranslations().data());
            hud->workerPoseBuffers = instance.workerPoseBufferCount;
            hud->instancePoseBuffers = instance.GetAllocatedPoseBufferCount();
            std::vector<std::uint32_t> executionOrder(instance.taskList.size(),
                animation::invalid_task);
            for (std::size_t order = 0; order < instance.executedTaskIndices.size(); ++order)
                executionOrder[instance.executedTaskIndices[order]] =
                    static_cast<std::uint32_t>(order);
            hud->tasks.reserve(instance.taskList.size());
            instance.taskList.for_each_task_snapshot(
                [&](std::uint32_t index, const animation::task& task, bool reachable)
            {
                hud->tasks.push_back({ index, task.kind, task.dependency_a,
                    task.dependency_b, task.output_slot, task.clip_index,
                    task.sample_slot, reachable, executionOrder[index],
                    TaskBufferOwner(task, item.animator->UsesMultipleControllers()) });
            });
        }
        std::lock_guard<std::mutex> lock(m_hudMutex);
        m_hudPublished = std::move(hud);
    }
    work.clear(); // Release generation references after both groups finish.

	// X7 — worker는 Animator 소유 pose/socket staging만 쓴다. Scene packed
	// storage와 부착 오브젝트 Transform은 모든 job이 끝난 이 barrier 뒤에서
	// 메인 스레드가 직렬 commit한다. 따라서 worker-local queue를 따로 만들지
	// 않아도 resolver/파괴/다른 Animator와 Scene write가 겹치지 않는다.
	for (Animator* animator : currentAnimators)
	{
		if (!animator || animator->IsDestroyMark() || !animator->IsEnabled()) continue;
		Entity* owner = animator->GetOwner();
		if (!owner || owner->IsDestroyMark()) continue;
		if (Scene* scene = owner->GetScene())
		{
            const auto publishBegin = sample ? Clock::now() : Clock::time_point{};
            const auto metrics = scene->PublishAnimatorPose(*animator);
            if (sample)
            {
                sample->m_publishUs += AnimationMeasurementScope::Microseconds(publishBegin, Clock::now());
                sample->m_validBones += metrics.validBones;
                sample->m_localWrites += metrics.localWrites;
            }
		}

		if (!animator->HasSocket() || !SceneManagers->m_isGameStart) continue;
        const auto socketBegin = sample ? Clock::now() : Clock::time_point{};
		for (Socket* socket : animator->socketvec)
		{
			if (!socket) continue;
			socket->transform.SetLocalMatrix(
				socket->m_boneMatrix, TransformWriteReason::Animator);
			socket->Update();
		}
        if (sample) sample->m_socketUs += AnimationMeasurementScope::Microseconds(socketBegin, Clock::now());
	}
    if (sample) sample->m_updateUs = AnimationMeasurementScope::Microseconds(begin, Clock::now());
}

void AnimationScheduler::PrepareAnimation()
{
	RequireOwnerThread();
	// 로드 이벤트는 만료 weak 참조를 정리하는 경계로만 쓴다. 렌더 씬의
	// registry를 당겨 오지 않는다 — animator는 게임/animation 소유다.
	(void)SnapshotAnimators();
}

void AnimationScheduler::CleanUp()
{
	RequireOwnerThread();
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	m_animators.clear();
	m_snapshotAnimators.clear();
	m_objectSize = 0;
}

// I6-B4b — legacy 재귀 틱(UpdateBlendBone/UpdateBone/UpdateBoneLayer/
// calculAni)을 걷었다. 재생 경로는 하나다 — 데이터 출처만 둘(typed generation ·
// experiment)이고, PHASE 3.75 MBC8은 그 둘을 아래 뷰 템플릿 하나에 태운다.
//
// ★ 왜 템플릿 뷰인가: 틱 본문(컨트롤러·블렌드·레이어·소켓 ~400줄)을 데이터
//   타입마다 복제하면 두 판이 갈리는 순간 "골든은 통과하는데 화면은 다른" 상태가
//   된다. 본문은 하나, 뷰는 둘(experiment 뷰는 MBC9와 함께 죽는다). typed 뷰의
//   포즈 산술은 experiment와 비트 동일해야 하고 animtick 골든(6b)이 그것을 잰다.
namespace
{
    constexpr std::uint32_t kPoseNoParent = (std::numeric_limits<std::uint32_t>::max)();

    struct GenerationPoseSource final
    {
        using Clip = assets::ModelAnimationAsset;
        using Track = assets::ModelAnimationTrack;
        const assets::ModelSkeletonAsset& skeleton;
        std::span<const assets::ModelAnimationAsset> clips;
        const assets::ModelAssetGeneration& generation;

        std::size_t BoneCount() const noexcept { return skeleton.bones.size(); }
        std::uint32_t Parent(std::size_t index) const noexcept
        {
            const std::uint32_t parent = skeleton.bones[index].parent;
            // 게시 계약은 parent < index다 — 어긋난 값은 루트 취급(안전).
            return (parent != assets::kInvalidModelAssetIndex && parent < index)
                ? parent : kPoseNoParent;
        }
        const math::matrix4x4& InverseBind(std::size_t index) const noexcept
        { return skeleton.bones[index].inverseBindMatrix; }
        const std::string& BoneName(std::size_t index) const noexcept
        { return skeleton.bones[index].name; }
        const math::matrix4x4& RootTransform() const noexcept { return skeleton.rootTransform; }
        const math::matrix4x4& GlobalInverse() const noexcept { return skeleton.globalInverseTransform; }
        std::size_t ClipCount() const noexcept { return clips.size(); }
        const Clip* ClipAt(int index) const noexcept
        {
            return index >= 0 && static_cast<std::size_t>(index) < clips.size()
                ? &clips[static_cast<std::size_t>(index)] : nullptr;
        }
        static double Duration(const Clip& clip) noexcept { return clip.durationTicks; }
        static double TicksPerSecond(const Clip& clip) noexcept { return clip.ticksPerSecond; }
        std::span<const Track* const> TracksAt(int clipIndex) const noexcept
        { return generation.AnimationTracks(clipIndex); }
        static Animation::LocalTransform SampleLocalTransform(const Track& track, double time,
            Animation::TrackKeyCursor& cursor)
        {
            // Short authored channels cost less with the established linear
            // sampler in the 100-actor product path. Keep cursors for clips
            // whose channels are long enough to amortize their state traffic.
            constexpr std::size_t kCursorThreshold = 32;
            if (track.translations.size() <= kCursorThreshold
                && track.rotations.size() <= kCursorThreshold
                && track.scales.size() <= kCursorThreshold)
                return assets::animation::SampleLocalTransform(track, time);
            return assets::animation::SampleLocalTransform(track, time, cursor);
        }
    };

    template <class Source>
    bool PrepareBindLocals(Animator& animator, const Source& source)
    {
        auto& instance = animator.GetInstance();
        const std::size_t boneCount = source.BoneCount();
        const std::uint64_t serial = animator.GetSkeletonSerial();
        if (instance.bindSkeletonSerial == serial
            && instance.bindLocals.size() == boneCount) return true;

        instance.bindLocals.clear();
        instance.bindSkeletonSerial = 0;
        std::vector<math::matrix4x4> bindGlobals(boneCount);
        std::vector<Animation::LocalTransform> locals(boneCount);
        const math::matrix4x4 inverseGlobal = math::inverse(source.GlobalInverse());
        for (std::size_t bone = 0; bone < boneCount; ++bone)
        {
            const math::matrix4x4 global = math::inverse(source.InverseBind(bone))
                * inverseGlobal;
            const std::uint32_t parent = source.Parent(bone);
            const math::matrix4x4 parentGlobal = parent != kPoseNoParent
                ? bindGlobals[parent] : source.RootTransform();
            const math::matrix4x4 local = global * math::inverse(parentGlobal);
            const auto decomposed = math::decompose(local);
            if (!decomposed) return false;
            const math::matrix4x4 reconstructed = math::compose(
                decomposed->scale, decomposed->rotation, decomposed->translation);
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column)
                    if (!std::isfinite(local(row, column))
                        || std::abs(reconstructed(row, column) - local(row, column)) > .02f)
                        return false;
            locals[bone] = { decomposed->translation,
                decomposed->rotation, decomposed->scale };
            bindGlobals[bone] = global;
        }
        instance.bindLocals = std::move(locals);
        instance.bindSkeletonSerial = serial;
        return true;
    }

    template <class Source>
    bool SampleClipPose(Animator& animator, const Source& source,
        int clipIndex, float time, std::array<Animation::ClipSamplingCursor, 2>& cursors,
        std::uint8_t sampleSlot, int referenceClipIndex = -1,
        bool useDetailReduction = false)
    {
        using Track = typename Source::Track;
        if (nullptr == source.ClipAt(clipIndex)) return false;

        const std::size_t boneCount = source.BoneCount();
        auto& instance = animator.GetInstance();
        const std::size_t activeBones = useDetailReduction
            ? (std::min)(boneCount, static_cast<std::size_t>(instance.activeBoneCount))
            : boneCount;
        instance.ResizePose(boneCount);
        const auto trackOf = source.TracksAt(clipIndex);
        const auto referenceTracks = referenceClipIndex >= 0
            ? source.TracksAt(referenceClipIndex)
            : std::span<const Track* const>{};
        auto& sampledPose = sampleSlot == 1
            ? g_workerPosePool.next : g_workerPosePool.current;
        sampledPose.Resize(boneCount);
        const auto sampledCursors = cursors[sampleSlot].Prepare(clipIndex, boneCount);
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            if (boneIndex >= activeBones)
            {
                sampledPose.SetTransform(boneIndex, instance.bindLocals[boneIndex]);
                continue;
            }
            Animation::LocalTransform local{};
            // A next-only channel never contributed to the legacy blend.
            if (const Track* track = trackOf[boneIndex]; track
                && (referenceTracks.empty() || referenceTracks[boneIndex]))
            {
                local = Source::SampleLocalTransform(*track, time,
                    sampledCursors[boneIndex]);
                if (useDetailReduction) ++instance.evaluatedBoneSamples;
            }
            else if (sampleSlot == 0 && referenceClipIndex < 0
                && !animator.UsesMultipleControllers())
                // The output task transfers this buffer into the instance.
                // Preserve the previous value for channels that never write.
                local = instance.pose.GetTransform(boneIndex);
            sampledPose.SetTransform(boneIndex, local);
        }
        return true;
    }

    template <class Source>
    void BlendClipPoses(Animator& animator, const Source& source,
        int clipIndex, int nextClipIndex, bool useDetailReduction = false)
    {
        const auto currentTracks = source.TracksAt(clipIndex);
        const auto nextTracks = source.TracksAt(nextClipIndex);
        auto& instance = animator.GetInstance();
        const std::size_t activeBones = useDetailReduction
            ? (std::min)(source.BoneCount(), static_cast<std::size_t>(instance.activeBoneCount))
            : source.BoneCount();
        for (std::size_t boneIndex = 0; boneIndex < activeBones; ++boneIndex)
        {
            if (!currentTracks[boneIndex] || !nextTracks[boneIndex]) continue;
            g_workerPosePool.current.SetTransform(boneIndex, Animation::Blend(
                g_workerPosePool.current.GetTransform(boneIndex),
                g_workerPosePool.next.GetTransform(boneIndex),
                instance.control.blendT));
        }
    }

    template <class Source>
    void RecoverSnappedBlend(Animator& animator, const Source& source,
        Animation::LocalPose& sampled, const Animation::LocalPose& prior)
    {
        auto& instance = animator.GetInstance();
        if (!instance.blendRecoveryPending && !instance.blendRecoveryActive) return;
        const std::size_t boneCount = source.BoneCount();
        if (instance.blendRecoveryPending)
        {
            instance.blendRecoveryPending = false;
            if (prior.GetCount() != boneCount) return;
            instance.blendRecoveryOffsets.resize(boneCount);
            for (std::size_t bone = 0; bone < boneCount; ++bone)
                instance.blendRecoveryOffsets[bone] = Animation::MakeAdditive(
                    prior.GetTransform(bone), sampled.GetTransform(bone));
            instance.blendRecoveryActive = true;
        }
        constexpr float recoverySeconds = .15f;
        if (instance.blendRecoveryOffsets.size() != boneCount)
        {
            instance.blendRecoveryActive = false;
            return;
        }
        const float t = std::clamp(instance.blendRecoveryElapsed / recoverySeconds, 0.f, 1.f);
        const float weight = 1.f - t * t * (3.f - 2.f * t);
        if (t >= 1.f - 1.e-5f || weight <= 0.f)
        {
            instance.blendRecoveryActive = false;
            return;
        }
        for (std::size_t bone = 0; bone < boneCount; ++bone)
            sampled.SetTransform(bone, Animation::ApplyAdditive(
                sampled.GetTransform(bone), instance.blendRecoveryOffsets[bone], weight));
    }

    template <class Source>
    void MaterializePose(Animator& animator, const Source& source,
        AnimationController* controller, int clipIndex,
        bool updateComposite = true, std::uint32_t changedRoot = kPoseNoParent)
    {
        using Track = typename Source::Track;
        const std::size_t boneCount = source.BoneCount();
        auto& instance = animator.GetInstance();
        const auto trackOf = source.TracksAt(clipIndex);
        auto& sampledPose = g_workerPosePool.current;
        const bool layered = controller && animator.UsesMultipleControllers();
        if (layered)
        {
            // The sample has no further reader. Transfer its SoA storage to
            // the layer consumer and recycle the former layer storage for the
            // next controller, without a per-bone copy.
            std::swap(g_workerPosePool.layer, sampledPose);
            return;
        }
        const math::matrix4x4& rootTransform = source.RootTransform();
        const math::matrix4x4& globalInverse = source.GlobalInverse();
        // Sockets consume the final pose, including single-controller blending.
        const bool writeSockets = updateComposite
            && (!controller || !animator.UsesMultipleControllers())
            && animator.HasSocket()
            && SceneManagers->m_isGameStart && nullptr != animator.GetOwner();

        // 게시 계약(parent < index) 덕에 단일 순회로 충분하다 — D4d와 같은 결.
        auto& globals = instance.poseGlobals;
        globals.resize(boneCount);
        if (changedRoot < boneCount) instance.ikAffectedBones.resize(boneCount);
        for (std::size_t boneIndex = changedRoot < boneCount ? changedRoot : 0;
            boneIndex < boneCount; ++boneIndex)
        {
            const std::uint32_t parent = source.Parent(boneIndex);
            if (changedRoot < boneCount)
            {
                const bool affected = boneIndex == changedRoot
                    || (parent != kPoseNoParent && parent >= changedRoot
                        && instance.ikAffectedBones[parent]);
                instance.ikAffectedBones[boneIndex] = affected;
                if (!affected) continue;
            }
            const math::matrix4x4 parentGlobal = parent != kPoseNoParent
                ? globals[parent] : rootTransform;
            const Track* track = trackOf[boneIndex];
            const bool retainedBindLocal = instance.qualityStage >= animation::quality_stage::l5
                && instance.qualityStage < animation::quality_stage::l7
                && boneIndex >= instance.activeBoneCount
                && instance.bindLocals.size() == boneCount;
            if (nullptr == track && !retainedBindLocal)
            {
                // legacy 재현: 채널 없는 본은 부모 전역을 그대로 잇고 팔레트
                // 슬롯을 건드리지 않는다(이전 값 유지).
                globals[boneIndex] = parentGlobal;
                continue;
            }

            const math::matrix4x4 local = sampledPose.GetTransform(boneIndex).ToMatrix();
            const math::matrix4x4 global = local * parentGlobal;
            globals[boneIndex] = global;

            if (boneIndex < kMaxBones)
            {
                // Layer evaluation writes only its own staging. Composite must
                // retain the previous final local when every mask excludes it.
                if (!controller || !animator.UsesMultipleControllers())
                {
                    instance.localTransforms[boneIndex] = local;
                    instance.finalTransforms[boneIndex] =
                        source.InverseBind(boneIndex) * global * globalInverse;
                }
            }

            if (writeSockets)
            {
                const std::string& boneName = source.BoneName(boneIndex);
                for (auto& socket : animator.socketvec)
                {
                    if (boneName == socket->m_ObjectName)
                    {
                        socket->m_boneMatrix = global * socket->m_offset;
                        socket->m_boneMatrix = socket->m_boneMatrix
                            * animator.GetOwner()->Transform_().GetWorldMatrix();
                    }
                }
            }
        }
    }

    template <class Source>
    void PublishL6Pose(Animator& animator, const Source& source, float alpha)
    {
        auto& instance = animator.GetInstance();
        if (!instance.l6Seeded
            || instance.l6PreviousPose.GetCount() != source.BoneCount()
            || instance.l6LatestPose.GetCount() != source.BoneCount()
            || instance.l6PreviousLocals.size() != instance.localTransforms.size()
            || instance.l6LatestLocals.size() != instance.localTransforms.size()
            || instance.l6PreviousFinals.size() != instance.finalTransforms.size()
            || instance.l6LatestFinals.size() != instance.finalTransforms.size()
            || instance.l6PreviousGlobals.size() != source.BoneCount()
            || instance.l6LatestGlobals.size() != source.BoneCount()) return;
        alpha = std::clamp(alpha, 0.f, 1.f);
        const auto interpolate = [alpha](std::vector<math::matrix4x4>& output,
            const std::vector<math::matrix4x4>& before,
            const std::vector<math::matrix4x4>& after)
        {
            for (std::size_t bone = 0; bone < output.size(); ++bone)
            {
                if (alpha == 1.f) { output[bone] = after[bone]; continue; }
                math::matrix4x4 blended{};
                for (int row = 0; row < 4; ++row)
                    for (int column = 0; column < 4; ++column)
                        blended(row, column) = before[bone](row, column)
                            + (after[bone](row, column) - before[bone](row, column)) * alpha;
                output[bone] = blended;
            }
        };
        interpolate(instance.localTransforms, instance.l6PreviousLocals,
            instance.l6LatestLocals);
        interpolate(instance.finalTransforms, instance.l6PreviousFinals,
            instance.l6LatestFinals);
        interpolate(instance.poseGlobals, instance.l6PreviousGlobals,
            instance.l6LatestGlobals);
        instance.l6PublishedAlpha = alpha;
        if (animator.HasSocket() && SceneManagers->m_isGameStart
            && animator.GetOwner())
        {
            const auto& ownerWorld = animator.GetOwner()->Transform_().GetWorldMatrix();
            for (std::size_t bone = 0; bone < source.BoneCount(); ++bone)
                for (auto& socket : animator.socketvec)
                    if (source.BoneName(bone) == socket->m_ObjectName)
                        socket->m_boneMatrix = instance.poseGlobals[bone]
                            * socket->m_offset * ownerWorld;
        }
    }

    // The deterministic sample probe uses the same operations as the task
    // executor without mutating the live recipe or playback cursors.
    template <class Source>
    void UpdatePose(Animator& animator, const Source& source,
        AnimationController* controller, int clipIndex, int nextClipIndex,
        float time, float nextTime, std::array<Animation::ClipSamplingCursor, 2>& cursors,
        bool updateComposite = true)
    {
        if (nullptr == source.ClipAt(clipIndex)) return;
        SampleClipPose(animator, source, clipIndex, time, cursors, 0);
        if (nullptr != source.ClipAt(nextClipIndex))
        {
            SampleClipPose(animator, source, nextClipIndex, nextTime, cursors, 1,
                clipIndex);
            BlendClipPoses(animator, source, clipIndex, nextClipIndex);
        }
        else cursors[1].Clear();
        MaterializePose(animator, source, controller, clipIndex, updateComposite);
    }

    bool MatchesLayerMask(const AnimatorLayerMaskCache& cache, const AvatarMask* mask,
        std::size_t boneCount)
    {
        if (!cache.m_valid || cache.m_weights.size() != boneCount
            || cache.m_hasMask != (mask != nullptr)) return false;
        if (!mask) return true;
        if (cache.m_isHumanoid != mask->isHumanoid || cache.m_useAll != mask->useAll
            || cache.m_useUpper != mask->useUpper || cache.m_useLower != mask->useLower)
            return false;
        if (mask->isHumanoid) return true;
        if (cache.m_namedBones.size() != mask->m_BoneMasks.size()) return false;
        for (std::size_t i = 0; i < cache.m_namedBones.size(); ++i)
        {
            const BoneMask* boneMask = mask->m_BoneMasks[i];
            const auto& saved = cache.m_namedBones[i];
            if (saved.m_present != (boneMask != nullptr)) return false;
            if (boneMask && (saved.m_name != boneMask->boneName
                || saved.m_enabled != boneMask->isEnabled
                || saved.m_weight != boneMask->weight)) return false;
        }
        return true;
    }

    template <class Source>
    void PrepareLayerMask(AnimatorLayerMaskCache& cache, AvatarMask* mask,
        const Animator& animator, const Source& source)
    {
        const std::size_t boneCount = source.BoneCount();
        if (MatchesLayerMask(cache, mask, boneCount)) return;

        cache.m_valid = true;
        cache.m_hasMask = mask != nullptr;
        cache.m_namedBones.clear();
        cache.m_weights.resize(boneCount);
        if (!mask)
        {
            std::fill(cache.m_weights.begin(), cache.m_weights.end(), 1.f);
            return;
        }

        cache.m_isHumanoid = mask->isHumanoid;
        cache.m_useAll = mask->useAll;
        cache.m_useUpper = mask->useUpper;
        cache.m_useLower = mask->useLower;
        if (!mask->isHumanoid)
        {
            cache.m_namedBones.reserve(mask->m_BoneMasks.size());
            for (const BoneMask* boneMask : mask->m_BoneMasks)
                cache.m_namedBones.push_back(boneMask
                    ? AnimatorLayerMaskCache::NamedBone{ true, boneMask->boneName,
                        boneMask->isEnabled, boneMask->weight }
                    : AnimatorLayerMaskCache::NamedBone{});
        }

        const auto& boneRegions = animator.GetInstance().boneRegions;
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            const BoneRegion region = boneIndex < boneRegions.size()
                ? static_cast<BoneRegion>(boneRegions[boneIndex])
                : BoneRegion::Root;
            if (mask->isHumanoid)
            {
                cache.m_weights[boneIndex] = mask->IsBoneEnabled(region) ? 1.f : 0.f;
                continue;
            }
            // Preserve the legacy first-match and first-null rules while
            // exposing authored weights in [0,1].
            float weight = 0.f;
            for (const BoneMask* boneMask : mask->m_BoneMasks)
            {
                if (!boneMask) break;
                if (boneMask->boneName != source.BoneName(boneIndex)) continue;
                if (boneMask->isEnabled && std::isfinite(boneMask->weight))
                    weight = (std::clamp)(boneMask->weight, 0.f, 1.f);
                break;
            }
            cache.m_weights[boneIndex] = weight;
        }
    }

    template <class Source>
    void PrepareCompositeLayers(Animator& animator, const Source& source)
    {
        const std::size_t boneCount = source.BoneCount();
        auto& instance = animator.GetInstance();
        if (instance.blendRecoveryPending)
            instance.blendRecoverySourcePose = instance.pose;

        // Disabled controllers did not run UpdatePose and must not contribute.
        auto& layerTracks = instance.layerTrackTables;
        layerTracks.resize(animator.m_animationControllers.size());
        auto& layerMasks = instance.layerMaskCaches;
        layerMasks.resize(animator.m_animationControllers.size());
        for (std::size_t slot = 0; slot < layerTracks.size(); ++slot)
        {
            AnimationController* controller = animator.m_animationControllers[slot].get();
            layerTracks[slot] = source.TracksAt(controller && controller->useController
                ? controller->GetAnimationIndex() : -1);
            if (controller && controller->useController && !layerTracks[slot].empty())
                PrepareLayerMask(layerMasks[slot], controller->GetAvatarMask(), animator, source);
        }

        instance.pose.Resize(boneCount);
        instance.layerSelected.assign(boneCount, 0);
    }

    template <class Source>
    void BlendMaskedLayer(Animator& animator, const Source& source, std::size_t slot)
    {
        auto& instance = animator.GetInstance();
        if (slot >= animator.m_animationControllers.size())
            throw std::logic_error("invalid animation layer slot");
        AnimationController* controller = animator.m_animationControllers[slot].get();
        if (!controller || !controller->useController
            || !(controller->IsBlending() || controller->IsUseLayer())) return;
        const auto tracks = instance.layerTrackTables[slot];
        if (tracks.empty()) return;
        const auto& weights = instance.layerMaskCaches[slot].m_weights;
        const auto& layerPose = g_workerPosePool.layer;
        for (std::size_t boneIndex = 0; boneIndex < source.BoneCount() && boneIndex < kMaxBones; ++boneIndex)
        {
            if (boneIndex >= instance.activeBoneCount
                && instance.qualityStage >= animation::quality_stage::l5
                && instance.qualityStage < animation::quality_stage::l7)
            {
                instance.pose.SetTransform(boneIndex, instance.bindLocals[boneIndex]);
                instance.layerSelected[boneIndex] = 1;
                continue;
            }
            if (!tracks[boneIndex]) continue;
            const float weight = weights[boneIndex];
            if (weight <= 0.f) continue;
            // The first contributing layer starts from identity, not the last
            // frame's composite. Unselected bones retain their old pose/local.
            const Animation::LocalTransform base = instance.layerSelected[boneIndex]
                ? instance.pose.GetTransform(boneIndex) : Animation::LocalTransform{};
            instance.pose.SetTransform(boneIndex, Animation::BlendMasked(
                base, layerPose.GetTransform(boneIndex), weight));
            instance.layerSelected[boneIndex] = 1;
        }
    }

    template <class Source>
    void MakeAdditiveLayer(Animator& animator, const Source& source,
        std::size_t slot, int clipIndex, int nextClipIndex)
    {
        auto& instance = animator.GetInstance();
        auto& layerPose = g_workerPosePool.layer;
        const auto currentTracks = source.TracksAt(clipIndex);
        if (currentTracks.empty()) return;
        const auto nextTracks = source.TracksAt(nextClipIndex);
        const auto& weights = instance.layerMaskCaches[slot].m_weights;
        for (std::size_t boneIndex = 0; boneIndex < source.BoneCount() && boneIndex < kMaxBones; ++boneIndex)
        {
            const auto* current = currentTracks[boneIndex];
            if (!current || weights[boneIndex] <= 0.f) continue;
            Animation::TrackKeyCursor cursor{};
            Animation::LocalTransform reference = Source::SampleLocalTransform(*current, 0.0, cursor);
            if (!nextTracks.empty())
            {
                if (const auto* next = nextTracks[boneIndex])
                {
                    cursor = {};
                    reference = Animation::Blend(reference,
                        Source::SampleLocalTransform(*next, 0.0, cursor),
                        instance.control.blendT);
                }
            }
            const Animation::AdditiveDelta delta = Animation::MakeAdditive(
                layerPose.GetTransform(boneIndex), reference);
            // The layer pose is dead after materialization; reuse its storage
            // for the delta until the immediately following apply task.
            layerPose.SetTransform(boneIndex,
                { delta.m_translation, delta.m_rotation, delta.m_scale });
        }
    }

    template <class Source>
    void ApplyAdditiveLayer(Animator& animator, const Source& source, std::size_t slot)
    {
        auto& instance = animator.GetInstance();
        if (slot >= animator.m_animationControllers.size())
            throw std::logic_error("invalid additive animation layer slot");
        AnimationController* controller = animator.m_animationControllers[slot].get();
        if (!controller || !controller->useController || !controller->m_additive
            || !(controller->IsBlending() || controller->IsUseLayer())) return;
        const auto tracks = instance.layerTrackTables[slot];
        if (tracks.empty()) return;
        const auto& weights = instance.layerMaskCaches[slot].m_weights;
        const auto& layerPose = g_workerPosePool.layer;
        for (std::size_t boneIndex = 0; boneIndex < source.BoneCount() && boneIndex < kMaxBones; ++boneIndex)
        {
            if (!tracks[boneIndex]) continue;
            const float weight = weights[boneIndex];
            if (weight <= 0.f) continue;
            const auto encoded = layerPose.GetTransform(boneIndex);
            const Animation::AdditiveDelta delta{
                encoded.m_translation, encoded.m_rotation, encoded.m_scale };
            const Animation::LocalTransform base = instance.layerSelected[boneIndex]
                ? instance.pose.GetTransform(boneIndex) : Animation::LocalTransform{};
            instance.pose.SetTransform(boneIndex,
                Animation::ApplyAdditive(base, delta, weight));
            instance.layerSelected[boneIndex] = 1;
        }
    }

    template <class Source>
    void MaterializeComposite(Animator& animator, const Source& source,
        std::uint32_t changedRoot = kPoseNoParent)
    {
        // Local projection, skinning and sockets consume the same selected pose.
        const std::size_t boneCount = source.BoneCount();
        auto& instance = animator.GetInstance();
        if (changedRoot == kPoseNoParent)
            RecoverSnappedBlend(animator, source, instance.pose,
                instance.blendRecoverySourcePose);

        const math::matrix4x4& rootTransform = source.RootTransform();
        const math::matrix4x4& globalInverse = source.GlobalInverse();
        const bool writeSockets = animator.HasSocket()
            && SceneManagers->m_isGameStart && nullptr != animator.GetOwner();

        auto& globals = instance.poseGlobals;
        globals.resize(boneCount);
        if (changedRoot < boneCount) instance.ikAffectedBones.resize(boneCount);
        for (std::size_t boneIndex = changedRoot < boneCount ? changedRoot : 0;
            boneIndex < boneCount; ++boneIndex)
        {
            const std::uint32_t parent = source.Parent(boneIndex);
            if (changedRoot < boneCount)
            {
                const bool affected = boneIndex == changedRoot
                    || (parent != kPoseNoParent && parent >= changedRoot
                        && instance.ikAffectedBones[parent]);
                instance.ikAffectedBones[boneIndex] = affected;
                if (!affected) continue;
            }
            const math::matrix4x4 parentGlobal = parent != kPoseNoParent
                ? globals[parent] : rootTransform;

            if (boneIndex >= kMaxBones)
            {
                globals[boneIndex] = parentGlobal;
                continue;
            }

            const std::string& boneName = source.BoneName(boneIndex);

            if (instance.layerSelected[boneIndex])
                instance.localTransforms[boneIndex] = instance.pose.GetTransform(boneIndex).ToMatrix();
            const math::matrix4x4 globalTransform = instance.localTransforms[boneIndex] * parentGlobal;
            instance.finalTransforms[boneIndex] =
                source.InverseBind(boneIndex) * globalTransform * globalInverse;

            if (writeSockets)
            {
                for (auto& socket : animator.socketvec)
                {
                    if (boneName == socket->m_ObjectName)
                    {
                        socket->m_boneMatrix = globalTransform * socket->m_offset;
                        socket->m_boneMatrix = socket->m_boneMatrix
                            * animator.GetOwner()->Transform_().GetWorldMatrix();
                    }
                }
            }

            globals[boneIndex] = globalTransform;
        }
    }

    template <class Source>
    void ApplyTwoBoneIK(Animator& animator, const Source& source,
        const animation::task& item)
    {
        auto& instance = animator.GetInstance();
        if (item.output_slot >= instance.ikSnapshots.size()) return;
        const AnimatorIKSnapshot& ik = instance.ikSnapshots[item.output_slot];
        const float weight = ik.weight * (ik.required ? 1.f : instance.optionalIKWeight);
        if (weight <= 0.f) return;
        if (ik.start >= source.BoneCount() || ik.middle >= source.BoneCount()
            || ik.end >= source.BoneCount() || ik.end >= instance.poseGlobals.size()) return;
        if (animator.UsesMultipleControllers())
        {
            if (ik.end >= instance.layerSelected.size()
                || !instance.layerSelected[ik.start]
                || !instance.layerSelected[ik.middle]
                || !instance.layerSelected[ik.end]) return;
        }
        else
        {
            const auto tracks = source.TracksAt(item.clip_index);
            if (tracks.size() <= ik.end || !tracks[ik.start]
                || !tracks[ik.middle] || !tracks[ik.end]) return;
        }

        const auto& globals = instance.poseGlobals;
        const math::vector3 start = globals[ik.start].translation();
        const math::vector3 middle = globals[ik.middle].translation();
        const math::vector3 end = globals[ik.end].translation();
        animation::two_bone_solution solution{};
        if (!animation::solve_two_bone_positions(start, middle, end,
            ik.target, ik.pole, solution)) return;

        const auto& localStart = instance.localTransforms[ik.start];
        const auto& localMiddle = instance.localTransforms[ik.middle];
        const auto startLocal = math::decompose(localStart);
        const auto middleLocal = math::decompose(localMiddle);
        const auto startGlobal = math::decompose(globals[ik.start]);
        if (!startLocal || !middleLocal || !startGlobal) return;
        const std::uint32_t parent = source.Parent(ik.start);
        const math::matrix4x4 parentMatrix = parent == kPoseNoParent
            ? source.RootTransform() : globals[parent];
        const auto parentGlobal = math::decompose(parentMatrix);
        if (!parentGlobal) return;

        const math::quaternion startDelta = animation::rotation_between(
            middle - start, solution.middle - start);
        const math::quaternion solvedStart = math::normalize(
            startGlobal->rotation * startDelta
                * math::inverse(parentGlobal->rotation));
        const math::matrix4x4 newStartMatrix = math::compose(
            startLocal->scale, solvedStart, startLocal->translation) * parentMatrix;
        const math::matrix4x4 newMiddleMatrix = localMiddle * newStartMatrix;
        const math::matrix4x4 newEndMatrix =
            instance.localTransforms[ik.end] * newMiddleMatrix;
        const auto newStartGlobal = math::decompose(newStartMatrix);
        const auto newMiddleGlobal = math::decompose(newMiddleMatrix);
        if (!newStartGlobal || !newMiddleGlobal) return;
        const math::vector3 newMiddle = newMiddleMatrix.translation();
        const math::quaternion middleDelta = animation::rotation_between(
            newEndMatrix.translation() - newMiddle, solution.end - newMiddle);
        const math::quaternion solvedMiddle = math::normalize(
            newMiddleGlobal->rotation * middleDelta
                * math::inverse(newStartGlobal->rotation));

        auto& pose = animator.UsesMultipleControllers()
            ? instance.pose : g_workerPosePool.current;
        Animation::LocalTransform startPose = pose.GetTransform(ik.start);
        Animation::LocalTransform middlePose = pose.GetTransform(ik.middle);
        startPose.m_rotation = math::nlerp(
            math::normalize(startPose.m_rotation), solvedStart, weight);
        middlePose.m_rotation = math::nlerp(
            math::normalize(middlePose.m_rotation), solvedMiddle, weight);
        pose.SetTransform(ik.start, startPose);
        pose.SetTransform(ik.middle, middlePose);

        // Only the changed chain and its descendants need a second FK pass.
        // Other branches retain their already-published palette and sockets.
        if (animator.UsesMultipleControllers())
            MaterializeComposite(animator, source, ik.start);
        else
        {
            AnimationController* controller = item.next_clip_index < 0
                ? nullptr : animator.m_animationControllers[item.next_clip_index].get();
            MaterializePose(animator, source, controller, item.clip_index, true, ik.start);
        }
    }

    template <class Source>
    void ApplyBoneTransform(Animator& animator, const Source& source,
        const animation::task& item)
    {
        auto& instance = animator.GetInstance();
        if (item.output_slot >= instance.boneTransformSnapshots.size()) return;
        const auto& correction = instance.boneTransformSnapshots[item.output_slot];
        const float weight = correction.weight *
            (correction.required ? 1.f : instance.optionalIKWeight);
        if (weight <= 0.f) return;
        if (correction.bone >= source.BoneCount()) return;
        if (animator.UsesMultipleControllers())
        {
            if (correction.bone >= instance.layerSelected.size()
                || !instance.layerSelected[correction.bone]) return;
        }
        else
        {
            const auto tracks = source.TracksAt(item.clip_index);
            if (tracks.size() <= correction.bone || !tracks[correction.bone]) return;
        }
        auto& pose = animator.UsesMultipleControllers()
            ? instance.pose : g_workerPosePool.current;
        const Animation::LocalTransform original = pose.GetTransform(correction.bone);
        Animation::LocalTransform target = original;
        target.m_translation += correction.translationOffset;
        target.m_rotation = math::normalize(original.m_rotation
            * correction.rotationOffset);
        target.m_scale = { original.m_scale.x * correction.scaleMultiplier.x,
            original.m_scale.y * correction.scaleMultiplier.y,
            original.m_scale.z * correction.scaleMultiplier.z };
        pose.SetTransform(correction.bone,
            Animation::Blend(original, target, weight));
        if (animator.UsesMultipleControllers())
            MaterializeComposite(animator, source, correction.bone);
        else
        {
            AnimationController* controller = item.next_clip_index < 0
                ? nullptr : animator.m_animationControllers[item.next_clip_index].get();
            MaterializePose(animator, source, controller, item.clip_index,
                true, correction.bone);
        }
    }

    std::uint32_t AppendClipTasks(animation::task_list& tasks,
        std::uint32_t previous, std::uint32_t controllerSlot,
        int clipIndex, float time, int nextClipIndex, float nextTime)
    {
        auto current = animation::make_task(animation::task_kind::sample_clip);
        current.dependency_a = previous;
        current.output_slot = controllerSlot;
        current.clip_index = clipIndex;
        current.time = time;
        const auto currentIndex = tasks.append(current);
        previous = currentIndex;
        if (nextClipIndex >= 0)
        {
            auto next = animation::make_task(animation::task_kind::sample_clip);
            next.dependency_a = currentIndex;
            next.output_slot = controllerSlot;
            next.clip_index = nextClipIndex;
            next.next_clip_index = clipIndex; // current-channel mask
            next.time = nextTime;
            next.sample_slot = 1;
            const auto nextIndex = tasks.append(next);

            auto blend = animation::make_task(animation::task_kind::blend);
            blend.dependency_a = currentIndex;
            blend.dependency_b = nextIndex;
            blend.clip_index = clipIndex;
            blend.next_clip_index = nextClipIndex;
            previous = tasks.append(blend);
        }
        auto materialize = animation::make_task(animation::task_kind::materialize);
        materialize.dependency_a = previous;
        materialize.output_slot = controllerSlot;
        materialize.clip_index = clipIndex;
        materialize.next_clip_index = nextClipIndex;
        return tasks.append(materialize);
    }

    template <class Source>
    void PreparePose(Animator& animator, const Source& source, float deltaT)
    {
        auto& instance = animator.GetInstance();
        auto& control = instance.control;
        instance.PruneExpiredControllerPlayback();
        auto& tasks = instance.taskList;
        tasks.clear();
        auto& samplingCursors = instance.samplingCursors;
        samplingCursors.resize((std::max)(std::size_t{ 1 }, animator.m_animationControllers.size()));
        if (animator.m_animationControllers.empty() || animator.IsDirectEditorPreview())
        {
            const int clipIndex = static_cast<int>(instance.selectedClipIndex);
            const auto* clip = source.ClipAt(clipIndex);
            if (!clip) return;
            const auto current = animation::AdvanceClip(instance.timeElapsed,
                deltaT * Source::TicksPerSecond(*clip), Source::Duration(*clip),
                animator.IsClipLooping(clipIndex));
            instance.timeElapsed = current.time;
            int nextIndex = -1;
            if (!animator.IsDirectEditorPreview() && control.isBlending)
            {
                if (const auto* next = source.ClipAt(control.nextClipIndex))
                {
                    nextIndex = control.nextClipIndex;
                    instance.nextTimeElapsed = animation::AdvanceClip(
                        instance.nextTimeElapsed, deltaT * Source::TicksPerSecond(*next),
                        Source::Duration(*next), animator.IsClipLooping(nextIndex)).time;
                }
            }
            auto finalIndex = AppendClipTasks(tasks, animation::invalid_task,
                animation::invalid_task, clipIndex, instance.timeElapsed,
                nextIndex, instance.nextTimeElapsed);
            for (std::uint32_t index = 0; index < instance.ikSnapshots.size(); ++index)
            {
                auto ik = animation::make_task(animation::task_kind::two_bone_ik);
                ik.dependency_a = finalIndex;
                ik.output_slot = index;
                ik.clip_index = clipIndex;
                finalIndex = tasks.append(ik);
            }
            for (std::uint32_t index = 0;
                index < instance.boneTransformSnapshots.size(); ++index)
            {
                auto correction = animation::make_task(animation::task_kind::bone_transform);
                correction.dependency_a = finalIndex;
                correction.output_slot = index;
                correction.clip_index = clipIndex;
                finalIndex = tasks.append(correction);
            }
            auto output = animation::make_task(animation::task_kind::output);
            output.dependency_a = finalIndex;
            output.output_slot = animation::worker_current_output;
            tasks.set_output(tasks.append(output));
            return;
        }

        const bool layered = animator.UsesMultipleControllers();
        std::uint32_t previous = animation::invalid_task;
        int lastClipIndex = -1;
        int lastControllerSlot = -1;
        if (layered)
        {
            // Cache masks and reset selection before the first layer writes
            // the single scratch pose. Each layer consumes that pose at once.
            previous = tasks.append(animation::make_task(
                animation::task_kind::prepare_composite));
        }
        for (std::size_t slot = 0; slot < animator.m_animationControllers.size(); ++slot)
        {
            const auto& sharedController = animator.m_animationControllers[slot];
            AnimationController* controller = sharedController.get();
            if (!controller || !controller->useController) continue;
            const int clipIndex = controller->GetAnimationIndex();
            const auto* clip = source.ClipAt(clipIndex);
            if (!clip) continue;
            const AnimationState* state = controller->GetCurrentState();
            const float speed = state ? state->animationSpeed
                * (state->useMultipler ? state->multiplerAnimationSpeed : 1.f) : 1.f;
            const bool looping = animator.IsClipLooping(clipIndex);
            auto& playback = controller->GetPlayback();
            const auto current = animation::AdvanceClip(playback.timeElapsed,
                deltaT * Source::TicksPerSecond(*clip) * speed,
                Source::Duration(*clip), looping);
            playback.timeElapsed = current.time;
            playback.previousCurrentProgress = playback.currentProgress;
            playback.currentProgress = current.progress;
            if (!looping && current.progress >= 1.f) controller->MarkAnimationEnded();

            int nextIndex = -1;
            animation::ClipStep nextStep{};
            if (controller->IsBlending())
            {
                if (const auto* next = source.ClipAt(controller->GetNextAnimationIndex()))
                {
                    nextIndex = controller->GetNextAnimationIndex();
                    nextStep = animation::AdvanceClip(playback.nextTimeElapsed,
                        deltaT * Source::TicksPerSecond(*next), Source::Duration(*next),
                        animator.IsClipLooping(nextIndex));
                    playback.nextTimeElapsed = nextStep.time;
                    playback.previousNextProgress = playback.nextProgress;
                    playback.nextProgress = nextStep.progress;
                }
            }
            // Delivery is queued for the game thread after the animation join.
            // Event traversal depends on the advanced time, not the pose result.
            animator.InvokeClipEvents(clipIndex, current.eventEnd, current.eventBegin);
            if (nextIndex >= 0)
                animator.InvokeClipEvents(nextIndex, nextStep.eventEnd, nextStep.eventBegin);

            // An enabled but non-contributing layer still advances playback
            // and events; its pose has no reader in the composite.
            if (layered && !(controller->IsBlending() || controller->IsUseLayer()))
                continue;
            if (slot >= animation::invalid_task)
                throw std::length_error("animation controller slot overflow");
            previous = AppendClipTasks(tasks, previous,
                static_cast<std::uint32_t>(slot), clipIndex, playback.timeElapsed,
                nextIndex, playback.nextTimeElapsed);
            lastClipIndex = clipIndex;
            lastControllerSlot = static_cast<int>(slot);

            if (!layered) continue;
            if (controller->m_additive)
            {
                auto make = animation::make_task(animation::task_kind::make_additive);
                make.dependency_a = previous;
                make.output_slot = static_cast<std::uint32_t>(slot);
                make.clip_index = clipIndex;
                make.next_clip_index = nextIndex;
                previous = tasks.append(make);
                auto apply = animation::make_task(animation::task_kind::apply_additive);
                apply.dependency_a = previous;
                apply.output_slot = static_cast<std::uint32_t>(slot);
                previous = tasks.append(apply);
            }
            else
            {
                auto masked = animation::make_task(animation::task_kind::blend_masked);
                masked.dependency_a = previous;
                masked.output_slot = static_cast<std::uint32_t>(slot);
                previous = tasks.append(masked);
            }
        }
        if (layered)
        {
            auto materialize = animation::make_task(animation::task_kind::materialize_composite);
            materialize.dependency_a = previous;
            previous = tasks.append(materialize);
        }
        if (previous != animation::invalid_task)
        {
            for (std::uint32_t index = 0; index < instance.ikSnapshots.size(); ++index)
            {
                auto ik = animation::make_task(animation::task_kind::two_bone_ik);
                ik.dependency_a = previous;
                ik.output_slot = index;
                if (!layered)
                {
                    ik.clip_index = lastClipIndex;
                    ik.next_clip_index = lastControllerSlot;
                }
                previous = tasks.append(ik);
            }
            for (std::uint32_t index = 0;
                index < instance.boneTransformSnapshots.size(); ++index)
            {
                auto correction = animation::make_task(animation::task_kind::bone_transform);
                correction.dependency_a = previous;
                correction.output_slot = index;
                if (!layered)
                {
                    correction.clip_index = lastClipIndex;
                    correction.next_clip_index = lastControllerSlot;
                }
                previous = tasks.append(correction);
            }
        }
        auto output = animation::make_task(animation::task_kind::output);
        output.dependency_a = previous;
        if (!layered && previous != animation::invalid_task)
            output.output_slot = animation::worker_current_output;
        tasks.set_output(tasks.append(output));
    }

    template <class Source>
    void ExecutePose(Animator& animator, const Source& source)
    {
        auto& instance = animator.GetInstance();
        instance.executedPoseSamples = 0;
        instance.evaluatedBoneSamples = 0;
        Animation::ResetPoseStorageGrowths();
        instance.taskList.for_each_reachable_indexed([&](std::uint32_t taskIndex,
            const animation::task& item)
        {
            const auto taskBegin = g_timedWork
                ? AnimationMeasurementScope::Clock::now()
                : AnimationMeasurementScope::Clock::time_point{};
            switch (item.kind)
            {
            case animation::task_kind::sample_clip:
            {
                if (item.sample_slot > 1)
                    throw std::logic_error("invalid animation sample slot");
                auto& cursors = instance.samplingCursors[
                    item.output_slot == animation::invalid_task ? 0 : item.output_slot];
                if (SampleClipPose(animator, source, item.clip_index, item.time,
                    cursors, item.sample_slot, item.next_clip_index, true))
                    ++instance.executedPoseSamples;
                break;
            }
            case animation::task_kind::blend:
                BlendClipPoses(animator, source, item.clip_index,
                    item.next_clip_index, true);
                break;
            case animation::task_kind::materialize:
            {
                AnimationController* controller = item.output_slot == animation::invalid_task
                    ? nullptr : animator.m_animationControllers[item.output_slot].get();
                auto& cursors = instance.samplingCursors[
                    item.output_slot == animation::invalid_task ? 0 : item.output_slot];
                if (item.next_clip_index < 0) cursors[1].Clear();
                if (!animator.UsesMultipleControllers())
                    RecoverSnappedBlend(animator, source, g_workerPosePool.current,
                        instance.pose);
                MaterializePose(animator, source, controller, item.clip_index);
                break;
            }
            case animation::task_kind::prepare_composite:
                PrepareCompositeLayers(animator, source);
                break;
            case animation::task_kind::blend_masked:
                BlendMaskedLayer(animator, source, item.output_slot);
                break;
            case animation::task_kind::make_additive:
                MakeAdditiveLayer(animator, source, item.output_slot,
                    item.clip_index, item.next_clip_index);
                break;
            case animation::task_kind::apply_additive:
                ApplyAdditiveLayer(animator, source, item.output_slot);
                break;
            case animation::task_kind::materialize_composite:
                MaterializeComposite(animator, source);
                break;
            case animation::task_kind::two_bone_ik:
                ApplyTwoBoneIK(animator, source, item);
                break;
            case animation::task_kind::bone_transform:
                ApplyBoneTransform(animator, source, item);
                break;
            case animation::task_kind::output:
                // A single-controller recipe leaves its final sample in the
                // worker slot. Publish storage ownership, then recycle the
                // previous instance buffer on this worker without copying TRS.
                // Composite recipes already write the retained output in place.
                if (item.output_slot == animation::worker_current_output)
                    std::swap(instance.pose, g_workerPosePool.current);
                break;
            }
            if (g_timedWork)
            {
                const auto index = static_cast<std::size_t>(item.kind);
                g_timedWork->taskTimesUs[index] += AnimationMeasurementScope::Microseconds(
                    taskBegin, AnimationMeasurementScope::Clock::now());
                ++g_timedWork->taskCounts[index];
            }
            if (instance.captureTaskExecution)
                instance.executedTaskIndices.push_back(taskIndex);
        });
        instance.workerPoseBufferCount = g_workerPosePool.allocated_count();
        instance.poseStorageGrowths = Animation::TakePoseStorageGrowths();
        instance.workerPosePoolIdentity = reinterpret_cast<std::uintptr_t>(&g_workerPosePool);
        instance.workerCurrentStorage = {
            g_workerPosePool.current.GetTranslations().data(),
            g_workerPosePool.current.GetRotations().data(),
            g_workerPosePool.current.GetScales().data() };
    }

    // 결정적 표본 진입점(animtick 게이트 전용) — 살아 있는 컴포넌트를 빌려 쓰므로
    // 팔레트·선택 인덱스를 원복한다. 항등에서 시작한다 — "채널 없는 슬롯
    // 미기록" 규약 아래에서도 결과가 결정적이어야 골든이 성립한다.
    template <class Source>
    bool EvaluatePoseSample(Animator& animator, const Source& source,
        int clipIndex, float time, math::matrix4x4* outPose)
    {
        if (nullptr == outPose) return false;
        if (nullptr == source.ClipAt(clipIndex)) return false;

        auto& instance = animator.GetInstance();
        const auto savedLocal = instance.localTransforms;
        const auto savedFinal = instance.finalTransforms;
        const auto savedPose = instance.pose;
        const uint32_t savedChosen = instance.selectedClipIndex;
        instance.selectedClipIndex = static_cast<uint32_t>(clipIndex);

        instance.ResizePose(source.BoneCount());
        std::fill(instance.finalTransforms.begin(), instance.finalTransforms.end(), math::matrix4x4::identity());
        // Diagnostic samples must not replace the live playback cursors.
        std::array<Animation::ClipSamplingCursor, 2> samplingCursors;
        UpdatePose(animator, source, nullptr, clipIndex, -1, time, 0.f, samplingCursors, false);
        std::fill_n(outPose, kMaxBones, math::matrix4x4::identity());
        std::copy(instance.finalTransforms.begin(), instance.finalTransforms.end(), outPose);

        instance.localTransforms = savedLocal;
        instance.finalTransforms = savedFinal;
        instance.pose = savedPose;
        instance.selectedClipIndex = savedChosen;
        return true;
    }
}

void AnimationScheduler::PrepareGeneration(Animator& animator,
    const assets::ModelAssetGeneration& generation, float deltaT)
{
    const assets::ModelSkeletonAsset* skeleton = generation.Skeleton();
    if (nullptr == skeleton) return;
    const GenerationPoseSource source{ *skeleton, generation.Animations(), generation };
    auto& instance = animator.GetInstance();
    instance.activeBoneCount = static_cast<std::uint32_t>(source.BoneCount());
    PreparePose(animator, source, deltaT);
}

void AnimationScheduler::FinalizeGenerationQuality(Animator& animator,
    const assets::ModelAssetGeneration& generation)
{
    const assets::ModelSkeletonAsset* skeleton = generation.Skeleton();
    if (nullptr == skeleton) return;
    const GenerationPoseSource source{ *skeleton, generation.Animations(), generation };
    auto& instance = animator.GetInstance();
    if (instance.qualityStage >= animation::quality_stage::l1
        && instance.optionalIKWeight <= 0.f)
    {
        instance.taskList.bypass_corrections();
        instance.ikSnapshots.clear();
        instance.boneTransformSnapshots.clear();
    }
    if (instance.qualityStage >= animation::quality_stage::l5
        && instance.qualityStage < animation::quality_stage::l7)
    {
        if (animator.m_LowDetailBoneCount > 0
            && animator.m_LowDetailBoneCount < source.BoneCount()
            && PrepareBindLocals(animator, source))
            instance.activeBoneCount = animator.m_LowDetailBoneCount;
        else
        {
            instance.qualityStage = animation::quality_stage::l4;
            instance.l6Seeded = false;
            instance.l6Evaluate = true;
        }
    }
    if (animator.UsesMultipleControllers())
        instance.taskList.prune_overlay_layers(instance.qualityStage);
    animator.GetInstance().l4SnappedBlend = false;
    if (instance.qualityStage >= animation::quality_stage::l4
        && instance.qualityStage < animation::quality_stage::l7)
    {
        const bool transitioning = instance.control.isBlending
            || std::any_of(animator.m_animationControllers.begin(),
                animator.m_animationControllers.end(),
                [](const std::shared_ptr<AnimationController>& controller)
                { return controller && controller->useController && controller->IsBlending(); });
        if (transitioning)
            instance.l4SnappedBlend = instance.taskList.snap_single_blend(
                instance.control.blendT >= .5f);
    }
    const int previousL6Clip = instance.l6ClipIndex;
    const int previousL6Controller = instance.l6ControllerSlot;
    instance.l6ClipIndex = -1;
    instance.l6ControllerSlot = -1;
    if (instance.qualityStage == animation::quality_stage::l6)
        instance.taskList.for_each_reachable([&](const animation::task& item)
        {
            if (animator.UsesMultipleControllers())
            {
                if (item.kind != animation::task_kind::sample_clip
                    || item.output_slot != 0) return;
            }
            else if (item.kind != animation::task_kind::materialize) return;
            instance.l6ClipIndex = item.clip_index;
            if (item.output_slot != animation::invalid_task)
                instance.l6ControllerSlot = static_cast<int>(item.output_slot);
        });
    if (instance.qualityStage == animation::quality_stage::l6
        && (instance.l6ClipIndex != previousL6Clip
            || instance.l6ControllerSlot != previousL6Controller))
    {
        instance.l6Seeded = false;
        instance.l6Evaluate = true;
        instance.l6Phase = 0;
        instance.l6NextPhase = 1;
    }
}

void AnimationScheduler::ExecuteGeneration(Animator& animator,
    const assets::ModelAssetGeneration& generation)
{
    const assets::ModelSkeletonAsset* skeleton = generation.Skeleton();
    if (nullptr == skeleton) return;
    const GenerationPoseSource source{ *skeleton, generation.Animations(), generation };
    ExecutePose(animator, source);
    auto& instance = animator.GetInstance();
    if (instance.qualityStage != animation::quality_stage::l6) return;
    if (!instance.l6Seeded)
    {
        instance.l6PreviousPose = instance.pose;
        instance.l6LatestPose = instance.pose;
        instance.l6PreviousLocals = instance.localTransforms;
        instance.l6LatestLocals = instance.localTransforms;
        instance.l6PreviousFinals = instance.finalTransforms;
        instance.l6LatestFinals = instance.finalTransforms;
        instance.l6PreviousGlobals = instance.poseGlobals;
        instance.l6LatestGlobals = instance.poseGlobals;
        instance.l6PublishedAlpha = 1.f;
        instance.l6Seeded = true;
        return;
    }
    instance.l6PreviousPose = instance.l6LatestPose;
    instance.l6LatestPose = instance.pose;
    instance.l6PreviousLocals = instance.l6LatestLocals;
    instance.l6LatestLocals = instance.localTransforms;
    instance.l6PreviousFinals = instance.l6LatestFinals;
    instance.l6LatestFinals = instance.finalTransforms;
    instance.l6PreviousGlobals = instance.l6LatestGlobals;
    instance.l6LatestGlobals = instance.poseGlobals;
    PublishL6Pose(animator, source, 1.f / instance.l6Interval);
}

void AnimationScheduler::InterpolateGeneration(Animator& animator,
    const assets::ModelAssetGeneration& generation)
{
    const assets::ModelSkeletonAsset* skeleton = generation.Skeleton();
    if (nullptr == skeleton) return;
    auto& instance = animator.GetInstance();
    PublishL6Pose(animator,
        GenerationPoseSource{ *skeleton, generation.Animations(), generation },
        static_cast<float>(instance.l6Phase + 1) / instance.l6Interval);
}

// I6-B4b — 파리티 하네스를 experiment 단독 평가로 좁혔다. legacy 재귀가
// 죽었으므로 대조할 팔이 없다 — 남은 쓸모는 **결정적 표본으로 제품
// 포즈를 산출**해 주는 것이고, 게이트는 그것을 골든 digest로 잰다(6b).
// MBC8: typed 판이 같은 골든을 내야 한다 — 그것이 typed 샘플러의 정확성 증명이다.
bool AnimationScheduler::EvaluateGenerationPose(Animator& animator,
    const assets::ModelAssetGeneration& generation, int clipIndex,
    float time, math::matrix4x4* outPose)
{
    const assets::ModelSkeletonAsset* skeleton = generation.Skeleton();
    if (nullptr == skeleton) return false;
    return EvaluatePoseSample(animator,
        GenerationPoseSource{ *skeleton, generation.Animations(), generation },
        clipIndex, time, outPose);
}
