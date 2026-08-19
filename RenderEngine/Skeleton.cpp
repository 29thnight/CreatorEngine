#include "Skeleton.h"
#include <atomic> // NextSerial의 카운터 — 전이 include에 기대지 않는다.
// ScriptBinder/Socket.h include가 여기 있었다. RenderEngine이 게임플레이
// 헤더를 여는 마지막 자리 중 하나였다 — 사연은 Skeleton.h에 적었다.

uint64 Skeleton::NextSerial()
{
	// 1부터 시작한다 — BoneComponent::m_resolvedSerial의 초기값 0이 "아직 못
	// 풀었음"을 뜻하므로 실제 스켈레톤과 절대 겹치면 안 된다.
	static std::atomic<uint64> counter{ 1 };
	return counter.fetch_add(1, std::memory_order_relaxed);
}

Skeleton::~Skeleton()
{
	for (Bone* bone : m_bones)
	{
        delete bone;
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

Bone* Skeleton::FindBone(std::string_view _name)
{
	for (Bone* bone : m_bones)
	{
		if (bone->m_name == _name)
			return bone;
	}

	/*auto it = m_boneMap.find(std::string(_name));
	if (it != m_boneMap.end())
	{
		return it->second;
	}*/

	return nullptr;
}
