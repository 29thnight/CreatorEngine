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

Bone* Skeleton::FindBone(const std::string_view& _name)
{
	for (Bone* bone : m_bones)
	{
		if (bone->m_name == _name)
			return bone;
	}
	return nullptr;
}
