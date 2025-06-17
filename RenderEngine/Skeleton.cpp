#include "Skeleton.h"
#include "ResourceAllocator.h"
#include "Socket.h"
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
		else if (bonename.find("neck") != std::string::npos)
		{
			MarkRegion(bone, BoneRegion::Neck);
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

void Skeleton::MakeSocket(const std::string_view& socketName, const std::string_view& boneName)
{

	Socket* newSocket = new Socket();
	newSocket->m_name = socketName;
	newSocket->m_boneName = boneName;
	m_sockets.push_back(newSocket);
}

Socket* Skeleton::FindSocket(const std::string_view& socketName)
{
	for (auto& socket : m_sockets)
	{
		if (socket->m_name == socketName)
			return socket;
	}

	return nullptr;
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
