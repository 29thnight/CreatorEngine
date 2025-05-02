#include "Skeleton.h"
#include "ResourceAllocator.h"

Skeleton::~Skeleton()
{
	for (Bone* bone : m_bones)
	{
        DeallocateResource(bone);
	}

	m_bones.clear();
}


void Skeleton::MarkRegionSkeleton()
{

	for (auto& bone : m_bones)
	{
		std::string bonename = ToLower(bone->m_name);
		if (bonename.find("spine") != std::string::npos)
		{
			MarkRegion(bone, BoneRegion::Spine);
		}
		else if (bonename.find("shoulder") != std::string::npos)
		{
			if (bonename.find("left") != std::string::npos)
			{
				MarkRegion(bone, BoneRegion::LeftArm);
			}
			else if (bonename.find("right") != std::string::npos)
			{
				MarkRegion(bone, BoneRegion::RightArm);
			}
		}
		else if (bonename.find("leg") != std::string::npos)
		{
			if (bonename.find("left") != std::string::npos)
			{
				MarkRegion(bone, BoneRegion::LeftLeg);
			}
			else if (bonename.find("right") != std::string::npos)
			{
				MarkRegion(bone, BoneRegion::RightLeg);
			}
		}

	}

}

void Skeleton::MarkRegion(Bone* bone, BoneRegion region)
{
	bone->m_region = region;
	for (Bone* child : bone->m_children)
	{
		MarkRegion(child, region);
	}
}

std::string ToLower(std::string boneName)
{
	std::string name = boneName;

	std::transform(name.begin(), name.end(), name.begin(),
		[](unsigned char c) { return std::tolower(c); });

	return name;

}

Bone* Skeleton::FindBone(const std::string_view& _name)
{
	for (Bone* bone : m_bones)
	{
		if (bone->m_name == _name)
			return bone;
	}
	return nullptr;
}
