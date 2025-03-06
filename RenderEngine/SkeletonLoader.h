#pragma once
#include "Core.Minimal.h"
#include "AnimationLoader.h"
#include "Skeleton.h"

class SkeletonLoader
{
public:
	std::map<std::string, Bone*> m_boneMap;
	std::vector<Bone*> m_bones;

	SkeletonLoader(const aiScene* scene);
	~SkeletonLoader();
	Skeleton* GenerateSkeleton(aiNode* root);
	int AddBone(aiBone* bone);
	void ProcessBones(aiNode* node, Bone* _bone);
	void LoadAnimations(Skeleton* skeleton);

private:
	aiNode* FindBoneRoot(aiNode* root);
	AnimationLoader m_animationLoader;
	const aiScene* m_scene;
	int m_boneReached{ 0 };
};