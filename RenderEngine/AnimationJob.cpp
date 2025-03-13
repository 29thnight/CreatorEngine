#include "AnimationJob.h"
#include "Scene.h"
#include "Skeleton.h"
#include "ObjectRenderers.h"

using namespace DirectX;

inline float lerp(float a, float b, float f)
{
    return a + f * (b - a);
}

template <typename T>
int CurrentKeyIndex(std::vector<T> keys, double time)
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

void AnimationJob::Update(Scene& scene, float deltaTime)
{
    for (auto sceneObj : scene.m_SceneObjects)
    {
        if (!sceneObj->m_animator.m_IsEnabled) continue;

        Animator& animator = sceneObj->m_animator;
        Skeleton* skeleton = animator.m_Skeleton;
        Animation& animation = skeleton->m_animations[animator.m_AnimIndexChosen];

        animator.m_TimeElapsed += deltaTime * animation.m_ticksPerSecond;
        animator.m_TimeElapsed = fmod(animator.m_TimeElapsed, animation.m_duration);
		XMMATRIX sceneObjMatrix = sceneObj->m_transform.GetWorldMatrix();

		XMMATRIX rootTransform = sceneObjMatrix * skeleton->m_rootTransform;
        UpdateBone(skeleton->m_rootBone, animator, rootTransform, animator.m_TimeElapsed);
    }
}

void AnimationJob::UpdateBone(Bone* bone, Animator& animator, const XMMATRIX& parentTransform, float time)
{
    Skeleton* skeleton = animator.m_Skeleton;
    Animation& animation = skeleton->m_animations[animator.m_AnimIndexChosen];
    std::string& boneName = bone->m_name;
    NodeAnimation& nodeAnim = animation.m_nodeAnimations[boneName];
    float t = 0;

	XMVECTOR interpPos{ 0, 0, 0, 1 };
    if (nodeAnim.m_positionKeys.size() > 1)
    {
        interpPos = nodeAnim.m_positionKeys[0].m_position;
        int posKeyIdx = CurrentKeyIndex<NodeAnimation::PositionKey>(nodeAnim.m_positionKeys, time);
        int nPosKeyIdx = posKeyIdx + 1;

        NodeAnimation::PositionKey posKey = nodeAnim.m_positionKeys[posKeyIdx];
        NodeAnimation::PositionKey nPosKey = nodeAnim.m_positionKeys[nPosKeyIdx];

        t = (time - posKey.m_time) / (nPosKey.m_time - posKey.m_time);
        interpPos = XMVectorLerp(posKey.m_position, nPosKey.m_position, t);
    }
    XMMATRIX translation = XMMatrixTranslationFromVector(interpPos);

    // Rotation
    XMVECTOR interpQuat{};
    if (nodeAnim.m_rotationKeys.size() > 1)
    {
		interpQuat = nodeAnim.m_rotationKeys[0].m_rotation;
        int rotKeyIdx = CurrentKeyIndex<NodeAnimation::RotationKey>(nodeAnim.m_rotationKeys, time);
        int nRotKeyIdx = rotKeyIdx + 1;

        NodeAnimation::RotationKey rotKey = nodeAnim.m_rotationKeys[rotKeyIdx];
        NodeAnimation::RotationKey nRotKey = nodeAnim.m_rotationKeys[nRotKeyIdx];

        t = (time - rotKey.m_time) / (nRotKey.m_time - rotKey.m_time);
        interpQuat = XMQuaternionSlerp(rotKey.m_rotation, nRotKey.m_rotation, t);

    }
    XMMATRIX rotation = XMMatrixRotationQuaternion(interpQuat);

    // Scaling
    float interpScale{ 1.f };
    if (nodeAnim.m_scaleKeys.size() > 1)
    {
		interpScale = nodeAnim.m_scaleKeys[0].m_scale.x;
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

