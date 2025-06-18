#pragma once
#include "Core.Minimal.h"
#include "Skeleton.generated.h"
#include "Animation.h"
enum class BoneRegion
{
	Root,
	Spine,
	Neck,
	LeftArm,
	RightArm,
	LeftLeg,
	RightLeg,
};
AUTO_REGISTER_ENUM(BoneRegion)

class Bone;
class Animation;
class Socket;
class Skeleton
{
public:
   ReflectSkeleton
	[[Serializable]]
	Skeleton() = default;
	~Skeleton();

	Bone* m_rootBone{};
	[[Property]]
	std::vector<Animation> m_animations;
	std::vector<Bone*> m_bones;
	[[Property]]
	Mathf::xMatrix m_rootTransform;
	Mathf::xMatrix m_globalInverseTransform;


	std::vector<Socket*> m_sockets;
	static constexpr uint32 MAX_BONES{ 512 };

	bool HasSocket() { return !m_sockets.empty(); }
	void MakeSocket(const std::string_view& socketName, const std::string_view& objectName);
	Socket* FindSocket(const std::string_view& socketName);
	void DeleteSocket(const std::string_view& socketName);
	Bone* FindBone(const std::string_view& _name);

	void MarkRegionSkeleton();
	void MarkRegion(Bone* bone, BoneRegion region);
};

std::string ToLower(std::string boneName);

class Bone
{
public:
	std::string m_name{};
	int m_index{};
	int m_parentIndex{};
	Mathf::xMatrix m_globalTransform;
	Mathf::xMatrix m_localTransform;
	Mathf::xMatrix m_offset;
	std::vector<Bone*> m_children;
	BoneRegion m_region = BoneRegion::Root;
    Bone() = default;
	Bone(const std::string& _name, int _index, const Mathf::xMatrix& _offset) :
		m_name(_name),
		m_index(_index),
		m_offset(_offset),
		m_globalTransform(XMMatrixIdentity())
	{

	}
};


inline static XMMATRIX InitialMatrix[Skeleton::MAX_BONES]{};
