#ifndef DYNAMICCPP_EXPORTS
#include "AnimationJob.h"
#include "RenderScene.h"
#include "Skeleton.h"
#include "SceneManager.h"
#include "RenderableComponents.h"
#include "Scene.h"
#include "Benchmark.hpp"
#include "AnimationController.h"
#include "Socket.h"
using namespace DirectX;

std::vector<std::weak_ptr<Animator>> m_currAnimator;

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
    delete m_UpdateThreadPool;
	m_currAnimator.clear();
}

void AnimationJob::Update(float deltaTime)
{
    Scene* scene = SceneManagers->GetActiveScene();
	RenderScene* renderScene = SceneManagers->GetRenderScene();
    uint32 currSize = scene->m_SceneObjects.size();

    m_currAnimator.clear();
    for (auto& [guid, animator] : renderScene->GetAnimatorMap())
    {
        if (nullptr == animator || !animator->IsEnabled()) continue;

        m_currAnimator.push_back(animator);
    }

    for(auto& weakanimator : m_currAnimator)
    {
        auto animator = weakanimator.lock();
        if (animator == nullptr) return;
        std::vector<std::weak_ptr<AnimationController>> controllers;
        for (auto& sharedcontroller : animator->m_animationControllers)
        {
            controllers.push_back(sharedcontroller);
        }
        
        for (auto& controller : controllers)
        {
            if (controller.lock() == nullptr)
                return;
        }
        m_UpdateThreadPool->Enqueue([this, animator, controllers, delta = deltaTime]
        {
            Skeleton* skeleton = animator->m_Skeleton;
            if (!skeleton) return;
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
                    animationcontroller->m_timeElapsed += delta * animation.m_ticksPerSecond * animationspeed;
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
                    XMMATRIX rootTransform = skeleton->m_rootTransform;
                    if (animationcontroller->m_isBlend)
                    {
                        Animation& nextanimation = skeleton->m_animations[animationcontroller->GetNextAnimationIndex()];
                        animationcontroller->m_nextTimeElapsed += delta * nextanimation.m_ticksPerSecond;
                        animationcontroller->m_nextTimeElapsed = fmod(animationcontroller->m_nextTimeElapsed, nextanimation.m_duration);
                        animationcontroller->preNextAnimationProgress = animationcontroller->nextAnimationProgress;
                        animationcontroller->nextAnimationProgress = animationcontroller->m_nextTimeElapsed / nextanimation.m_duration;
                        UpdateBlendBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animationcontroller).m_timeElapsed, (*animationcontroller).m_nextTimeElapsed);
                    }
                    else
                    {
                        UpdateBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animationcontroller).m_timeElapsed);
                    }

                    skeleton->m_animations[animationcontroller->GetAnimationIndex()].InvokeEvent(animator.get(), animationcontroller->curAnimationProgress, animationcontroller->preCurAnimationProgress);
                    if (animationcontroller->m_isBlend == true) //블렌딩중엔 다음애니메이션 프레임이벤트도
                    {
                        skeleton->m_animations[animationcontroller->GetNextAnimationIndex()].InvokeEvent(animator.get(), animationcontroller->nextAnimationProgress, animationcontroller->preNextAnimationProgress);
                    }
                    
                }
                
                XMMATRIX rootTransform = skeleton->m_rootTransform;

                UpdateBoneLayer(skeleton->m_rootBone, *animator , rootTransform);
               
            }
            else
            {
                AnimationController* animationcontroller = nullptr;
                if (animator->m_animationControllers.empty()) //아예없으면
                {
                    if (skeleton->m_animations.empty()) return;
                    Animation& animation = skeleton->m_animations[animator->m_AnimIndexChosen];
                    animator->m_TimeElapsed += delta * animation.m_ticksPerSecond;

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
                    XMMATRIX rootTransform = skeleton->m_rootTransform;
                    if (animator->m_isBlend)
                    {
                        if (animator->nextAnimIndex == -1)
                        {
                            //Debug->Log("다음애니메이션인덱스를확인해주세요");
                            return;
                        }
                        Animation& nextanimation = skeleton->m_animations[animator->nextAnimIndex];
                        animator->m_nextTimeElapsed += delta * nextanimation.m_ticksPerSecond;
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
                    animationcontroller->m_timeElapsed += delta * animation.m_ticksPerSecond;
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
                    XMMATRIX rootTransform = skeleton->m_rootTransform;

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
                        animationcontroller->m_nextTimeElapsed += delta * nextanimation.m_ticksPerSecond;
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
                    skeleton->m_animations[animationcontroller->GetAnimationIndex()].InvokeEvent(animator.get(), animationcontroller->curAnimationProgress, animationcontroller->preCurAnimationProgress);
                    if (animationcontroller->m_isBlend == true) //블렌딩중엔 다음애니메이션 프레임이벤트도
                    {
                        skeleton->m_animations[animationcontroller->GetNextAnimationIndex()].InvokeEvent(animator.get(), animationcontroller->nextAnimationProgress, animationcontroller->preNextAnimationProgress);
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
    m_currAnimator.clear();
    Scene* scene = SceneManagers->GetActiveScene();
    RenderScene* renderScene = SceneManagers->GetRenderScene();
	if (nullptr == scene) return;

    for (auto& [guid, animator] : renderScene->GetAnimatorMap())
    {
        if (nullptr == animator || !animator->IsEnabled()) continue;

        m_currAnimator.push_back(animator);
    }
}

void AnimationJob::CleanUp()
{
    m_currAnimator.clear();
	m_objectSize = 0;
}

void AnimationJob::UpdateBlendBone(Bone* bone, Animator& animator, AnimationController* controller,const DirectX::XMMATRIX& parentTransform, float time, float nextanitime)
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
    
    XMMATRIX nodeTransform = calculAni(nodeAnim, time, &animation->curKey);
    
    XMMATRIX nextnodeTransform = calculAni(nextnodeAnim, nextanitime, &nextanimation->curKey);
    XMMATRIX blendTransform = BlendAni(nodeTransform, nextnodeTransform, animator.blendT);
    animator.blendtransform = blendTransform;
    XMMATRIX globalTransform = blendTransform * parentTransform;

    
    animator.m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    bone->m_globalTransform = globalTransform;
    bone->m_localTransform = blendTransform;
    animator.m_localTransforms[bone->m_index] = bone->m_localTransform;
    if (skeleton->HasSocket())
    {
        for (auto& socket : skeleton->m_sockets)
        {
            if (bone->m_name == socket->m_ObjectName)
            {
                socket->m_boneMatrix = bone->m_globalTransform * socket->m_offset;
                socket->m_boneMatrix = socket->m_boneMatrix * animator.GetOwner()->m_transform.GetWorldMatrix();
            }
        }
    }
    if (controller)
    {
        controller->m_LocalTransforms[bone->m_index] = blendTransform;
        controller->m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    }
    for (Bone* child : bone->m_children)
    {
        UpdateBlendBone(child, animator, controller,globalTransform, time, nextanitime);
    }
}

void AnimationJob::UpdateBone(Bone* bone, Animator& animator, AnimationController* controller,const XMMATRIX& parentTransform, float time)
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
    XMMATRIX nodeTransform = calculAni(nodeAnim, time, &animation->curKey);
    XMMATRIX globalTransform = nodeTransform * parentTransform;
    
    bone->m_globalTransform = globalTransform;
    bone->m_localTransform = nodeTransform;
    animator.m_localTransforms[bone->m_index] = bone->m_localTransform;
    animator.m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
 

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
                    socket->m_boneMatrix = socket->m_boneMatrix * animator.GetOwner()->m_transform.GetWorldMatrix();
                }
            }
        }
    }


    if (controller)
    {
        controller->m_LocalTransforms[bone->m_index] = nodeTransform;
        controller->m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    }
    for (Bone* child : bone->m_children)
    {
        UpdateBone(child, animator, controller,globalTransform, time);
    }
}


void AnimationJob::UpdateBoneLayer(Bone* bone, Animator& animator,const DirectX::XMMATRIX& parentTransform)
{
    Skeleton* skeleton = animator.m_Skeleton;
    std::string& boneName = bone->m_name;
    bool isCalculAnimate = true;
    XMMATRIX globalTransform{};
    
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

    //bone->m_globalTransform = globalTransform;
    animator.m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    
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
                    socket->m_boneMatrix = socket->m_boneMatrix * animator.GetOwner()->m_transform.GetWorldMatrix();
                }
            }
        }
    }

    for (Bone* child : bone->m_children)
    {
        UpdateBoneLayer(child, animator,globalTransform);
    }
    
}

XMMATRIX AnimationJob::BlendAni(XMMATRIX curAni, XMMATRIX nextAni, float t)
{
    XMVECTOR scale1, rot1, trans1;
    XMMatrixDecompose(&scale1, &rot1, &trans1, curAni);

    XMVECTOR scale2, rot2, trans2;
    XMMatrixDecompose(&scale2, &rot2, &trans2, nextAni);

    XMVECTOR blendedScale = XMVectorLerp(scale1, scale2, t);
    XMVECTOR blendedTrans = XMVectorLerp(trans1, trans2, t);
    XMVECTOR blendedRot = XMQuaternionSlerp(rot1, rot2, t);

    XMMATRIX blendedNodeTransform =
        XMMatrixScalingFromVector(blendedScale) *
        XMMatrixRotationQuaternion(blendedRot) *
        XMMatrixTranslationFromVector(blendedTrans);
    return blendedNodeTransform;
}

XMMATRIX AnimationJob::calculAni(NodeAnimation& nodeAnim, float time ,int* _key)
{
    float t = 0;
    // Translation
    XMVECTOR interpPos = nodeAnim.m_positionKeys[0].m_position;
    if (nodeAnim.m_positionKeys.size() > 1)
    {
        int posKeyIdx = CurrentKeyIndex<NodeAnimation::PositionKey>(nodeAnim.m_positionKeys, time);
        int nPosKeyIdx = posKeyIdx + 1;

        if (_key)
            *_key = posKeyIdx;
        NodeAnimation::PositionKey posKey = nodeAnim.m_positionKeys[posKeyIdx];
        NodeAnimation::PositionKey nPosKey = nodeAnim.m_positionKeys[nPosKeyIdx];

        t = (time - posKey.m_time) / (nPosKey.m_time - posKey.m_time);
        interpPos = XMVectorLerp(posKey.m_position, nPosKey.m_position, t);
    }
    XMMATRIX translation = XMMatrixTranslationFromVector(interpPos);

    // Rotation
    XMVECTOR interpQuat = nodeAnim.m_rotationKeys[0].m_rotation;
    if (nodeAnim.m_rotationKeys.size() > 1)
    {
        int rotKeyIdx = CurrentKeyIndex<NodeAnimation::RotationKey>(nodeAnim.m_rotationKeys, time);
        int nRotKeyIdx = rotKeyIdx + 1;

        NodeAnimation::RotationKey rotKey = nodeAnim.m_rotationKeys[rotKeyIdx];
        NodeAnimation::RotationKey nRotKey = nodeAnim.m_rotationKeys[nRotKeyIdx];

        t = (time - rotKey.m_time) / (nRotKey.m_time - rotKey.m_time);
        interpQuat = XMQuaternionSlerp(rotKey.m_rotation, nRotKey.m_rotation, t);

    }
    XMMATRIX rotation = XMMatrixRotationQuaternion(interpQuat);

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

    XMMATRIX scale = XMMatrixScaling(interpScale, interpScale, interpScale);
    XMMATRIX nodeTransform = scale * rotation * translation;
    return nodeTransform;
}
#endif // !DYNAMICCPP_EXPORTS
