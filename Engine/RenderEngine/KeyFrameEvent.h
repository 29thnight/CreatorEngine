#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
class KeyFrameEvent
{
   public:
   static consteval auto reflect()
   {
       using Self = KeyFrameEvent;
       return meta::schema<Self>(
           meta::field<&Self::m_eventName>,
           meta::field<&Self::m_scriptName>,
           meta::field<&Self::m_funName>,
           meta::field<&Self::key>,
           meta::field<&Self::frameKey>);
   }
public:
	KeyFrameEvent() = default;
	~KeyFrameEvent() {};
	bool operator==(const KeyFrameEvent& other) const
	{
		return m_eventName == other.m_eventName &&
			m_scriptName == other.m_scriptName &&
			m_funName == other.m_funName &&
			std::abs(key - other.key) < 0.0001f; //오차
	}

	std::string m_eventName  = "NoneE";
	std::string m_scriptName = "NoneS";
	std::string m_funName    = "NoneF";
	float key = 0;
	
	int  frameKey = 1; //애니메이션 프레임 int값
};
