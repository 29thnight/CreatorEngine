#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "KeyFrameEvent.h"
#include <mathematics/quaternion.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>

struct NodeAnimation
{
	std::string m_name{};

	struct PositionKey
	{
		// 모델 캐시는 기존 4-float 위치와 같은 16B를 유지한다. w=1은 Assimp
		// 로드 시 채우고, 평가 시 xyz만 translation으로 사용한다.
		math::vector4 m_position;
		double m_time;
	};
	std::vector<PositionKey> m_positionKeys;

	struct RotationKey
	{
		math::quaternion m_rotation;
		double m_time;
	};
	std::vector<RotationKey> m_rotationKeys;

	struct ScaleKey
	{
		math::vector3 m_scale;
		double m_time;
	};
	std::vector<ScaleKey> m_scaleKeys;
};

static_assert(sizeof(NodeAnimation::PositionKey::m_position) == sizeof(float) * 4);
static_assert(sizeof(NodeAnimation::RotationKey::m_rotation) == sizeof(float) * 4);
static_assert(sizeof(NodeAnimation::ScaleKey::m_scale) == sizeof(float) * 3);

class Animator;
class Animation
{
   public:
   static consteval auto reflect()
   {
       using Self = Animation;
       return meta::schema<Self>(
           meta::field<&Self::m_name>,
           meta::field<&Self::m_isLoop>,
           meta::field<&Self::m_keyFrameEvent>);
   }
public:
	Animation() = default;
	std::string m_name{};
	std::map<std::string, NodeAnimation> m_nodeAnimations;
	size_t m_totalKeyFrames = 0;
	float m_duration{};
	double m_ticksPerSecond{};
	bool m_isLoop = true;

	int preKey = 0;
	int curKey = 0; //&&&&& 실제 사용되는 값인지 확인필요

	// I5-D4e-2 — 이벤트 표면(InvokeEvent·CRUD)은 Animator 클립 오버라이드로
	// 이주했다(AnimationEventBridge.cpp — 공유 자산 재주입 오염 청산). 이 필드는
	// 씬 표기 형상(reflect)을 위해 남는다: reader 구세대 호환 + writer는
	// Animator::OnAfterSerialize가 오버라이드 값으로 교체한다. 자산 인스턴스의
	// 이 벡터는 이제 항상 비어 있다(임포터·캐시 어느 쪽도 채우지 않는다).
	std::vector<KeyFrameEvent> m_keyFrameEvent;
};



