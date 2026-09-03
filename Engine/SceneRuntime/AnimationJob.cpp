#include "AnimationJob.h"
#include "RenderScene.h"
#include "BoneRegion.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Benchmark.hpp"
#include "AnimationController.h"
#include "Animator.h"
#include "Socket.h"
#include "Assets/ModelAssetGeneration.h"   // PHASE 3.75 MBC8
#include "Assets/ModelAnimationSampler.h"  // PHASE 3.75 MBC8
#include <limits>
#include <span>
#include <atomic> // D34b: 루트 본 부재 1회 경고
#include "ModelConsumptionDiagnostics.h" // MBC10: 틱 경로 관측(읽기 전용 계수)
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
        // legacy 재현: 소켓 행렬은 비블렌드 순회(UpdateBone)에서만 계산됐다.
        const bool writeSockets = nullptr == nextClip && animator.HasSocket()
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

            if (boneIndex < MAX_BONES)
            {
                animator.m_localTransforms[boneIndex] = local;
                animator.m_FinalTransforms[boneIndex] =
                    source.InverseBind(boneIndex) * global * globalInverse;
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
        // legacy UpdateBoneLayer 재현(단일 순회). 컨트롤러들의 m_LocalTransforms
        // (앞선 포즈 패스가 채움)를 마스크로 골라 합성한다. legacy의 알려진 결함도
        // 그대로 승계한다: 채널이 있는 본에서 모든 컨트롤러가 마스크에 걸리면
        // globalTransform이 기본 초기화(영행렬 — math 규약)로 남아 팔레트에 나간다.
        const std::size_t boneCount = source.BoneCount();

        // 컨트롤러별 "이 본에 채널이 있는가" 표 — legacy hasAnyAnimation 재현
        // (useController 필터가 없는 것까지 동일).
        std::vector<std::vector<std::uint8_t>> controllerHasChannel(
            animator.m_animationControllers.size());
        for (std::size_t slot = 0; slot < controllerHasChannel.size(); ++slot)
        {
            controllerHasChannel[slot].assign(boneCount, 0);
            AnimationController* controller = animator.m_animationControllers[slot].get();
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

            const BoneRegion region = boneIndex < animator.m_boneRegions.size()
                ? static_cast<BoneRegion>(animator.m_boneRegions[boneIndex])
                : BoneRegion::Root;
            const std::string& boneName = source.BoneName(boneIndex);

            math::matrix4x4 globalTransform{};
            for (auto& sharedController : animator.m_animationControllers)
            {
                AnimationController* controller = sharedController.get();
                if (nullptr == controller) continue;
                const math::matrix4x4 candidate =
                    controller->m_LocalTransforms[boneIndex] * parentGlobal;
                if (controller->m_isBlend == false && controller->IsUseLayer() == false)
                    continue;
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
                    else if (mask->IsBoneEnabled(boneName))
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

                const auto* clip = source.ClipAt(controller->GetAnimationIndex());
                if (nullptr == clip) continue;
                const float duration = static_cast<float>(Source::Duration(*clip));
                controller->m_timeElapsed += deltaT
                    * static_cast<float>(Source::TicksPerSecond(*clip)) * animationSpeed;
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
                    const auto* nextClip = source.ClipAt(controller->GetNextAnimationIndex());
                    if (nextClip)
                    {
                        const float nextDuration =
                            static_cast<float>(Source::Duration(*nextClip));
                        controller->m_nextTimeElapsed += deltaT
                            * static_cast<float>(Source::TicksPerSecond(*nextClip));
                        controller->m_nextTimeElapsed =
                            fmod(controller->m_nextTimeElapsed, nextDuration);
                        controller->preNextAnimationProgress =
                            controller->nextAnimationProgress;
                        controller->nextAnimationProgress =
                            controller->m_nextTimeElapsed / nextDuration;
                        UpdatePose(animator, source, controller,
                            controller->GetAnimationIndex(),
                            controller->GetNextAnimationIndex(),
                            controller->m_timeElapsed,
                            controller->m_nextTimeElapsed);
                    }
                }
                else
                {
                    UpdatePose(animator, source, controller,
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

            UpdateLayer(animator, source);
        }
        else if (animator.m_animationControllers.empty())
        {
            const auto* clip = source.ClipAt(static_cast<int>(animator.m_AnimIndexChosen));
            if (nullptr == clip) return;
            const float duration = static_cast<float>(Source::Duration(*clip));
            animator.m_TimeElapsed += deltaT
                * static_cast<float>(Source::TicksPerSecond(*clip));
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
                const auto* nextClip = source.ClipAt(animator.nextAnimIndex);
                if (nullptr == nextClip) return;
                const float nextDuration =
                    static_cast<float>(Source::Duration(*nextClip));
                animator.m_nextTimeElapsed += deltaT
                    * static_cast<float>(Source::TicksPerSecond(*nextClip));
                animator.m_nextTimeElapsed =
                    fmod(animator.m_nextTimeElapsed, nextDuration);
                UpdatePose(animator, source, nullptr,
                    static_cast<int>(animator.m_AnimIndexChosen),
                    animator.nextAnimIndex,
                    animator.m_TimeElapsed, animator.m_nextTimeElapsed);
            }
            else
            {
                UpdatePose(animator, source, nullptr,
                    static_cast<int>(animator.m_AnimIndexChosen), -1,
                    animator.m_TimeElapsed, 0.f);
            }
        }
        else // 컨트롤러 1개
        {
            AnimationController* controller = animator.m_animationControllers[0].get();
            const auto* clip = source.ClipAt(controller->GetAnimationIndex());
            if (nullptr == clip) return;
            const float duration = static_cast<float>(Source::Duration(*clip));
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
                * static_cast<float>(Source::TicksPerSecond(*clip)) * animationSpeed;
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
                const auto* nextClip = source.ClipAt(controller->GetNextAnimationIndex());
                if (nullptr == nextClip) return;
                const float nextDuration =
                    static_cast<float>(Source::Duration(*nextClip));
                controller->m_nextTimeElapsed += deltaT
                    * static_cast<float>(Source::TicksPerSecond(*nextClip));
                controller->m_nextTimeElapsed =
                    fmod(controller->m_nextTimeElapsed, nextDuration);
                controller->preNextAnimationProgress =
                    controller->nextAnimationProgress;
                controller->nextAnimationProgress =
                    controller->m_nextTimeElapsed / nextDuration;
                UpdatePose(animator, source, controller,
                    controller->GetAnimationIndex(),
                    controller->GetNextAnimationIndex(),
                    controller->m_timeElapsed, controller->m_nextTimeElapsed);
            }
            else
            {
                UpdatePose(animator, source, controller,
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
            animator.m_localTransforms, animator.m_localTransforms + MAX_BONES);
        std::vector<math::matrix4x4> savedFinal(
            animator.m_FinalTransforms, animator.m_FinalTransforms + MAX_BONES);
        const uint32_t savedChosen = animator.m_AnimIndexChosen;
        animator.m_AnimIndexChosen = static_cast<uint32_t>(clipIndex);

        std::fill(animator.m_FinalTransforms,
            animator.m_FinalTransforms + MAX_BONES, math::matrix4x4::identity());
        UpdatePose(animator, source, nullptr, clipIndex, -1, time, 0.f);
        std::copy(animator.m_FinalTransforms,
            animator.m_FinalTransforms + MAX_BONES, outPose);

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
