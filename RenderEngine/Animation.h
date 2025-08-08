#pragma once
#include "Core.Minimal.h"
#include "Animation.generated.h"
#include "KeyFrameEvent.h"
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
	size_t m_totalKeyFrames = 0;
	float m_duration{};
	double m_ticksPerSecond{};
	[[Property]]
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

	[[Property]]
	std::vector<KeyFrameEvent> m_keyFrameEvent;
};



