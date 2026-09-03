#pragma once
// PHASE 3.75 MBC9 — legacy Skeleton.h(Assimp 산물·역브리지 Bone* 트리)가 은퇴하며
// 살아남은 조각. AvatarMask humanoid 레이어 판정과 Animator의 본 리전 파생이 쓴다.
#include <cstdint>
#include <string>

// 애니메이터 팔레트의 고정 상한(legacy Skeleton::MAX_BONES 승계).
inline constexpr std::uint32_t MAX_BONES{ 512 };

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

std::string ToLower(std::string boneName);
