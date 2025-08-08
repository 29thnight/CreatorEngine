#include "AnimationLoader.h"

static size_t CountUniqueKeyTimes(const aiAnimation* anim, double eps = 1e-6)
{
    std::vector<double> times;
    times.reserve(1024);

    // 노드(본) 채널들
    for (unsigned c = 0; c < anim->mNumChannels; ++c) {
        const aiNodeAnim* ch = anim->mChannels[c];
        for (unsigned i = 0; i < ch->mNumPositionKeys; ++i) times.push_back(ch->mPositionKeys[i].mTime);
        for (unsigned i = 0; i < ch->mNumRotationKeys; ++i) times.push_back(ch->mRotationKeys[i].mTime);
        for (unsigned i = 0; i < ch->mNumScalingKeys; ++i) times.push_back(ch->mScalingKeys[i].mTime);
    }
    // 메쉬 애니
    for (unsigned c = 0; c < anim->mNumMeshChannels; ++c) {
        const aiMeshAnim* ch = anim->mMeshChannels[c];
        for (unsigned i = 0; i < ch->mNumKeys; ++i) times.push_back(ch->mKeys[i].mTime);
    }
    // 모프 애니
    for (unsigned c = 0; c < anim->mNumMorphMeshChannels; ++c) {
        const aiMeshMorphAnim* ch = anim->mMorphMeshChannels[c];
        for (unsigned i = 0; i < ch->mNumKeys; ++i) times.push_back(ch->mKeys[i].mTime);
    }

    if (times.empty()) return 0;

    std::sort(times.begin(), times.end());
    // epsilon 기반 unique
    size_t uniqueCount = 1;
    double prev = times[0];
    for (size_t i = 1; i < times.size(); ++i) {
        if (std::abs(times[i] - prev) > eps) {
            ++uniqueCount;
            prev = times[i];
        }
    }
    return uniqueCount;
}

std::optional<Animation> AnimationLoader::LoadAnimation(aiAnimation* _pAnimation)
{
    if (_pAnimation->mName.length <= 0 || strstr(_pAnimation->mName.C_Str(), "ik") != nullptr)
    {
        return std::nullopt;
    }

    Animation animation;
    animation.m_name = _pAnimation->mName.data;
    animation.m_duration = _pAnimation->mDuration;
    animation.m_ticksPerSecond = _pAnimation->mTicksPerSecond;
    animation.m_totalKeyFrames = CountUniqueKeyTimes(_pAnimation, 1e-6);

    for (uint32 i = 0; i < _pAnimation->mNumChannels; ++i)
    {
        aiNodeAnim* ainodeAnim = _pAnimation->mChannels[i];
        std::optional<NodeAnimation> nodeAnim = LoadNodeAnimation(ainodeAnim);

		if (nodeAnim.has_value())
        {
            animation.m_nodeAnimations.emplace(ainodeAnim->mNodeName.data, nodeAnim.value());
        }
        else
        {
			return std::nullopt;
        }
    }

    return animation;
}

std::optional<NodeAnimation> AnimationLoader::LoadNodeAnimation(aiNodeAnim* _pNodeAnim)
{
	if (_pNodeAnim->mNodeName.length <= 0
		|| _pNodeAnim->mNumPositionKeys <= 0
		|| _pNodeAnim->mNumRotationKeys <= 0
		|| _pNodeAnim->mNumScalingKeys <= 0
    )
	{
		return std::nullopt;
	}

    NodeAnimation nodeAnim;
    nodeAnim.m_name = _pNodeAnim->mNodeName.data;

    nodeAnim.m_positionKeys.reserve(_pNodeAnim->mNumPositionKeys);
    for (uint32 i = 0; i < _pNodeAnim->mNumPositionKeys; ++i)
    {
        aiVectorKey& aiPosKey = _pNodeAnim->mPositionKeys[i];
        NodeAnimation::PositionKey posKey{};
        posKey.m_position = XMVectorSet(aiPosKey.mValue.x, aiPosKey.mValue.y, aiPosKey.mValue.z, 1);
        posKey.m_time = aiPosKey.mTime;
        nodeAnim.m_positionKeys.push_back(posKey);
    }

    nodeAnim.m_rotationKeys.reserve(_pNodeAnim->mNumRotationKeys);
    for (uint32 i = 0; i < _pNodeAnim->mNumRotationKeys; ++i)
    {
        aiQuatKey& aiRotKey = _pNodeAnim->mRotationKeys[i];
        NodeAnimation::RotationKey rotKey{};
        aiQuaternion& aiQuat = aiRotKey.mValue;
        rotKey.m_rotation = XMVectorSet(aiQuat.x, aiQuat.y, aiQuat.z, aiQuat.w);
        rotKey.m_time = aiRotKey.mTime;
        nodeAnim.m_rotationKeys.push_back(rotKey);
    }

    nodeAnim.m_scaleKeys.reserve(_pNodeAnim->mNumScalingKeys);
    for (uint32 i = 0; i < _pNodeAnim->mNumScalingKeys; ++i)
    {
        aiVectorKey& aiScalKeys = _pNodeAnim->mScalingKeys[i];
        NodeAnimation::ScaleKey scalKey{};
        scalKey.m_scale.x = aiScalKeys.mValue.x;
        scalKey.m_scale.y = aiScalKeys.mValue.y;
        scalKey.m_scale.z = aiScalKeys.mValue.z;
        scalKey.m_time = aiScalKeys.mTime;
        nodeAnim.m_scaleKeys.push_back(scalKey);
    }

    return nodeAnim;
}
