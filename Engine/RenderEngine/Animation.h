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
	void InvokeEvent();
	void InvokeEvent(Animator* _ownerAnimator,float _curAnimatonProgress, float _preAnimationProgress);

	void AddEvent();
	void AddEvent(KeyFrameEvent _event);
	void DeleteEvent(KeyFrameEvent _event);
	void DeleteEvent(int _index);
	KeyFrameEvent* FindEvent(KeyFrameEvent _event);
	KeyFrameEvent* FindEvent(const std::string& _eventName, const std::string& _scriptName, const std::string& _funName, float progressPercent);
	bool FindEventName(std::string Name);
	void SetEvent(const std::string& _eventName,const std::string& _scriptName, const std::string& _funName, float progressPercent);

	std::vector<KeyFrameEvent> m_keyFrameEvent;
};



