#include "AnimationJob.h"
#include "RenderScene.h"
#include "Skeleton.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Benchmark.hpp"
#include "AnimationController.h"
#include "Animator.h"
#include "Socket.h"
#include "Experiment/Model.h"       // I5-D4e-1
#include "Experiment/PoseSampler.h" // I5-D4e-1
#include <atomic> // D34b: 루트 본 부재 1회 경고
#include <cstdio> // I5-D4e-1: [anim.tick] 경로 관측
#include <mathematics/transform.hpp>

inline float lerp(float a, float b, float f)
{
    return a + f * (b - a);
}

template <typename T>
int CurrentKeyIndex(std::vector<T>& keys, double time)
{
    float duration = time;
    for (UINT i = 0; i < keys.size() - 1; ++i)
    {
        if (duration <= keys[i + 1].m_time)
        {
            return i;
        }
    }
    return -1;
}

AnimationJob::AnimationJob()
{
	m_UpdateThreadPool = new ThreadPool<std::function<void()>>(8); // 8 threads for animation updates
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
	delete m_UpdateThreadPool;
	m_UpdateThreadPool = nullptr;
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
	// NotifyAllAndWait로 그 프레임 안에서 완결된다. 실제 소멸(Scene::OnDestroy →
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

    for(const auto& animator : currentAnimators)
    {
		if (nullptr == animator || !animator->IsEnabled()) continue;
        std::vector<std::weak_ptr<AnimationController>> controllers;
        for (auto& sharedcontroller : animator->m_animationControllers)
        {
            controllers.push_back(sharedcontroller);
        }
        
        // raw Animator* 캡처의 안전 근거는 "이 함수가 NotifyAllAndWait까지 반드시
        // 도달해 잡을 프레임 안에서 완결한다"는 불변식이다 — 여기서 return으로
        // 함수를 빠져나가면 이미 Enqueue된 잡이 대기 없이 프레임 경계를 넘어
        // UAF가 된다(적대 리뷰 발견 1). 그래서 이 애니메이터만 건너뛴다.
        const bool hasExpiredController = std::any_of(controllers.begin(), controllers.end(),
            [](const std::weak_ptr<AnimationController>& controller) { return controller.lock() == nullptr; });
        if (hasExpiredController)
            continue;
        m_UpdateThreadPool->Enqueue([this, animator, controllers, delta = deltaTime] ()
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
            const experiment::Skeleton* experimentSkeleton =
                animator->m_experimentModel
                ? animator->m_experimentModel->TryGetSkeleton() : nullptr;
            if (!animator->m_tickPathLogged)
            {
                animator->m_tickPathLogged = true;
                std::printf("[anim.tick] %s\n",
                    experimentSkeleton ? "experiment" : "none");
            }
            if (nullptr == experimentSkeleton) return;

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

            TickExperiment(*animator, *experimentSkeleton, deltaT);
        });
    }

    m_UpdateThreadPool->NotifyAllAndWait();

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

math::matrix4x4 AnimationJob::BlendAni(const math::matrix4x4& curAni,
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

// I6-B4b — legacy 재귀 틱(UpdateBlendBone/UpdateBone/UpdateBoneLayer/
// calculAni)을 걷었다. 재생 경로는 TickExperiment 하나다.
//
// ★ 이 함수들이 Animator의 legacy Skeleton 소비 중 유일한 **런타임**
//   소비였다. 나머지는 진단·대조군이라, 이것이 죽어야 타입 은퇴가
//   실제 코드 삭제로 이어진다.

void AnimationJob::TickExperiment(Animator& animator,
    const experiment::Skeleton& skeleton, float deltaT)
{
    const std::vector<experiment::AnimationClip>& clips = skeleton.clips;

    const auto clipAt = [&clips](int index) -> const experiment::AnimationClip*
    {
        return index >= 0 && static_cast<std::size_t>(index) < clips.size()
            ? &clips[static_cast<std::size_t>(index)] : nullptr;
    };

    if (animator.UsesMultipleControllers())
    {
        for (auto& sharedController : animator.m_animationControllers)
        {
            AnimationController* controller = sharedController.get();
            if (nullptr == controller || !controller->useController) continue;

            float animationSpeed = 1;
            AnimationState* curState = controller->m_curState;
            if (curState)
            {
                animationSpeed = curState->animationSpeed;
                if (curState->useMultipler)
                {
                    animationSpeed *= curState->multiplerAnimationSpeed;
                }
            }

            const experiment::AnimationClip* clip =
                clipAt(controller->GetAnimationIndex());
            if (nullptr == clip) continue;
            const float duration = static_cast<float>(clip->durationTicks);
            controller->m_timeElapsed += deltaT
                * static_cast<float>(clip->ticksPerSecond) * animationSpeed;
            if (animator.IsClipLooping(controller->GetAnimationIndex()))
            {
                controller->m_timeElapsed =
                    fmod(controller->m_timeElapsed, duration);
            }
            else if (controller->m_timeElapsed >= duration)
            {
                controller->m_timeElapsed = duration;
                if (controller->curAnimationProgress >= 0.95)
                    controller->endAnimation = true;
            }
            controller->preCurAnimationProgress = controller->curAnimationProgress;
            controller->curAnimationProgress =
                controller->m_timeElapsed / duration;

            if (controller->m_isBlend)
            {
                const experiment::AnimationClip* nextClip =
                    clipAt(controller->GetNextAnimationIndex());
                if (nextClip)
                {
                    const float nextDuration =
                        static_cast<float>(nextClip->durationTicks);
                    controller->m_nextTimeElapsed += deltaT
                        * static_cast<float>(nextClip->ticksPerSecond);
                    controller->m_nextTimeElapsed =
                        fmod(controller->m_nextTimeElapsed, nextDuration);
                    controller->preNextAnimationProgress =
                        controller->nextAnimationProgress;
                    controller->nextAnimationProgress =
                        controller->m_nextTimeElapsed / nextDuration;
                    UpdateExperimentPose(animator, skeleton, controller,
                        controller->GetAnimationIndex(),
                        controller->GetNextAnimationIndex(),
                        controller->m_timeElapsed,
                        controller->m_nextTimeElapsed);
                }
            }
            else
            {
                UpdateExperimentPose(animator, skeleton, controller,
                    controller->GetAnimationIndex(), -1,
                    controller->m_timeElapsed, 0.f);
            }

            if (deltaT <= 0.f) continue;
            animator.InvokeClipEvents(controller->GetAnimationIndex(),
                controller->curAnimationProgress,
                controller->preCurAnimationProgress);
            if (controller->m_isBlend)
            {
                animator.InvokeClipEvents(controller->GetNextAnimationIndex(),
                    controller->nextAnimationProgress,
                    controller->preNextAnimationProgress);
            }
        }

        UpdateExperimentLayer(animator, skeleton);
    }
    else if (animator.m_animationControllers.empty())
    {
        const experiment::AnimationClip* clip =
            clipAt(static_cast<int>(animator.m_AnimIndexChosen));
        if (nullptr == clip) return;
        const float duration = static_cast<float>(clip->durationTicks);
        animator.m_TimeElapsed += deltaT
            * static_cast<float>(clip->ticksPerSecond);
        if (animator.IsClipLooping(static_cast<int>(animator.m_AnimIndexChosen)))
        {
            animator.m_TimeElapsed = fmod(animator.m_TimeElapsed, duration);
        }
        else if (animator.m_TimeElapsed >= duration)
        {
            animator.m_TimeElapsed = duration;
        }

        if (animator.m_isBlend)
        {
            if (animator.nextAnimIndex == -1) return;
            const experiment::AnimationClip* nextClip =
                clipAt(animator.nextAnimIndex);
            if (nullptr == nextClip) return;
            const float nextDuration =
                static_cast<float>(nextClip->durationTicks);
            animator.m_nextTimeElapsed += deltaT
                * static_cast<float>(nextClip->ticksPerSecond);
            animator.m_nextTimeElapsed =
                fmod(animator.m_nextTimeElapsed, nextDuration);
            UpdateExperimentPose(animator, skeleton, nullptr,
                static_cast<int>(animator.m_AnimIndexChosen),
                animator.nextAnimIndex,
                animator.m_TimeElapsed, animator.m_nextTimeElapsed);
        }
        else
        {
            UpdateExperimentPose(animator, skeleton, nullptr,
                static_cast<int>(animator.m_AnimIndexChosen), -1,
                animator.m_TimeElapsed, 0.f);
        }
    }
    else // 컨트롤러 1개
    {
        AnimationController* controller = animator.m_animationControllers[0].get();
        const experiment::AnimationClip* clip =
            clipAt(controller->GetAnimationIndex());
        if (nullptr == clip) return;
        const float duration = static_cast<float>(clip->durationTicks);
        AnimationState* curState = controller->m_curState;
        float animationSpeed = 1;
        if (curState)
        {
            animationSpeed = curState->animationSpeed;
            if (curState->useMultipler)
            {
                animationSpeed *= curState->multiplerAnimationSpeed;
            }
        }
        controller->m_timeElapsed += deltaT
            * static_cast<float>(clip->ticksPerSecond) * animationSpeed;
        if (animator.IsClipLooping(controller->GetAnimationIndex()))
        {
            controller->m_timeElapsed = fmod(controller->m_timeElapsed, duration);
        }
        else if (controller->m_timeElapsed >= duration)
        {
            controller->m_timeElapsed = duration;
            if (controller->curAnimationProgress >= 0.95)
                controller->endAnimation = true;
        }

        controller->preCurAnimationProgress = controller->curAnimationProgress;
        controller->curAnimationProgress = controller->m_timeElapsed / duration;

        if (animator.m_isBlend)
        {
            if (animator.nextAnimIndex == -1) return;
            const experiment::AnimationClip* nextClip =
                clipAt(controller->GetNextAnimationIndex());
            if (nullptr == nextClip) return;
            const float nextDuration =
                static_cast<float>(nextClip->durationTicks);
            controller->m_nextTimeElapsed += deltaT
                * static_cast<float>(nextClip->ticksPerSecond);
            controller->m_nextTimeElapsed =
                fmod(controller->m_nextTimeElapsed, nextDuration);
            controller->preNextAnimationProgress =
                controller->nextAnimationProgress;
            controller->nextAnimationProgress =
                controller->m_nextTimeElapsed / nextDuration;
            UpdateExperimentPose(animator, skeleton, controller,
                controller->GetAnimationIndex(),
                controller->GetNextAnimationIndex(),
                controller->m_timeElapsed, controller->m_nextTimeElapsed);
        }
        else
        {
            UpdateExperimentPose(animator, skeleton, controller,
                controller->GetAnimationIndex(), -1,
                controller->m_timeElapsed, 0.f);
        }

        if (deltaT > 0.f)
        {
            animator.InvokeClipEvents(controller->GetAnimationIndex(),
                controller->curAnimationProgress,
                controller->preCurAnimationProgress);
            if (controller->m_isBlend)
            {
                animator.InvokeClipEvents(controller->GetNextAnimationIndex(),
                    controller->nextAnimationProgress,
                    controller->preNextAnimationProgress);
            }
        }
    }
}

void AnimationJob::UpdateExperimentPose(Animator& animator,
    const experiment::Skeleton& skeleton, AnimationController* controller,
    int clipIndex, int nextClipIndex, float time, float nextTime)
{
    namespace sampler = experiment::sampler;
    const std::vector<experiment::AnimationClip>& clips = skeleton.clips;
    if (clipIndex < 0 || static_cast<std::size_t>(clipIndex) >= clips.size())
        return;
    const experiment::AnimationClip& clip =
        clips[static_cast<std::size_t>(clipIndex)];
    const experiment::AnimationClip* nextClip =
        (nextClipIndex >= 0
            && static_cast<std::size_t>(nextClipIndex) < clips.size())
        ? &clips[static_cast<std::size_t>(nextClipIndex)] : nullptr;

    const std::size_t boneCount = skeleton.bones.size();
    std::vector<const experiment::AnimationChannel*> channelOf(boneCount, nullptr);
    for (const experiment::AnimationChannel& channel : clip.channels)
    {
        if (experiment::IsInRange(channel.bone, boneCount))
            channelOf[channel.bone.Value()] = &channel;
    }
    std::vector<const experiment::AnimationChannel*> nextChannelOf;
    if (nextClip)
    {
        nextChannelOf.assign(boneCount, nullptr);
        for (const experiment::AnimationChannel& channel : nextClip->channels)
        {
            if (experiment::IsInRange(channel.bone, boneCount))
                nextChannelOf[channel.bone.Value()] = &channel;
        }
    }

    const math::matrix4x4& rootTransform = skeleton.rootTransform;
    const math::matrix4x4& globalInverse = skeleton.globalInverseTransform;
    // legacy 재현: 소켓 행렬은 비블렌드 순회(UpdateBone)에서만 계산됐다.
    const bool writeSockets = nullptr == nextClip && animator.HasSocket()
        && SceneManagers->m_isGameStart && nullptr != animator.GetOwner();

    // 게시 계약(parent < index) 덕에 단일 순회로 충분하다 — D4d와 같은 결.
    std::vector<math::matrix4x4> globals(boneCount, rootTransform);
    for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
    {
        const experiment::Bone& bone = skeleton.bones[boneIndex];
        const math::matrix4x4 parentGlobal = bone.parent.IsValid()
            ? globals[bone.parent.Value()] : rootTransform;
        const experiment::AnimationChannel* channel = channelOf[boneIndex];
        if (nullptr == channel)
        {
            // legacy 재현: 채널 없는 본은 부모 전역을 그대로 잇고 팔레트
            // 슬롯을 건드리지 않는다(이전 값 유지).
            globals[boneIndex] = parentGlobal;
            continue;
        }

        math::matrix4x4 local = sampler::SampleLocal(*channel, time);
        if (nextClip)
        {
            // legacy UpdateBlendBone은 다음 클립 맵에 operator[]로 접근해
            // 채널이 없으면 빈 항목을 만들고 빈 키 배열을 읽었다(잠재 UB).
            // 여기서는 다음 채널이 없으면 블렌드를 생략한다 — 실코퍼스는
            // 클립 간 채널 집합이 같아 행동 차이가 없다.
            if (const experiment::AnimationChannel* nextChannel =
                nextChannelOf[boneIndex])
            {
                local = BlendAni(local,
                    sampler::SampleLocal(*nextChannel, nextTime),
                    animator.blendT);
            }
        }
        const math::matrix4x4 global = local * parentGlobal;
        globals[boneIndex] = global;

        if (boneIndex < MAX_BONES)
        {
            animator.m_localTransforms[boneIndex] = local;
            animator.m_FinalTransforms[boneIndex] =
                bone.inverseBindMatrix * global * globalInverse;
            if (controller)
            {
                controller->m_LocalTransforms[boneIndex] = local;
            }
        }

        if (writeSockets)
        {
            for (auto& socket : animator.socketvec)
            {
                if (bone.name == socket->m_ObjectName)
                {
                    socket->m_boneMatrix = global * socket->m_offset;
                    socket->m_boneMatrix = socket->m_boneMatrix
                        * animator.GetOwner()->Transform_().GetWorldMatrix();
                }
            }
        }
    }
}

void AnimationJob::UpdateExperimentLayer(Animator& animator,
    const experiment::Skeleton& skeleton)
{
    // legacy UpdateBoneLayer 재현(단일 순회). 컨트롤러들의 m_LocalTransforms
    // (앞선 포즈 패스가 채움)를 마스크로 골라 합성한다. legacy의 알려진 결함도
    // 그대로 승계한다: 채널이 있는 본에서 모든 컨트롤러가 마스크에 걸리면
    // globalTransform이 기본 초기화(영행렬 — math 규약)로 남아 팔레트에 나간다.
    const std::vector<experiment::AnimationClip>& clips = skeleton.clips;
    const std::size_t boneCount = skeleton.bones.size();

    // 컨트롤러별 "이 본에 채널이 있는가" 표 — legacy hasAnyAnimation 재현
    // (useController 필터가 없는 것까지 동일).
    std::vector<const experiment::AnimationClip*> controllerClips;
    controllerClips.reserve(animator.m_animationControllers.size());
    for (auto& sharedController : animator.m_animationControllers)
    {
        AnimationController* controller = sharedController.get();
        const int index = controller ? controller->GetAnimationIndex() : -1;
        controllerClips.push_back(
            index >= 0 && static_cast<std::size_t>(index) < clips.size()
            ? &clips[static_cast<std::size_t>(index)] : nullptr);
    }
    std::vector<std::vector<std::uint8_t>> controllerHasChannel(
        controllerClips.size());
    for (std::size_t slot = 0; slot < controllerClips.size(); ++slot)
    {
        controllerHasChannel[slot].assign(boneCount, 0);
        if (nullptr == controllerClips[slot]) continue;
        for (const experiment::AnimationChannel& channel
            : controllerClips[slot]->channels)
        {
            if (experiment::IsInRange(channel.bone, boneCount))
                controllerHasChannel[slot][channel.bone.Value()] = 1;
        }
    }

    const math::matrix4x4& rootTransform = skeleton.rootTransform;
    const math::matrix4x4& globalInverse = skeleton.globalInverseTransform;
    const bool writeSockets = animator.HasSocket()
        && SceneManagers->m_isGameStart && nullptr != animator.GetOwner();

    std::vector<math::matrix4x4> globals(boneCount, rootTransform);
    for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
    {
        const experiment::Bone& bone = skeleton.bones[boneIndex];
        const math::matrix4x4 parentGlobal = bone.parent.IsValid()
            ? globals[bone.parent.Value()] : rootTransform;

        bool hasAnyAnimation = false;
        for (const auto& table : controllerHasChannel)
        {
            if (boneIndex < table.size() && table[boneIndex])
            {
                hasAnyAnimation = true;
                break;
            }
        }
        if (!hasAnyAnimation || boneIndex >= MAX_BONES)
        {
            globals[boneIndex] = parentGlobal;
            continue;
        }

        const BoneRegion region = boneIndex < animator.m_experimentBoneRegions.size()
            ? static_cast<BoneRegion>(animator.m_experimentBoneRegions[boneIndex])
            : BoneRegion::Root;

        math::matrix4x4 globalTransform{};
        for (auto& sharedController : animator.m_animationControllers)
        {
            AnimationController* controller = sharedController.get();
            if (nullptr == controller) continue;
            const math::matrix4x4 candidate =
                controller->m_LocalTransforms[boneIndex] * parentGlobal;
            if (controller->m_isBlend == false)
            {
                if (controller->IsUseLayer() == false) continue;
                AvatarMask* mask = controller->GetAvatarMask();
                if (mask != nullptr)
                {
                    if (mask->isHumanoid)
                    {
                        if (mask->IsBoneEnabled(region))
                        {
                            globalTransform = candidate;
                        }
                    }
                    else if (mask->IsBoneEnabled(bone.name))
                    {
                        animator.m_localTransforms[boneIndex] =
                            controller->m_LocalTransforms[boneIndex];
                        globalTransform = candidate;
                    }
                }
                else
                {
                    globalTransform = candidate;
                }
            }
            else
            {
                AvatarMask* mask = controller->GetAvatarMask();
                if (mask != nullptr)
                {
                    if (mask->isHumanoid)
                    {
                        if (mask->IsBoneEnabled(region))
                        {
                            globalTransform = candidate;
                        }
                    }
                    else if (mask->IsBoneEnabled(bone.name))
                    {
                        animator.m_localTransforms[boneIndex] =
                            controller->m_LocalTransforms[boneIndex];
                        globalTransform = candidate;
                    }
                }
                else
                {
                    globalTransform = candidate;
                }
            }
        }

        animator.m_FinalTransforms[boneIndex] =
            bone.inverseBindMatrix * globalTransform * globalInverse;

        if (writeSockets)
        {
            for (auto& socket : animator.socketvec)
            {
                if (bone.name == socket->m_ObjectName)
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
// I6-B4b — 파리티 하네스를 experiment 단독 평가로 좁혔다. legacy 재귀가
// 죽었으므로 대조할 팔이 없다 — 남은 쓸모는 **결정적 표본으로 제품
// 포즈를 산출**해 주는 것이고, 게이트는 그것을 골든 digest로 잰다(6b).
//
// ★ 이것은 정확성 축의 강등이다. 축 6은 legacy와의 값 대조라 "맞다"를
//   말할 수 있었지만, 이제 말할 수 있는 것은 "어제와 같다"뿐이다.
//   B4a가 그 인수인계를 겹치는 구간에서 증명해 두었다.
bool AnimationJob::EvaluateExperimentPose(Animator& animator,
    const experiment::Skeleton& experimentSkeleton, int clipIndex,
    float time, math::matrix4x4* outExperiment)
{
    if (nullptr == outExperiment) return false;
    if (clipIndex < 0
        || static_cast<std::size_t>(clipIndex)
            >= experimentSkeleton.clips.size())
    {
        return false;
    }

    // 살아 있는 컴포넌트를 빌려 쓰므로 팔레트·선택 인덱스를 원복한다.
    std::vector<math::matrix4x4> savedLocal(
        animator.m_localTransforms, animator.m_localTransforms + MAX_BONES);
    std::vector<math::matrix4x4> savedFinal(
        animator.m_FinalTransforms, animator.m_FinalTransforms + MAX_BONES);
    const uint32_t savedChosen = animator.m_AnimIndexChosen;
    animator.m_AnimIndexChosen = static_cast<uint32_t>(clipIndex);

    // 항등에서 시작한다 — "채널 없는 슬롯 미기록" 규약 아래에서도 결과가
    // 결정적이어야 골든이 성립한다.
    std::fill(animator.m_FinalTransforms,
        animator.m_FinalTransforms + MAX_BONES, math::matrix4x4::identity());
    UpdateExperimentPose(animator, experimentSkeleton, nullptr,
        clipIndex, -1, time, 0.f);
    std::copy(animator.m_FinalTransforms,
        animator.m_FinalTransforms + MAX_BONES, outExperiment);

    std::copy(savedLocal.begin(), savedLocal.end(), animator.m_localTransforms);
    std::copy(savedFinal.begin(), savedFinal.end(), animator.m_FinalTransforms);
    animator.m_AnimIndexChosen = savedChosen;
    return true;
}
