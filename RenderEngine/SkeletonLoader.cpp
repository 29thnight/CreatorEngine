#include "SkeletonLoader.h"

SkeletonLoader::SkeletonLoader(const aiScene* scene) :
	m_scene(scene)
{
}

SkeletonLoader::~SkeletonLoader()
{
	for (Bone* bone : m_bones)
	{
		delete bone;
	}
	m_bones.clear();
	m_boneMap.clear();
}

Skeleton* SkeletonLoader::GenerateSkeleton(aiNode* root)
{
    Skeleton* skeleton = new Skeleton();
    aiNode* boneRoot = FindBoneRoot(root);

    // Parent is not a bone recorded
    Bone* parent = new Bone(std::string(boneRoot->mName.data), m_bones.size(), XMMatrixIdentity());
    m_bones.push_back(parent);

    skeleton->m_rootBone = parent;
    ProcessBones(boneRoot, parent);
    skeleton->m_bones = m_bones;
    skeleton->m_rootTransform = XMMATRIX(&boneRoot->mTransformation.a1);

    m_bones.clear();
    skeleton->m_globalInverseTransform = XMMatrixInverse(NULL, XMMATRIX(&boneRoot->mTransformation.a1));

    LoadAnimations(skeleton);
    return skeleton;
}

int SkeletonLoader::AddBone(aiBone* _bone)
{
    std::string boneName(_bone->mName.data);
    if (m_boneMap.find(boneName) == m_boneMap.end())
    {
        Bone* bone = new Bone(boneName, m_bones.size(), XMMatrixTranspose(XMMATRIX(&_bone->mOffsetMatrix.a1)));
        m_bones.push_back(bone);
        m_boneMap.emplace(boneName, bone);
    }
    return m_boneMap[boneName]->m_index;
}

void SkeletonLoader::ProcessBones(aiNode* node, Bone* bone)
{
    for (UINT i = 0; i < node->mNumChildren; ++i)
    {
        aiNode* child = node->mChildren[i];
        if (m_boneMap.find(child->mName.data) != m_boneMap.end())
        {
            Bone* childBone = m_boneMap[child->mName.data];
            bone->m_children.push_back(childBone);
            ProcessBones(child, childBone);
        }
    }
}

void SkeletonLoader::LoadAnimations(Skeleton* skeleton)
{
    for (UINT i = 0; i < m_scene->mNumAnimations; ++i)
    {
        std::optional<Animation> anim = m_animationLoader.LoadAnimation(m_scene->mAnimations[i]);

		if (anim.has_value())
        {
            skeleton->m_animations.push_back(anim.value());
        }
    }
}

aiNode* SkeletonLoader::FindBoneRoot(aiNode* root)
{
    std::queue<aiNode*> queue;
    queue.push(root);

    while (!queue.empty())
    {
        aiNode* node = queue.front();
        queue.pop();

        std::string nodeName(node->mName.data);
        if (m_boneMap.find(nodeName) != m_boneMap.end())
        {
            return node->mParent;
        }
        for (UINT i = 0; i < node->mNumChildren; ++i)
        {
            aiNode* childNode = node->mChildren[i];
            queue.push(childNode);
        }
    }
    return nullptr;
}
