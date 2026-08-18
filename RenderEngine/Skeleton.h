#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "MetaPolymorphic.h"
#include "Core.Minimal.h"
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

class Bone;
class Animation;
class Skeleton : public meta::polymorphic
{
   public:
   static consteval auto reflect()
   {
       using Self = Skeleton;
       return meta::schema<Self>(
           meta::field<&Self::m_animations>,
           meta::field<&Self::m_rootTransform>);
   }
public:
	Skeleton() = default;
	~Skeleton();

	Bone* m_rootBone{};
	std::vector<Animation> m_animations;
	std::vector<Bone*> m_bones;
	std::unordered_map<std::string, Bone*> m_boneMap;
	Mathf::xMatrix m_rootTransform;
	Mathf::xMatrix m_globalInverseTransform;


	static constexpr uint32 MAX_BONES{ 512 };

	// ★ 소켓 보관소(m_sockets)와 그 API 넷이 여기 있었다.
	//
	//   소켓은 뼈에 GameObject를 매다는 게임플레이 개념이다 — Socket이 드는
	//   것이 GameObject* 목록·Transform·SceneManagers 이벤트다. 렌더 쪽
	//   Skeleton이 그것을 소유하고 파괴하는 것은 방향이 거꾸로였다.
	//
	//   그런데 이전은 이미 끝나 있었다. Skeleton::MakeSocket은 본문이 통째로
	//   주석이었고 그 주석에 "animator로 옮김"이라 적혀 있다. 즉 m_sockets에
	//   넣는 코드가 없어 늘 비어 있었고(직렬화 대상도 아니다), HasSocket은
	//   항상 false, FindSocket은 항상 nullptr였다. 남은 것은 껍데기뿐이다.
	//
	//   살아 있는 보관소는 Animator::socketvec이다. 게임 스크립트도
	//   m_animator->MakeSocket만 부른다.
	Bone* FindBone(std::string_view _name);

	void MarkRegionSkeleton();
	void MarkRegion(Bone* bone, BoneRegion region);
};

std::string ToLower(std::string boneName);

class Bone : public meta::polymorphic
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
