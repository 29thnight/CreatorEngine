#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "MetaPolymorphic.h"
#include "Core.Minimal.h"
#include "Animation.h"
#include <mathematics/matrix4x4.hpp>
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
	math::matrix4x4 m_rootTransform{};
	math::matrix4x4 m_globalInverseTransform{};


	static constexpr uint32 MAX_BONES{ 512 };

	// ★ E7-b — 인스턴스마다 유일한 일련번호. BoneComponent가 캐시한 뼈 인덱스가
	// "어느 스켈레톤에 대해 푼 값인가"를 이 값으로 판별한다.
	//
	// 포인터(Skeleton*)만 비교하면 ABA에 뚫린다: Model.cpp의 delete m_Skeleton으로
	// 스켈레톤이 해제된 뒤 ModelLoader의 new Skeleton()이 같은 주소를 다시 받으면,
	// 캐시가 우연히 적중해 **다른 모델의 뼈 인덱스를 그대로 재사용한다**. 범위
	// 검사는 MAX_BONES 고정 배열만 보므로 이 경우를 못 거른다 — 크래시가 아니라
	// 엉뚱한 뼈가 조용히 도는 형태로 나타난다(옛 코드는 매 프레임 이름으로 다시
	// 풀어서 이 문제에서 자유로웠다). 생성마다 증가하는 값이면 주소 재사용이
	// 캐시를 속이지 못한다. 0은 "아직 아무 스켈레톤도 아님"이라 쓰지 않는다.
	uint64 m_serial{ NextSerial() };
	static uint64 NextSerial();

	// ★ 소켓 보관소(m_sockets)와 그 API 넷이 여기 있었다.
	//
	//   소켓은 뼈에 GameObject를 매다는 게임플레이 개념이다 — Socket이 드는
	//   것이 Entity* 목록·Transform·SceneManagers 이벤트다. 렌더 쪽
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
	math::matrix4x4 m_offset{};
	std::vector<Bone*> m_children;
	BoneRegion m_region = BoneRegion::Root;
    Bone() = default;
	Bone(const std::string& _name, int _index, const math::matrix4x4& _offset) :
		m_name(_name),
		m_index(_index),
		m_offset(_offset)
	{

	}
};
