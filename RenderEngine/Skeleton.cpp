#include "Skeleton.h"

Skeleton::~Skeleton()
{
	for (Bone* bone : m_bones)
	{
		delete bone;
	}

	m_bones.clear();
}
