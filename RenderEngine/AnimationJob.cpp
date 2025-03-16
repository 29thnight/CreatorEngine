#include "AnimationJob.h"
#include "Scene.h"
#include "Skeleton.h"
#include "ObjectRenderers.h"
#include "../Utility_Framework/ImGuiLogger.h"

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
 //   Skeleton* skeleton = animator.m_Skeleton;
 //   Animation& animation = skeleton->m_animations[animator.m_AnimIndexChosen];
 //   std::string& boneName = bone->m_name;

	//XMMATRIX transform = XMMatrixIdentity();

	//// 애니메이션 데이터가 없으면 기본값(identity) 사용
	//if (animation.m_nodeAnimations.find(boneName) != animation.m_nodeAnimations.end())
	//{
	//	NodeAnimation& nodeAnim = animation.m_nodeAnimations[boneName];

	//	if (nodeAnim.m_scaleKeys.size() > 0)
	//	{
	//		XMMATRIX scale = InterpolateScale(nodeAnim, time);
	//		transform *= scale;
	//	}
	//	if (nodeAnim.m_rotationKeys.size() > 0)
	//	{
	//		XMMATRIX rot = InterpolateRotation(nodeAnim, time);
	//		transform *= rot;
	//	}
	//	if (nodeAnim.m_positionKeys.size() > 0)
	//	{
	//		XMMATRIX pos = InterpolatePosition(nodeAnim, time);
	//		transform *= pos;
	//	}
	//}

	//XMMATRIX globalTransform = transform * parentTransform;
	//animator.m_FinalTransforms[bone->m_index] = bone->m_offset * globalTransform * skeleton->m_globalInverseTransform;
	//bone->m_globalTransform = globalTransform;

	//if ("mixamorig:RightToe_End" == bone->m_name)
	//{
	//	LoggerSystem->AddMatrixLog(animator.m_FinalTransforms[bone->m_index], std::string(bone->m_name + "Final").c_str());
	//}

	//for (auto child : bone->m_children)
	//{
	//	UpdateBone(child, animator, globalTransform, time);
	//}

    Skeleton* skeleton = animator.m_Skeleton;
    Animation& animation = skeleton->m_animations[animator.m_AnimIndexChosen];
    std::string& boneName = bone->m_name;
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

XMMATRIX AnimationJob::InterpolatePosition(NodeAnimation& nodeAnim, float time)
{
    if(nodeAnim.m_positionKeys.size() == 0)
	{
		return XMMatrixIdentity();
	}

	if (nodeAnim.m_positionKeys.size() == 1)
	{
		return XMMatrixTranslationFromVector(nodeAnim.m_positionKeys[0].m_position);
	}

	int posKeyIdx = CurrentKeyIndex<NodeAnimation::PositionKey>(nodeAnim.m_positionKeys, time);
	int nPosKeyIdx = posKeyIdx + 1;

	NodeAnimation::PositionKey posKey = nodeAnim.m_positionKeys[posKeyIdx];
	NodeAnimation::PositionKey nPosKey = nodeAnim.m_positionKeys[nPosKeyIdx];

	float t = (time - posKey.m_time) / (nPosKey.m_time - posKey.m_time);
	XMVECTOR interpPos = XMVectorLerp(posKey.m_position, nPosKey.m_position, t);

	return XMMatrixTranslationFromVector(interpPos);
}

XMMATRIX AnimationJob::InterpolateRotation(NodeAnimation& nodeAnim, float time)
{
	if (nodeAnim.m_rotationKeys.size() == 0)
	{
		return XMMatrixIdentity();
	}

	if (nodeAnim.m_rotationKeys.size() == 1)
	{
		return XMMatrixRotationQuaternion(nodeAnim.m_rotationKeys[0].m_rotation);
	}

	int rotKeyIdx = CurrentKeyIndex<NodeAnimation::RotationKey>(nodeAnim.m_rotationKeys, time);
	int nRotKeyIdx = rotKeyIdx + 1;

	NodeAnimation::RotationKey rotKey = nodeAnim.m_rotationKeys[rotKeyIdx];
	NodeAnimation::RotationKey nRotKey = nodeAnim.m_rotationKeys[nRotKeyIdx];

	float t = (time - rotKey.m_time) / (nRotKey.m_time - rotKey.m_time);
	XMVECTOR interpQuat = XMQuaternionSlerp(rotKey.m_rotation, nRotKey.m_rotation, t);

	return XMMatrixRotationQuaternion(interpQuat);
}

XMMATRIX AnimationJob::InterpolateScale(NodeAnimation& nodeAnim, float time)
{
	if (nodeAnim.m_scaleKeys.size() == 0)
	{
		return XMMatrixIdentity();
	}

	if (nodeAnim.m_scaleKeys.size() == 1)
	{
		return XMMatrixScaling(nodeAnim.m_scaleKeys[0].m_scale.x, nodeAnim.m_scaleKeys[0].m_scale.y, nodeAnim.m_scaleKeys[0].m_scale.z);
	}

	int scalKeyIdx = CurrentKeyIndex<NodeAnimation::ScaleKey>(nodeAnim.m_scaleKeys, time);
	int nScalKeyIdx = scalKeyIdx + 1;

	NodeAnimation::ScaleKey scalKey = nodeAnim.m_scaleKeys[scalKeyIdx];
	NodeAnimation::ScaleKey nScalKey = nodeAnim.m_scaleKeys[nScalKeyIdx];

	float t = (time - scalKey.m_time) / (nScalKey.m_time - scalKey.m_time);
	float interpScale = lerp(scalKey.m_scale.x, nScalKey.m_scale.x, t);

	return XMMatrixScaling(interpScale, interpScale, interpScale);
}

