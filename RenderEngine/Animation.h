#pragma once
#include "Core.Minimal.h"
#include "Animation.generated.h"
struct NodeAnimation
{
	std::string m_name{};

	struct PositionKey
	{
		Mathf::xVector m_position;
		double m_time;
	};
	std::vector<PositionKey> m_positionKeys;

	struct RotationKey
	{
		Mathf::xVector m_rotation;
		double m_time;
	};
	std::vector<RotationKey> m_rotationKeys;

	struct ScaleKey
	{
		Mathf::Vector3 m_scale;
		double m_time;
	};
	std::vector<ScaleKey> m_scaleKeys;
};


class KeyFrameEvent
{
public:
	std::string m_eventName;
	std::string m_scriptName;
	std::string m_funName;
	float key = 0;
	std::function<void()> m_event;


};


class Animator;
class Animation
{
public:
   ReflectAnimation
	[[Serializable]]
	Animation() = default;
	[[Property]]
	std::string m_name{};
	std::map<std::string, NodeAnimation> m_nodeAnimations;
	float m_duration{};
	double m_ticksPerSecond{};
	[[Property]]
	bool m_isLoop = true;

	float preAnimationProgress =0;
	float curAnimationProgress =0;
	int preKey = 0;
	int curKey = 0;
	void InvokeEvent();
	void InvokeEvent(Animator* _ownerAnimator);
	void SetEvent(const std::string& _funName, float progressPercent, std::function<void()> _func);
	void SetEvent(const std::string& _scriptName, const std::string& _funName, float progressPercent);

	std::vector<KeyFrameEvent> m_keyFrameEvent;
};



