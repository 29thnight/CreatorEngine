#include "AnimationJob.h"
#include "RenderScene.h"
#include "Skeleton.h"
#include "Renderer.h"
#include "Scene.h"
#include "Banchmark.hpp"

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
        if (duration < keys[i + 1].m_time)
        {
            return i;
        }
    }
    return -1;
}

AnimationJob::AnimationJob() :
    m_UpdateThreadPool(8)
{
}

AnimationJob::~AnimationJob()
{
}

void AnimationJob::Update(RenderScene& scene, float deltaTime)
{
    uint32 currSize = scene.GetScene()->m_SceneObjects.size();
    if(m_objectSize != currSize)
    {
        if(0 == m_objectSize)
        {
            for (auto& sceneObj : scene.GetScene()->m_SceneObjects)
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
                Animator* animator = scene.GetScene()->m_SceneObjects[i]->GetComponent<Animator>();
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
            Animation& animation = skeleton->m_animations[animator->m_AnimIndexChosen];

            animator->m_TimeElapsed += deltaTime * animation.m_ticksPerSecond;
            animator->m_TimeElapsed = fmod(animator->m_TimeElapsed, animation.m_duration);
            XMMATRIX rootTransform = skeleton->m_rootTransform;
            UpdateBone(skeleton->m_rootBone, *animator, rootTransform, (*animator).m_TimeElapsed);
        });
    }

    m_UpdateThreadPool.NotifyAllAndWait();
}

void AnimationJob::UpdateBone(Bone* bone, Animator& animator, const XMMATRIX& parentTransform, float time)
{
    Skeleton* skeleton = animator.m_Skeleton;
    Animation& animation = skeleton->m_animations[animator.m_AnimIndexChosen];
    std::string& boneName = bone->m_name;
	auto it = animation.m_nodeAnimations.find(boneName);
	if (it == animation.m_nodeAnimations.end())
	{
		for (Bone* child : bone->m_children)
		{
			UpdateBone(child, animator, parentTransform, time);
		}
		return;
	}

	NodeAnimation& nodeAnim = animation.m_nodeAnimations[boneName];
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
    XMMATRIX globalTransform = nodeTransform * parentTransform;

    animator.m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
    bone->m_globalTransform = globalTransform;

    for (Bone* child : bone->m_children)
    {
        UpdateBone(child, animator, globalTransform, time);
    }
}

