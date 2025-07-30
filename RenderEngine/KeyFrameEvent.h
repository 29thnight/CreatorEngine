#pragma once
#include "Core.Minimal.h"
#include "KeyFrameEvent.generated.h"
class KeyFrameEvent
{
public:
   ReflectKeyFrameEvent
	[[Serializable]]
	KeyFrameEvent() = default;
	~KeyFrameEvent() {};
	bool operator==(const KeyFrameEvent& other) const
	{
		return m_eventName == other.m_eventName &&
			m_scriptName == other.m_scriptName &&
			m_funName == other.m_funName &&
			std::abs(key - other.key) < 0.0001f; //¿ÀÂ÷
	}

	[[Property]]
	std::string m_eventName  = "NoneE";
	[[Property]]
	std::string m_scriptName = "NoneS";
	[[Property]]
	std::string m_funName    = "NoneF";
	[[Property]]
	float key = 0;
};
