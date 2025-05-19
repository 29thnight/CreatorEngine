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
};
