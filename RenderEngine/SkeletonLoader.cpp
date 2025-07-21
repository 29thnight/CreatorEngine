#include "SkeletonLoader.h"
#include "ResourceAllocator.h"

SkeletonLoader::SkeletonLoader(const aiScene* scene) :
    m_scene(scene)
{
}

SkeletonLoader::~SkeletonLoader()
{
    m_boneMap.clear();
}

Skeleton* SkeletonLoader::GenerateSkeleton(aiNode* root)
{
    Skeleton* skeleton = AllocateResource<Skeleton>();
    aiNode* boneRoot = FindBoneRoot(root);

    if (boneRoot == nullptr)
    {
        LoadAnimations(skeleton);
        return skeleton;
    }

    // Parent is not a bone recorded
    Bone* parent = AllocateResource<Bone>(std::string(boneRoot->mName.data), m_bones.size(), XMMatrixIdentity());
    m_bones.push_back(parent);

    skeleton->m_rootBone = parent;
    ProcessBones(boneRoot, parent);
    skeleton->m_bones = std::move(m_bones);
    skeleton->m_rootTransform = XMMatrixTranspose(XMMATRIX(&boneRoot->mTransformation.a1));

    skeleton->m_globalInverseTransform = XMMatrixInverse(NULL, XMMatrixTranspose(XMMATRIX(&boneRoot->mTransformation.a1)));

    LoadAnimations(skeleton);
    return skeleton;
}

int SkeletonLoader::AddBone(aiBone* _bone)
{
    std::string boneName(_bone->mName.data);
    if (m_boneMap.find(boneName) == m_boneMap.end())
    {
        Bone* bone = AllocateResource<Bone>(boneName, m_bones.size(), XMMatrixTranspose(XMMATRIX(&_bone->mOffsetMatrix.a1)));
        m_bones.push_back(bone);
        m_boneMap.emplace(boneName, bone);
    }
    return m_boneMap[boneName]->m_index;
}

void SkeletonLoader::ProcessBones(aiNode* node, Bone* parentBone)
{
    for (UINT i = 0; i < node->mNumChildren; ++i)
    {
        aiNode* childNode = node->mChildren[i];
        Bone* nextParentBone = parentBone; // Keep the same parent by default

        // Check if the child node is a registered bone
        auto it = m_boneMap.find(childNode->mName.data);
        if (it != m_boneMap.end())
        {
            // If it is a bone, add it as a child to the current parent
            // and set it as the new parent for subsequent recursive calls.
            Bone* childBone = it->second;
            parentBone->m_children.push_back(childBone);
            nextParentBone = childBone;
        }

        // Continue traversal down the hierarchy, regardless of whether the child
        // was a bone or not.
        ProcessBones(childNode, nextParentBone);
    }
}

void SkeletonLoader::LoadAnimations(Skeleton* skeleton)
{
    skeleton->m_animations.reserve(m_scene->mNumAnimations);

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
    if (!root)
    {
        return nullptr;
    }

    std::queue<aiNode*> queue;
    queue.push(root);

    while (!queue.empty())
    {
        // Process one level at a time
        int levelSize = queue.size();
        std::vector<aiNode*> bonesOnThisLevel;

        for (int i = 0; i < levelSize; ++i)
        {
            aiNode* node = queue.front();
            queue.pop();

            // Check if the current node is a bone
            if (m_boneMap.count(node->mName.data))
            {
                bonesOnThisLevel.push_back(node);
            }

            // Enqueue children for the next level
            for (UINT j = 0; j < node->mNumChildren; ++j)
            {
                queue.push(node->mChildren[j]);
            }
        }

        // If we found any bones on this level, this is the highest level
        // with bones. The parent of the first one is our root.
        if (!bonesOnThisLevel.empty())
        {
            return bonesOnThisLevel[0]->mParent;
        }
    }

    // No bones were found in the entire hierarchy
    return nullptr;
}
