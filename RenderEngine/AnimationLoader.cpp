#include "AnimationLoader.h"

Animation AnimationLoader::LoadAnimation(aiAnimation* _pAnimation)
{
    Animation animation;
    animation.m_name = _pAnimation->mName.data;
    animation.m_duration = _pAnimation->mDuration;
    animation.m_ticksPerSecond = _pAnimation->mTicksPerSecond;

    for (uint32 i = 0; i < _pAnimation->mNumChannels; ++i)
    {
        aiNodeAnim* ainodeAnim = _pAnimation->mChannels[i];
        NodeAnimation nodeAnim = LoadNodeAnimation(ainodeAnim);
        animation.m_nodeAnimations.emplace(ainodeAnim->mNodeName.data, nodeAnim);
    }

    return animation;
}

NodeAnimation AnimationLoader::LoadNodeAnimation(aiNodeAnim* _pNodeAnim)
{
    NodeAnimation nodeAnim;
    nodeAnim.m_name = _pNodeAnim->mNodeName.data;

    for (uint32 i = 0; i < _pNodeAnim->mNumPositionKeys; ++i)
    {
        aiVectorKey& aiPosKey = _pNodeAnim->mPositionKeys[i];
        NodeAnimation::PositionKey posKey;
        posKey.m_position = XMVectorSet(aiPosKey.mValue.x, aiPosKey.mValue.y, aiPosKey.mValue.z, 1);
        posKey.m_time = aiPosKey.mTime;
        nodeAnim.m_positionKeys.push_back(posKey);
    }

    for (uint32 i = 0; i < _pNodeAnim->mNumRotationKeys; ++i)
    {
        aiQuatKey& aiRotKey = _pNodeAnim->mRotationKeys[i];
        NodeAnimation::RotationKey rotKey;
        aiQuaternion& aiQuat = aiRotKey.mValue;
        rotKey.m_rotation = XMVectorSet(aiQuat.x, aiQuat.y, aiQuat.z, aiQuat.w);
        rotKey.m_time = aiRotKey.mTime;
        nodeAnim.m_rotationKeys.push_back(rotKey);
    }

    for (uint32 i = 0; i < _pNodeAnim->mNumScalingKeys; ++i)
    {
        aiVectorKey& aiScalKeys = _pNodeAnim->mScalingKeys[i];
        NodeAnimation::ScaleKey scalKey;
        scalKey.m_scale.x = aiScalKeys.mValue.x;
        scalKey.m_scale.y = aiScalKeys.mValue.y;
        scalKey.m_scale.z = aiScalKeys.mValue.z;
        scalKey.m_time = aiScalKeys.mTime;
        nodeAnim.m_scaleKeys.push_back(scalKey);
    }

    return nodeAnim;
}
