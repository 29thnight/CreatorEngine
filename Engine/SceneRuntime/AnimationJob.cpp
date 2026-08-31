#include "AnimationJob.h"
#include "RenderScene.h"
#include "Skeleton.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Benchmark.hpp"
#include "AnimationController.h"
#include "Animator.h"
#include "Socket.h"
#include <atomic> // D34b: 루트 본 부재 1회 경고
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
            Skeleton* skeleton = animator->m_Skeleton;
            if (!skeleton) return;
            // 루트 없는 스켈레톤은 틱할 수 없다 — UpdateBone(nullptr)이
            // 워커 스레드에서 0x80 널 역참조로 죽는다(D34b 게이트 실측 —
            // 저장·재로드된 스킨 씬). 조용히 건너뛰지 않고 1회 알린다:
            // 스켈레톤 재구성 경로(역브리지·postLoad)의 결함이 여기로 온다.
            if (nullptr == skeleton->m_rootBone)
            {
                static std::atomic_flag warned = ATOMIC_FLAG_INIT;
                if (!warned.test_and_set())
                {
                    Debug->LogError("[AnimationJob] 스켈레톤에 루트 본이 없다 — "
                        "틱을 건너뛴다(본 "
                        + std::to_string(skeleton->m_bones.size()) + "개)");
                }
                return;
            }

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

            //컨트롤러별로 상,하체 등등이 분리되있다면
            if (animator->UsesMultipleControllers() == true)
            {
                for (auto& sharedanimationcontroller : animator->m_animationControllers)
                {
                    float animationspeed = 1;
                  
                    AnimationController* animationcontroller = sharedanimationcontroller.get();
                    if (animationcontroller == nullptr || !animationcontroller->useController) continue;

                    AnimationState* curState = animationcontroller->m_curState;
                    if (curState)
                    {
                        animationspeed = curState->animationSpeed;
                        if (curState->useMultipler)
                        {
                            animationspeed *= curState->multiplerAnimationSpeed;
                        }
                    }
                    Animation& animation = skeleton->m_animations[animationcontroller->GetAnimationIndex()];
                    animationcontroller->m_timeElapsed += deltaT * animation.m_ticksPerSecond * animationspeed;
                    if (animation.m_isLoop == true)
                    {
                        animationcontroller->m_timeElapsed = fmod(animationcontroller->m_timeElapsed, animation.m_duration); //&&&&&
                        //animationcontroller->curAnimationProgress = animationcontroller->m_timeElapsed / animation.m_duration;
                    }
                    else
                    {
                        if (animationcontroller->m_timeElapsed >= animation.m_duration)
                        {
                            animationcontroller->m_timeElapsed = animation.m_duration;
                            if (animationcontroller->curAnimationProgress >= 0.95)
                                animationcontroller->endAnimation = true;
                        }
                        
                    }
                    //animation.preAnimationProgress = animation.curAnimationProgress;
                    //animation.curAnimationProgress = animationcontroller->m_timeElapsed / animation.m_duration;
                    animationcontroller->preCurAnimationProgress = animationcontroller->curAnimationProgress;
                    animationcontroller->curAnimationProgress = animationcontroller->m_timeElapsed / animation.m_duration;
                    math::matrix4x4 rootTransform = skeleton->m_rootTransform;
                    if (animationcontroller->m_isBlend)
                    {
                        Animation& nextanimation = skeleton->m_animations[animationcontroller->GetNextAnimationIndex()];
                        animationcontroller->m_nextTimeElapsed += deltaT * nextanimation.m_ticksPerSecond;
                        animationcontroller->m_nextTimeElapsed = fmod(animationcontroller->m_nextTimeElapsed, nextanimation.m_duration);
                        animationcontroller->preNextAnimationProgress = animationcontroller->nextAnimationProgress;
                        animationcontroller->nextAnimationProgress = animationcontroller->m_nextTimeElapsed / nextanimation.m_duration;
                        UpdateBlendBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animationcontroller).m_timeElapsed, (*animationcontroller).m_nextTimeElapsed);
                    }
                    else
                    {
                        UpdateBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animationcontroller).m_timeElapsed);
                    }

                    if (deltaT <= 0.f) continue;
                    skeleton->m_animations[animationcontroller->GetAnimationIndex()].InvokeEvent(animator, animationcontroller->curAnimationProgress, animationcontroller->preCurAnimationProgress);
                    if (animationcontroller->m_isBlend == true) //블렌딩중엔 다음애니메이션 프레임이벤트도
                    {
                        skeleton->m_animations[animationcontroller->GetNextAnimationIndex()].InvokeEvent(animator, animationcontroller->nextAnimationProgress, animationcontroller->preNextAnimationProgress);
                    }
                    
                }
                
                math::matrix4x4 rootTransform = skeleton->m_rootTransform;

                UpdateBoneLayer(skeleton->m_rootBone, *animator , rootTransform);
               
            }
            else
            {
                AnimationController* animationcontroller = nullptr;
                if (animator->m_animationControllers.empty()) //아예없으면
                {
                    if (skeleton->m_animations.empty()) return;
                    Animation& animation = skeleton->m_animations[animator->m_AnimIndexChosen];
                    animator->m_TimeElapsed += deltaT * animation.m_ticksPerSecond;

                    if (animation.m_isLoop == true)
                    {
                        animator->m_TimeElapsed = fmod(animator->m_TimeElapsed, animation.m_duration);
                    }
                    else
                    {
                        if (animator->m_TimeElapsed >= animation.m_duration)
                        {
                            animator->m_TimeElapsed = animation.m_duration;
                        }
                    }
                   // animation.preAnimationProgress = animation.curAnimationProgress;
                   // animation.curAnimationProgress = animator->m_TimeElapsed / animation.m_duration;
                    math::matrix4x4 rootTransform = skeleton->m_rootTransform;
                    if (animator->m_isBlend)
                    {
                        if (animator->nextAnimIndex == -1)
                        {
                            //Debug->Log("다음애니메이션인덱스를확인해주세요");
                            return;
                        }
                        Animation& nextanimation = skeleton->m_animations[animator->nextAnimIndex];
                        animator->m_nextTimeElapsed += deltaT * nextanimation.m_ticksPerSecond;
                        animator->m_nextTimeElapsed = fmod(animator->m_nextTimeElapsed, nextanimation.m_duration);
                        UpdateBlendBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animator).m_TimeElapsed, (*animator).m_nextTimeElapsed);
                    }
                    else
                    {
                        UpdateBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animator).m_TimeElapsed);
                    }
                    //animation.InvokeEvent(animator);
                }
                else //한개만 있으면
                {
                    animationcontroller = animator->m_animationControllers[0].get();
                    if (skeleton->m_animations.empty()) return;
                    Animation& animation = skeleton->m_animations[animationcontroller->GetAnimationIndex()];
                    AnimationState* curState = animationcontroller->m_curState;
                    float animationspeed = 1;
                    if (curState)
                    {
                        animationspeed = curState->animationSpeed;
                        if (curState->useMultipler)
                        {
                            animationspeed *= curState->multiplerAnimationSpeed;
                        }
                    }
                    animationcontroller->m_timeElapsed += deltaT * animation.m_ticksPerSecond * animationspeed;
                    //animationcontroller->curAnimationProgress = animationcontroller->m_timeElapsed / animation.m_duration;
                    if (animation.m_isLoop == true)
                    {
                        animationcontroller->m_timeElapsed = fmod(animationcontroller->m_timeElapsed, animation.m_duration); //&&&&&
                    }
                    else
                    {
                        if (animationcontroller->m_timeElapsed >= animation.m_duration)
                        {
                            animationcontroller->m_timeElapsed = animation.m_duration;
                            if (animationcontroller->curAnimationProgress >= 0.95)
                                animationcontroller->endAnimation = true;
                        }

                    }

                    //auto& animation = skeleton->m_animations[animationcontroller->GetAnimationIndex()];
                    //animation.preAnimationProgress = animation.curAnimationProgress;
                   // animation.curAnimationProgress = animationcontroller->curAnimationProgress;
                    animationcontroller->preCurAnimationProgress = animationcontroller->curAnimationProgress;
                    animationcontroller->curAnimationProgress = animationcontroller->m_timeElapsed / animation.m_duration;
                    math::matrix4x4 rootTransform = skeleton->m_rootTransform;

                    if (animator->m_isBlend)
                    {
                        if (animator->nextAnimIndex == -1)
                        {
                            //Debug->Log("다음애니메이션인덱스를확인해주세요");
                            return;
                        }
                        /*Animation& nextanimation = skeleton->m_animations[animationcontroller->GetNextAnimationIndex()];
                        animationcontroller->m_nextTimeElapsed += delta * nextanimation.m_ticksPerSecond;
                        animationcontroller->m_nextTimeElapsed = fmod(animationcontroller->m_nextTimeElapsed, nextanimation.m_duration);
                        UpdateBlendBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animationcontroller).m_timeElapsed, (*animationcontroller).m_nextTimeElapsed);*/


                        Animation& nextanimation = skeleton->m_animations[animationcontroller->GetNextAnimationIndex()];
                        animationcontroller->m_nextTimeElapsed += deltaT * nextanimation.m_ticksPerSecond;
                        animationcontroller->m_nextTimeElapsed = fmod(animationcontroller->m_nextTimeElapsed, nextanimation.m_duration);
                        animationcontroller->preNextAnimationProgress = animationcontroller->nextAnimationProgress;
                        animationcontroller->nextAnimationProgress = animationcontroller->m_nextTimeElapsed / nextanimation.m_duration;
                        UpdateBlendBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animationcontroller).m_timeElapsed, (*animationcontroller).m_nextTimeElapsed);




                    }
                    else
                    {
                        UpdateBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animationcontroller).m_timeElapsed);
                    }
                    // skeleton->m_animations[animationcontroller->GetAnimationIndex()].InvokeEvent(animator);

                    if (deltaT > 0.f) {
                        skeleton->m_animations[animationcontroller->GetAnimationIndex()].InvokeEvent(animator, animationcontroller->curAnimationProgress, animationcontroller->preCurAnimationProgress);
                        if (animationcontroller->m_isBlend == true) //블렌딩중엔 다음애니메이션 프레임이벤트도
                        {
                            skeleton->m_animations[animationcontroller->GetNextAnimationIndex()].InvokeEvent(animator, animationcontroller->nextAnimationProgress, animationcontroller->preNextAnimationProgress);
                        }
                    }
                }
                
            }

            if (animator->HasSocket())
            {
                if (SceneManagers->m_isGameStart == false || animator->GetOwner() == nullptr)
                {


                }
                else
                {

                    for (auto& socket : animator->socketvec)
                    {
                        socket->transform.SetLocalMatrix(socket->m_boneMatrix);
                        socket->Update();
                    }
                }
            }


        });
    }

    m_UpdateThreadPool->NotifyAllAndWait();
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

void AnimationJob::UpdateBlendBone(Bone* bone, Animator& animator,
    AnimationController* controller, const math::matrix4x4& parentTransform,
    float time, float nextanitime)
{
    Skeleton* skeleton = animator.m_Skeleton;
    Animation* animation;
    Animation* nextanimation;
    if (controller)
    {
        animation = &skeleton->m_animations[controller->GetAnimationIndex()];
        nextanimation = &skeleton->m_animations[controller->GetNextAnimationIndex()];
    }
    else
    {
        animation = &skeleton->m_animations[animator.m_AnimIndexChosen];
        nextanimation = &skeleton->m_animations[animator.nextAnimIndex];
    }

    std::string& boneName = bone->m_name;


    auto it = animation->m_nodeAnimations.find(boneName);
    if (it == animation->m_nodeAnimations.end())
    {
        for (Bone* child : bone->m_children)
        {
            UpdateBlendBone(child, animator, controller,parentTransform, time, nextanitime);
        }
        return;
    }

    NodeAnimation& nodeAnim = animation->m_nodeAnimations[boneName];
    NodeAnimation& nextnodeAnim = nextanimation->m_nodeAnimations[boneName];
    
    math::matrix4x4 nodeTransform = calculAni(nodeAnim, time, &animation->curKey);
    
    math::matrix4x4 nextnodeTransform = calculAni(
        nextnodeAnim, nextanitime, &nextanimation->curKey);
    math::matrix4x4 blendTransform = BlendAni(
        nodeTransform, nextnodeTransform, animator.blendT);
    math::matrix4x4 globalTransform = blendTransform * parentTransform;

    
    animator.m_FinalTransforms[bone->m_index] =
        bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    animator.m_localTransforms[bone->m_index] = blendTransform;
    // ??skeleton->m_sockets瑜??묒뼱 socket->m_boneMatrix瑜?怨꾩궛?섎뜕 釉붾줉??
    //   ?ш린 ?덉뿀?? 洹?蹂닿??뚮뒗 ??鍮꾩뼱 ?덉뼱(Skeleton.h 李멸퀬) ??踰덈룄
    //   ???곸씠 ?녿떎 ???댁븘 ?덈뒗 怨꾩궛? animator.socketvec瑜??꾨뒗 ?꾨옒履?
    //   ???먮━?닿퀬, ?닿쾬? 洹?以묐났?댁뿀??
    if (controller)
    {
        controller->m_LocalTransforms[bone->m_index] = blendTransform;
    }
    for (Bone* child : bone->m_children)
    {
        UpdateBlendBone(child, animator, controller,globalTransform, time, nextanitime);
    }
}

void AnimationJob::UpdateBone(Bone* bone, Animator& animator,
    AnimationController* controller, const math::matrix4x4& parentTransform,
    float time)
{
    Skeleton* skeleton = animator.m_Skeleton;
    std::string& boneName = bone->m_name;
    Animation* animation;
    if (controller)
    {
        animation = &skeleton->m_animations[controller->GetAnimationIndex()];
        auto mask = controller->GetAvatarMask();
    }
    else
    {
        animation = &skeleton->m_animations[animator.m_AnimIndexChosen];
    }
    auto it = animation->m_nodeAnimations.find(boneName);
    if (it == animation->m_nodeAnimations.end())
    {
        for (Bone* child : bone->m_children)
        {
            UpdateBone(child, animator, controller,parentTransform, time);
        }
        return;
    }
    NodeAnimation& nodeAnim = animation->m_nodeAnimations[boneName];
    math::matrix4x4 nodeTransform = calculAni(nodeAnim, time, &animation->curKey);
    math::matrix4x4 globalTransform = nodeTransform * parentTransform;
    
    animator.m_localTransforms[bone->m_index] = nodeTransform;
    animator.m_FinalTransforms[bone->m_index] =
        bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
 

    if (animator.HasSocket())
    {
        if (SceneManagers->m_isGameStart == false || animator.GetOwner() == nullptr)
        {

        }
        else
        {
            for (auto& socket : animator.socketvec)
            {
                if (bone->m_name == socket->m_ObjectName)
                {
                    socket->m_boneMatrix = globalTransform * socket->m_offset;
                    socket->m_boneMatrix = socket->m_boneMatrix
                        * animator.GetOwner()->Transform_().GetWorldMatrix();
                }
            }
        }
    }


    if (controller)
    {
        controller->m_LocalTransforms[bone->m_index] = nodeTransform;
    }
    for (Bone* child : bone->m_children)
    {
        UpdateBone(child, animator, controller,globalTransform, time);
    }
}


void AnimationJob::UpdateBoneLayer(Bone* bone, Animator& animator,
    const math::matrix4x4& parentTransform)
{
    Skeleton* skeleton = animator.m_Skeleton;
    std::string& boneName = bone->m_name;
    bool isCalculAnimate = true;
    math::matrix4x4 globalTransform{};
    
    Animation* animation;

    bool hasAnyAnimation = false;
    for (auto& precontroller : animator.m_animationControllers)
    {
        animation = &skeleton->m_animations[precontroller->GetAnimationIndex()];
        auto it = animation->m_nodeAnimations.find(boneName);
        if (it != animation->m_nodeAnimations.end())
        {
            hasAnyAnimation = true;
            break;
        }
    }

    if (!hasAnyAnimation)
    {
        for (Bone* child : bone->m_children)
            UpdateBoneLayer(child, animator, parentTransform);
        return;
    }

    for (auto& controller : animator.m_animationControllers)
    {
        if (controller->m_isBlend == false)
        {
            if (controller->IsUseLayer() == true)
            {
                auto mask = controller->GetAvatarMask();


                if (mask != nullptr) //마스크 있으면
                {

                    if (mask->isHumanoid)
                    {
                        if (mask->IsBoneEnabled(bone->m_region) == true) //&&&&& region이아니라  mask->IsBoneEnabled(); 로 수정할것
                        {
                            //animator.m_localTransforms[bone->m_index] = controller->m_LocalTransforms[bone->m_index];
                            globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;
                        }
                        else
                        {
                            // globalTransform = parentTransform;
                        }
                    }
                    else
                    {
                        if (mask->IsBoneEnabled(bone->m_name) == true)
                        {
                            animator.m_localTransforms[bone->m_index] = controller->m_LocalTransforms[bone->m_index];
                            globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;

                        }
                        else
                        {
                            //globalTransform = parentTransform;
                        }
                    }
                }
                else
                {
                    //animator.m_localTransforms[bone->m_index] = controller->m_LocalTransforms[bone->m_index];
                    globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;
                }
            }
        }
        else
        {
            auto mask = controller->GetAvatarMask();
            if (mask != nullptr) //마스크 있으면
            {

                if (mask->isHumanoid)
                {
                    if (mask->IsBoneEnabled(bone->m_region) == true) //&&&&& region이아니라  mask->IsBoneEnabled(); 로 수정할것
                    {
                        //animator.m_localTransforms[bone->m_index] = controller->m_LocalTransforms[bone->m_index];
                        globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;
                    }
                }
                else
                {
                    if (mask->IsBoneEnabled(bone->m_name) == true)
                    {
                        animator.m_localTransforms[bone->m_index] = controller->m_LocalTransforms[bone->m_index];
                        globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;

                    }
                }
            }
            else
            {
                //animator.m_localTransforms[bone->m_index] = controller->m_LocalTransforms[bone->m_index];
                globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;
            }
        }
    }

    //int controllerSize = animator.m_animationControllers.size();
    //for (int i = controllerSize - 1; i >= 0; --i)
    //{
    //    auto& controller = animator.m_animationControllers[i];

    //    if (controller->IsUseLayer())
    //    {
    //        auto mask = controller->GetAvatarMask();

    //        if (mask != nullptr) // 마스크 있으면
    //        {
    //            if (mask->isHumanoid)
    //            {
    //                if (mask->IsBoneEnabled(bone->m_region))
    //                {
    //                    globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;
    //                }
    //            }
    //            else
    //            {
    //                if (mask->IsBoneEnabled(bone->m_name))
    //                {
    //                    animator.m_localTransforms[bone->m_index] = controller->m_LocalTransforms[bone->m_index];
    //                    globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;
    //                }
    //            }
    //        }
    //        else
    //        {
    //            globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;
    //        }
    //    }
    //}

    animator.m_FinalTransforms[bone->m_index] =
        bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    
    if (animator.HasSocket())
    {
        if (SceneManagers->m_isGameStart == false || animator.GetOwner() == nullptr)
        {

        }
        else
        {
            for (auto& socket : animator.socketvec)
            {
                if (bone->m_name == socket->m_ObjectName)
                {
                    socket->m_boneMatrix = globalTransform * socket->m_offset;
                    socket->m_boneMatrix = socket->m_boneMatrix
                        * animator.GetOwner()->Transform_().GetWorldMatrix();
                }
            }
        }
    }

    for (Bone* child : bone->m_children)
    {
        UpdateBoneLayer(child, animator,globalTransform);
    }
    
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

math::matrix4x4 AnimationJob::calculAni(NodeAnimation& nodeAnim, float time,
    int* _key)
{
    float t = 0;
    // Translation
    math::vector4 interpPos = nodeAnim.m_positionKeys[0].m_position;
    if (nodeAnim.m_positionKeys.size() > 1)
    {
        int posKeyIdx = CurrentKeyIndex<NodeAnimation::PositionKey>(nodeAnim.m_positionKeys, time);
        int nPosKeyIdx = posKeyIdx + 1;

        if (_key)
            *_key = posKeyIdx;
        NodeAnimation::PositionKey posKey = nodeAnim.m_positionKeys[posKeyIdx];
        NodeAnimation::PositionKey nPosKey = nodeAnim.m_positionKeys[nPosKeyIdx];

        t = (time - posKey.m_time) / (nPosKey.m_time - posKey.m_time);
        interpPos = math::lerp(posKey.m_position, nPosKey.m_position, t);
    }

    // Rotation
    math::quaternion interpQuat = nodeAnim.m_rotationKeys[0].m_rotation;
    if (nodeAnim.m_rotationKeys.size() > 1)
    {
        int rotKeyIdx = CurrentKeyIndex<NodeAnimation::RotationKey>(nodeAnim.m_rotationKeys, time);
        int nRotKeyIdx = rotKeyIdx + 1;

        NodeAnimation::RotationKey rotKey = nodeAnim.m_rotationKeys[rotKeyIdx];
        NodeAnimation::RotationKey nRotKey = nodeAnim.m_rotationKeys[nRotKeyIdx];

        t = (time - rotKey.m_time) / (nRotKey.m_time - rotKey.m_time);
        interpQuat = math::slerp(rotKey.m_rotation, nRotKey.m_rotation, t);

    }

    // Scaling
    float interpScale = nodeAnim.m_scaleKeys[0].m_scale.x;

    if (nodeAnim.m_scaleKeys.size() > 1)
    {
        int scalKeyIdx = CurrentKeyIndex<NodeAnimation::ScaleKey>(nodeAnim.m_scaleKeys, time);
        int nScalKeyIdx = scalKeyIdx + 1;

        NodeAnimation::ScaleKey scalKey = nodeAnim.m_scaleKeys[scalKeyIdx];
        NodeAnimation::ScaleKey nScalKey = nodeAnim.m_scaleKeys[nScalKeyIdx];

        t = (time - scalKey.m_time) / (nScalKey.m_time - scalKey.m_time);
        interpScale = lerp(scalKey.m_scale.x, nScalKey.m_scale.x, t);
    }

    return math::compose(
        math::vector3{ interpScale, interpScale, interpScale }, interpQuat,
        math::vector3{ interpPos.x, interpPos.y, interpPos.z });
}
