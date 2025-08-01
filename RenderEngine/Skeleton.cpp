#include "Skeleton.h"
#include "ResourceAllocator.h"
#include "Socket.h"
#include "Scene.h"
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

void Skeleton::MakeSocket(const std::string_view& socketName)
{
	//animator·Î¿Å±è
	/*Socket* socket = FindSocket(socketName);
	if (socket) return;
	GameObject* obj = GameObject::Find(objectName);
	SceneManagers->GetActiveScene()->CreateGameObject(socketName, GameObjectType::Empty, obj->m_index);

	Socket* socket = FindSocket(socketName);
	if (socket) return;
	Socket* newSocket = new Socket();
	newSocket->m_name = socketName;
	newSocket->GameObjectIndex = obj->m_index;
	newSocket->m_ObjectName = objectName;
	m_sockets.push_back(newSocket);*/
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

void Skeleton::DeleteSocket(const std::string_view& socketName)
{
	auto it = std::find_if(m_sockets.begin(), m_sockets.end(),
		[&](const Socket* socket) 
		{
			return socket->m_name == socketName;
		});
	m_sockets.erase(it, m_sockets.end());
}

Bone* Skeleton::FindBone(const std::string_view& _name)
{
	//for (Bone* bone : m_bones)
	//{
	//	if (bone->m_name == _name)
	//		return bone;
	//}

	auto it = m_boneMap.find(std::string(_name));
	if (it != m_boneMap.end())
	{
		return it->second;
	}

	return nullptr;
}
