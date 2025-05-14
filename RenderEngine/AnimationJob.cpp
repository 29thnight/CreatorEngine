#include "AnimationJob.h"
#include "RenderScene.h"
#include "Skeleton.h"
#include "SceneManager.h"
#include "RenderableComponents.h"
#include "Scene.h"
#include "Benchmark.hpp"
#include "AnimationController.h"
using namespace DirectX;

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

AnimationJob::AnimationJob() :
    m_UpdateThreadPool(8)
{
	m_sceneLoadedHandle = SceneManagers->sceneLoadedEvent.AddRaw(this, &AnimationJob::PrepareAnimation);
    m_AnimationUpdateHandle = SceneManagers->InternalAnimationUpdateEvent.AddRaw(this, &AnimationJob::Update);
	m_sceneUnloadedHandle = SceneManagers->sceneUnloadedEvent.AddRaw(this, &AnimationJob::CleanUp);
}

AnimationJob::~AnimationJob()
{
	SceneManagers->sceneLoadedEvent.Remove(m_sceneLoadedHandle);
	SceneManagers->InternalAnimationUpdateEvent.Remove(m_AnimationUpdateHandle);
}

void AnimationJob::Update(float deltaTime)
{
    Scene* scene = SceneManagers->GetActiveScene();
    uint32 currSize = scene->m_SceneObjects.size();

    if (currSize < m_objectSize)
    {
        m_currAnimator.clear();
        m_objectSize = 0;
    }

    if(m_objectSize != currSize)
    {
        if(0 == m_objectSize)
        {
            for (auto& sceneObj : scene->m_SceneObjects)
            {
                Animator* animator = sceneObj->GetComponent<Animator>();
                if (nullptr == animator || !animator->IsEnabled()) continue;

                m_currAnimator.push_back(animator);
            }
        }
        else
        {
            for (uint32 i = m_objectSize - 1; i < currSize; ++i)
            {
                Animator* animator = scene->m_SceneObjects[i]->GetComponent<Animator>();
                if (nullptr == animator || !animator->IsEnabled()) continue;

                m_currAnimator.push_back(animator);
            }
        }
        m_objectSize = currSize;
    }

    for(auto& animator : m_currAnimator)
    {
        m_UpdateThreadPool.Enqueue([&]
        {
            
            Skeleton* skeleton = animator->m_Skeleton;

            //컨트롤러별로 상,하체 등등이 분리되있다면
            if (animator->UsesMultipleControllers() == true)
            {
               
                for (auto& animationcontroller : animator->m_animationControllers)
                {
                    Animation& animation = skeleton->m_animations[animationcontroller->GetAnimationIndex()];
                    animationcontroller->m_timeElapsed += deltaTime * animation.m_ticksPerSecond;
                    //animationcontroller->m_timeElapsed = fmod(animationcontroller->m_timeElapsed, animation.m_duration); //&&&&&
                    if (animationcontroller->m_timeElapsed >= animation.m_duration)
                        animationcontroller->m_timeElapsed = animation.m_duration;
                    XMMATRIX rootTransform = skeleton->m_rootTransform;
                    if (animationcontroller->m_isBlend)
                    {
                        Animation& nextanimation = skeleton->m_animations[animationcontroller->GetNextAnimationIndex()];
                        animationcontroller->m_nextTimeElapsed += deltaTime * nextanimation.m_ticksPerSecond;
                        animationcontroller->m_nextTimeElapsed = fmod(animationcontroller->m_nextTimeElapsed, nextanimation.m_duration);
                        UpdateBlendBone(skeleton->m_rootBone, *animator, animationcontroller,rootTransform, (*animationcontroller).m_timeElapsed, (*animationcontroller).m_nextTimeElapsed);
                    }
                    else
                    {
                        UpdateBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animationcontroller).m_timeElapsed);  
                    }
                }
                XMMATRIX rootTransform = skeleton->m_rootTransform;

                UpdateBoneLayer(skeleton->m_rootBone, *animator , rootTransform);
               
            }
            else
            {
                Animation& animation = skeleton->m_animations[animator->m_AnimIndexChosen];
                animator->m_TimeElapsed += deltaTime * animation.m_ticksPerSecond;
                animator->m_TimeElapsed = fmod(animator->m_TimeElapsed, animation.m_duration);
                AnimationController* animationcontroller = nullptr;
                if(!animator->m_animationControllers.empty())
                    animationcontroller = animator->m_animationControllers[0];
                XMMATRIX rootTransform = skeleton->m_rootTransform;
                if (animator->m_isBlend)
                {
                    Animation& nextanimation = skeleton->m_animations[animator->nextAnimIndex];
                    animator->m_nextTimeElapsed += deltaTime * nextanimation.m_ticksPerSecond;
                    animator->m_nextTimeElapsed = fmod(animator->m_nextTimeElapsed, nextanimation.m_duration);
                    UpdateBlendBone(skeleton->m_rootBone, *animator, animationcontroller, rootTransform, (*animator).m_TimeElapsed, (*animator).m_nextTimeElapsed);
                }
                else
                {
                    UpdateBone(skeleton->m_rootBone, *animator, animationcontroller,rootTransform, (*animator).m_TimeElapsed);
                }
            }
            
            
        });
    }

    m_UpdateThreadPool.NotifyAllAndWait();
}

void AnimationJob::PrepareAnimation()
{
    m_currAnimator.clear();
    Scene* scene = SceneManagers->GetActiveScene();
	if (nullptr == scene) return;

    uint32 currSize = scene->m_SceneObjects.size();

    for (uint32 i = 0; i < currSize; ++i)
    {
        Animator* animator = scene->m_SceneObjects[i]->GetComponent<Animator>();
        if (nullptr == animator) continue;
		animator->SetEnabled(true);

        m_currAnimator.push_back(animator);
    }
	m_objectSize = currSize;
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
    XMMATRIX nodeTransform = calculAni(nodeAnim, time);
    XMMATRIX nextnodeTransform = calculAni(nextnodeAnim, nextanitime);
    XMMATRIX blendTransform = BlendAni(nodeTransform, nextnodeTransform, animator.blendT);
    animator.blendtransform = blendTransform;
    XMMATRIX globalTransform = blendTransform * parentTransform;

    
    animator.m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    bone->m_globalTransform = globalTransform;
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
    XMMATRIX nodeTransform = calculAni(nodeAnim, time);
    XMMATRIX globalTransform = nodeTransform * parentTransform;
    
    bone->m_globalTransform = globalTransform;
    animator.m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    
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
   
    for (auto& controller : animator.m_animationControllers)
    {
        auto mask = controller->GetAvatarMask();
        if (mask->IsBoneEnabled(bone->m_region) == true)
        {
            globalTransform = controller->m_LocalTransforms[bone->m_index] * parentTransform;
        }
    }
    animator.m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
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

XMMATRIX AnimationJob::calculAni(NodeAnimation& nodeAnim, float time)
{
    float t = 0;
    // Translation
    XMVECTOR interpPos = nodeAnim.m_positionKeys[0].m_position;
    if (nodeAnim.m_positionKeys.size() > 1)
    {
        int posKeyIdx = CurrentKeyIndex<NodeAnimation::PositionKey>(nodeAnim.m_positionKeys, time);
        int nPosKeyIdx = posKeyIdx + 1;

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

