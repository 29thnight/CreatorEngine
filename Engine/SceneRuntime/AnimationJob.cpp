#include "AnimationJob.h"
#include "JobScheduler.h"
#include "RenderScene.h"
#include "BoneRegion.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Benchmark.hpp"
#include "AnimationController.h"
#include "AnimationPlayback.h"
#include "Animator.h"
#include "Socket.h"
#include "Assets/ModelAssetGeneration.h"   // PHASE 3.75 MBC8
#include "Assets/ModelAnimationSampler.h"  // PHASE 3.75 MBC8
#include <limits>
#include <span>
#include <atomic> // D34b: 루트 본 부재 1회 경고
#include "ModelConsumptionDiagnostics.h" // MBC10: 틱 경로 관측(읽기 전용 계수)
#include <mathematics/transform.hpp>

AnimationJob::AnimationJob()
{
	m_sceneLoadedHandle = SceneManagers->sceneLoadedEvent.AddRaw(this, &AnimationJob::PrepareAnimation);
    m_AnimationUpdateHandle = SceneManagers->InternalAnimationUpdateEvent.AddRaw(this, &AnimationJob::Update);
	m_sceneUnloadedHandle = SceneManagers->sceneUnloadedEvent.AddRaw(this, &AnimationJob::CleanUp);
}

AnimationJob::~AnimationJob()
{
	SceneManagers->sceneLoadedEvent.Remove(m_sceneLoadedHandle);
	SceneManagers->InternalAnimationUpdateEvent.Remove(m_AnimationUpdateHandle);
    SceneManagers->sceneUnloadedEvent.Remove(m_sceneUnloadedHandle);
}

void AnimationJob::Finalize()
{
	{
		std::lock_guard<std::mutex> lock(m_animatorMutex);
		m_animators.clear();
	}
}

void AnimationJob::RegisterAnimator(Animator* animator)
{
	if (nullptr == animator) return;
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	m_animators[animator->GetInstanceID()] = animator;
}

void AnimationJob::UnregisterAnimator(Animator* animator)
{
	if (nullptr == animator) return;
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	m_animators.erase(animator->GetInstanceID());
}

size_t AnimationJob::GetAnimatorCount() const
{
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	return m_animators.size();
}

std::vector<Animator*> AnimationJob::SnapshotAnimators()
{
	// K2: 프레임-로컬 raw 포인터 스냅샷.
	//
	// 안전 근거 — 잡 실행 창과 컴포넌트 소멸 창은 겹치지 않는다:
	// SceneManager::GameLogic이 InternalAnimationUpdateEvent를 Broadcast하면
	// AnimationJob::Update가 이 스냅샷으로 스레드 풀에 작업을 흘리고
	// 이 프레임의 job_handle.wait()로 완결된다. 실제 소멸(Scene::EndFramePass →
	// FlushPendingDestroy → DestroyComponents의 component.reset())은 같은 게임
	// 스레드의 그 뒤(EditorMain::Update의 DisableOrEnable)에서만 일어나므로,
	// 여기서 담아 스레드 풀 람다에 넘기는 raw 포인터는 그 잡이 완료될 때까지
	// 항상 살아 있다. 그래도 이번 프레임에 파괴 예약된 항목은 미리 걸러 낸다.
	std::vector<Animator*> snapshot;
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	snapshot.reserve(m_animators.size());
	for (const auto& [instanceID, animator] : m_animators)
	{
		if (nullptr == animator || animator->IsDestroyMark()) continue;
		snapshot.push_back(animator);
	}
	return snapshot;
}

void AnimationJob::Update(float deltaTime)
{
	const auto currentAnimators = SnapshotAnimators();
	job_group jobs;

    for(const auto& animator : currentAnimators)
    {
		if (nullptr == animator || !animator->IsEnabled()) continue;
        std::vector<std::weak_ptr<AnimationController>> controllers;
        for (auto& sharedcontroller : animator->m_animationControllers)
        {
            controllers.push_back(sharedcontroller);
        }
        
        // raw Animator* 캡처의 안전 근거는 "이 함수가 자기 작업 그룹의 wait까지 반드시
        // 도달해 잡을 프레임 안에서 완결한다"는 불변식이다 — 여기서 return으로
        // 함수를 빠져나가면 제출한 잡이 대기 없이 프레임 경계를 넘어
        // UAF가 된다(적대 리뷰 발견 1). 그래서 이 애니메이터만 건너뛴다.
        const bool hasExpiredController = std::any_of(controllers.begin(), controllers.end(),
            [](const std::weak_ptr<AnimationController>& controller) { return controller.lock() == nullptr; });
        if (hasExpiredController)
            continue;
        jobs.add([this, animator, controllers, delta = deltaTime] ()
        {
            // I6-B4b — 재생 경로가 하나다. legacy 재귀 폴백(UsesMultipleControllers
            // 분기 · UpdateBone/UpdateBlendBone/UpdateBoneLayer ~200줄)을 걷었다.
            // 그 폴백이 살아 있으면 Animator가 legacy Skeleton을 들고 있어야 하고,
            // 그것이 타입 은퇴를 막는 마지막 런타임 소비였다.
            //
            // ★ 폴백을 지운 값은 "폴백이 돌 상황"이 없어지는 것이 아니다 —
            //   experiment 바인딩이 없으면 이제 **애니메이션이 안 돈다**.
            //   코퍼스에서 그 상황은 0건이고(Assimp 폴백 발화 0 · 14모델 전부
            //   importer가 덮는다), A/B 스위치 off가 그 상태다. 즉 스위치의
            //   애니메이션 차원은 이 슬라이스로 은퇴한다(정점 차원은 남는다).
            // PHASE 3.75 MBC8 — typed 정본이 첫 축이다(스위치 무관). generation
            // shared_ptr을 이 잡 안에서 붙들어 바인딩 교체와 겹쳐도 데이터가 산다.
            const std::shared_ptr<const assets::ModelAssetGeneration> generation =
                animator->m_modelGeneration;
            const assets::ModelSkeletonAsset* typedSkeleton =
                generation ? generation->Skeleton() : nullptr;
            if (!animator->m_tickPathLogged)
            {
                animator->m_tickPathLogged = true;
                ModelConsumptionDiagnostics::NoteTickPath(nullptr != typedSkeleton);
            }
            if (nullptr == typedSkeleton) return;

            float deltaT = delta;
            if (animator->m_stopTimer > 0.f) {
                animator->m_stopDuration += delta;
                animator->m_stopTimer -= delta;
                deltaT = 0.f;
                if (animator->m_stopTimer <= 0.f) {
                    deltaT = animator->m_stopDuration;
                    animator->m_stopDuration = 0.f;
                }
            }

            TickGeneration(*animator, *generation, deltaT);
        });
    }

    ce::get_job_scheduler().submit(std::move(jobs)).wait();

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
			scene->PublishAnimatorPose(*animator);
		}

		if (!animator->HasSocket() || !SceneManagers->m_isGameStart) continue;
		for (Socket* socket : animator->socketvec)
		{
			if (!socket) continue;
			socket->transform.SetLocalMatrix(
				socket->m_boneMatrix, TransformWriteReason::Animator);
			socket->Update();
		}
	}
}

void AnimationJob::PrepareAnimation()
{
	// 로드 이벤트는 만료 weak 참조를 정리하는 경계로만 쓴다. 렌더 씬의
	// registry를 당겨 오지 않는다 — animator는 게임/animation 소유다.
	(void)SnapshotAnimators();
}

void AnimationJob::CleanUp()
{
	std::lock_guard<std::mutex> lock(m_animatorMutex);
	m_animators.clear();
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
        static void BuildTrackTable(const Clip& clip, std::size_t boneCount,
            std::vector<const Track*>& outTable)
        { assets::animation::BuildTrackTable(clip, boneCount, outTable); }
        static math::matrix4x4 SampleLocal(const Track& track, double time)
        { return assets::animation::SampleLocal(track, time); }
    };

    math::matrix4x4 BlendPose(const math::matrix4x4& curAni,
        const math::matrix4x4& nextAni, float t)
    {
        const auto current = math::decompose(curAni);
        const auto next = math::decompose(nextAni);
        // 애니메이션 키에서 만든 TRS는 항상 분해 가능하다. 손상된 입력이면
        // 초기화되지 않은 분해 결과를 쓰지 않고 현재 포즈를 유지한다.
        if (!current || !next) return curAni;

        return math::compose(
            math::lerp(current->scale, next->scale, t),
            math::slerp(current->rotation, next->rotation, t),
            math::lerp(current->translation, next->translation, t));
    }

    template <class Source>
    void UpdatePose(Animator& animator, const Source& source,
        AnimationController* controller, int clipIndex, int nextClipIndex,
        float time, float nextTime)
    {
        using Track = typename Source::Track;
        const auto* clip = source.ClipAt(clipIndex);
        if (nullptr == clip) return;
        const auto* nextClip = source.ClipAt(nextClipIndex);

        const std::size_t boneCount = source.BoneCount();
        std::vector<const Track*> trackOf;
        Source::BuildTrackTable(*clip, boneCount, trackOf);
        std::vector<const Track*> nextTrackOf;
        if (nextClip) Source::BuildTrackTable(*nextClip, boneCount, nextTrackOf);

        const math::matrix4x4& rootTransform = source.RootTransform();
        const math::matrix4x4& globalInverse = source.GlobalInverse();
        // Sockets consume the final pose, including single-controller blending.
        const bool writeSockets = (!controller || !animator.UsesMultipleControllers())
            && animator.HasSocket()
            && SceneManagers->m_isGameStart && nullptr != animator.GetOwner();

        // 게시 계약(parent < index) 덕에 단일 순회로 충분하다 — D4d와 같은 결.
        std::vector<math::matrix4x4> globals(boneCount, rootTransform);
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            const std::uint32_t parent = source.Parent(boneIndex);
            const math::matrix4x4 parentGlobal = parent != kPoseNoParent
                ? globals[parent] : rootTransform;
            const Track* track = trackOf[boneIndex];
            if (nullptr == track)
            {
                // legacy 재현: 채널 없는 본은 부모 전역을 그대로 잇고 팔레트
                // 슬롯을 건드리지 않는다(이전 값 유지).
                globals[boneIndex] = parentGlobal;
                continue;
            }

            math::matrix4x4 local = Source::SampleLocal(*track, time);
            if (nextClip)
            {
                // legacy UpdateBlendBone은 다음 클립 맵에 operator[]로 접근해
                // 채널이 없으면 빈 항목을 만들고 빈 키 배열을 읽었다(잠재 UB).
                // 여기서는 다음 채널이 없으면 블렌드를 생략한다 — 실코퍼스는
                // 클립 간 채널 집합이 같아 행동 차이가 없다.
                if (const Track* nextTrack = nextTrackOf[boneIndex])
                {
                    local = BlendPose(local,
                        Source::SampleLocal(*nextTrack, nextTime), animator.blendT);
                }
            }
            const math::matrix4x4 global = local * parentGlobal;
            globals[boneIndex] = global;

            if (boneIndex < kMaxBones)
            {
                // Layer evaluation writes only its own staging. Composite must
                // retain the previous final local when every mask excludes it.
                if (!controller || !animator.UsesMultipleControllers())
                {
                    animator.m_localTransforms[boneIndex] = local;
                    animator.m_FinalTransforms[boneIndex] =
                        source.InverseBind(boneIndex) * global * globalInverse;
                }
                if (controller)
                {
                    controller->m_LocalTransforms[boneIndex] = local;
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
    void UpdateLayer(Animator& animator, const Source& source)
    {
        using Track = typename Source::Track;
        // Compose evaluated channels only. Local projection, skinning and sockets
        // all consume the same selected pose, including humanoid masks.
        const std::size_t boneCount = source.BoneCount();

        // Disabled controllers did not run UpdatePose and must not contribute.
        std::vector<std::vector<std::uint8_t>> controllerHasChannel(
            animator.m_animationControllers.size());
        for (std::size_t slot = 0; slot < controllerHasChannel.size(); ++slot)
        {
            controllerHasChannel[slot].assign(boneCount, 0);
            AnimationController* controller = animator.m_animationControllers[slot].get();
            if (!controller || !controller->useController) continue;
            const auto* clip = source.ClipAt(controller ? controller->GetAnimationIndex() : -1);
            if (nullptr == clip) continue;
            std::vector<const Track*> trackOf;
            Source::BuildTrackTable(*clip, boneCount, trackOf);
            for (std::size_t bone = 0; bone < boneCount; ++bone)
                controllerHasChannel[slot][bone] = trackOf[bone] ? 1 : 0;
        }

        const math::matrix4x4& rootTransform = source.RootTransform();
        const math::matrix4x4& globalInverse = source.GlobalInverse();
        const bool writeSockets = animator.HasSocket()
            && SceneManagers->m_isGameStart && nullptr != animator.GetOwner();

        std::vector<math::matrix4x4> globals(boneCount, rootTransform);
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            const std::uint32_t parent = source.Parent(boneIndex);
            const math::matrix4x4 parentGlobal = parent != kPoseNoParent
                ? globals[parent] : rootTransform;

            if (boneIndex >= kMaxBones)
            {
                globals[boneIndex] = parentGlobal;
                continue;
            }

            const BoneRegion region = boneIndex < animator.m_boneRegions.size()
                ? static_cast<BoneRegion>(animator.m_boneRegions[boneIndex])
                : BoneRegion::Root;
            const std::string& boneName = source.BoneName(boneIndex);

            animation::LayerLocalPose selected(animator.m_localTransforms[boneIndex]);
            for (std::size_t slot = 0; slot < animator.m_animationControllers.size(); ++slot)
            {
                AnimationController* controller = animator.m_animationControllers[slot].get();
                if (nullptr == controller) continue;
                AvatarMask* mask = controller->GetAvatarMask();
                const bool maskAllows = !mask || (mask->isHumanoid
                    ? mask->IsBoneEnabled(region) : mask->IsBoneEnabled(boneName));
                selected.Apply(controller->m_LocalTransforms[boneIndex],
                    controller->useController && (controller->m_isBlend || controller->IsUseLayer()),
                    controllerHasChannel[slot][boneIndex] != 0, maskAllows);
            }

            animator.m_localTransforms[boneIndex] = selected.Local();
            const math::matrix4x4 globalTransform = selected.Local() * parentGlobal;
            animator.m_FinalTransforms[boneIndex] =
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
    void TickPose(Animator& animator, const Source& source, float deltaT)
    {
        if (animator.m_animationControllers.empty())
        {
            const int clipIndex = static_cast<int>(animator.m_AnimIndexChosen);
            const auto* clip = source.ClipAt(clipIndex);
            if (!clip) return;
            const auto current = animation::AdvanceClip(animator.m_TimeElapsed,
                deltaT * Source::TicksPerSecond(*clip), Source::Duration(*clip),
                animator.IsClipLooping(clipIndex));
            animator.m_TimeElapsed = current.time;
            int nextIndex = -1;
            if (animator.m_isBlend)
            {
                if (const auto* next = source.ClipAt(animator.nextAnimIndex))
                {
                    nextIndex = animator.nextAnimIndex;
                    animator.m_nextTimeElapsed = animation::AdvanceClip(
                        animator.m_nextTimeElapsed, deltaT * Source::TicksPerSecond(*next),
                        Source::Duration(*next), animator.IsClipLooping(nextIndex)).time;
                }
            }
            UpdatePose(animator, source, nullptr, clipIndex, nextIndex,
                animator.m_TimeElapsed, animator.m_nextTimeElapsed);
            return;
        }

        for (const auto& sharedController : animator.m_animationControllers)
        {
            AnimationController* controller = sharedController.get();
            if (!controller || !controller->useController) continue;
            const int clipIndex = controller->GetAnimationIndex();
            const auto* clip = source.ClipAt(clipIndex);
            if (!clip) continue;
            const AnimationState* state = controller->m_curState;
            const float speed = state ? state->animationSpeed
                * (state->useMultipler ? state->multiplerAnimationSpeed : 1.f) : 1.f;
            const bool looping = animator.IsClipLooping(clipIndex);
            const auto current = animation::AdvanceClip(controller->m_timeElapsed,
                deltaT * Source::TicksPerSecond(*clip) * speed,
                Source::Duration(*clip), looping);
            controller->m_timeElapsed = current.time;
            controller->preCurAnimationProgress = controller->curAnimationProgress;
            controller->curAnimationProgress = current.progress;
            if (!looping && current.progress >= 1.f) controller->endAnimation = true;

            int nextIndex = -1;
            animation::ClipStep nextStep{};
            if (controller->m_isBlend)
            {
                if (const auto* next = source.ClipAt(controller->GetNextAnimationIndex()))
                {
                    nextIndex = controller->GetNextAnimationIndex();
                    nextStep = animation::AdvanceClip(controller->m_nextTimeElapsed,
                        deltaT * Source::TicksPerSecond(*next), Source::Duration(*next),
                        animator.IsClipLooping(nextIndex));
                    controller->m_nextTimeElapsed = nextStep.time;
                    controller->preNextAnimationProgress = controller->nextAnimationProgress;
                    controller->nextAnimationProgress = nextStep.progress;
                }
            }
            UpdatePose(animator, source, controller, clipIndex, nextIndex,
                controller->m_timeElapsed, controller->m_nextTimeElapsed);

            animator.InvokeClipEvents(clipIndex, current.eventEnd, current.eventBegin);
            if (nextIndex >= 0)
                animator.InvokeClipEvents(nextIndex, nextStep.eventEnd, nextStep.eventBegin);
        }
        if (animator.UsesMultipleControllers()) UpdateLayer(animator, source);
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

        std::vector<math::matrix4x4> savedLocal(
            animator.m_localTransforms, animator.m_localTransforms + kMaxBones);
        std::vector<math::matrix4x4> savedFinal(
            animator.m_FinalTransforms, animator.m_FinalTransforms + kMaxBones);
        const uint32_t savedChosen = animator.m_AnimIndexChosen;
        animator.m_AnimIndexChosen = static_cast<uint32_t>(clipIndex);

        std::fill(animator.m_FinalTransforms,
            animator.m_FinalTransforms + kMaxBones, math::matrix4x4::identity());
        UpdatePose(animator, source, nullptr, clipIndex, -1, time, 0.f);
        std::copy(animator.m_FinalTransforms,
            animator.m_FinalTransforms + kMaxBones, outPose);

        std::copy(savedLocal.begin(), savedLocal.end(), animator.m_localTransforms);
        std::copy(savedFinal.begin(), savedFinal.end(), animator.m_FinalTransforms);
        animator.m_AnimIndexChosen = savedChosen;
        return true;
    }
}

void AnimationJob::TickGeneration(Animator& animator,
    const assets::ModelAssetGeneration& generation, float deltaT)
{
    const assets::ModelSkeletonAsset* skeleton = generation.Skeleton();
    if (nullptr == skeleton) return;
    TickPose(animator, GenerationPoseSource{ *skeleton, generation.Animations() }, deltaT);
}

// I6-B4b — 파리티 하네스를 experiment 단독 평가로 좁혔다. legacy 재귀가
// 죽었으므로 대조할 팔이 없다 — 남은 쓸모는 **결정적 표본으로 제품
// 포즈를 산출**해 주는 것이고, 게이트는 그것을 골든 digest로 잰다(6b).
// MBC8: typed 판이 같은 골든을 내야 한다 — 그것이 typed 샘플러의 정확성 증명이다.
bool AnimationJob::EvaluateGenerationPose(Animator& animator,
    const assets::ModelAssetGeneration& generation, int clipIndex,
    float time, math::matrix4x4* outPose)
{
    const assets::ModelSkeletonAsset* skeleton = generation.Skeleton();
    if (nullptr == skeleton) return false;
    return EvaluatePoseSample(animator,
        GenerationPoseSource{ *skeleton, generation.Animations() },
        clipIndex, time, outPose);
}
