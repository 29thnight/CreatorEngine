#include "Animation.h"

void Animation::InvokeEvent()
{
	if (!m_keyFrameEvent.empty())
	{
		for (auto& event : m_keyFrameEvent)
		{
			if (event.key == curKey)
				event.m_event();
		}
	}
}

void Animation::SetEvent(const std::string& _funName, float _key, std::function<void()> _func)
{
	KeyFrameEvent newEvent;
	newEvent.funName = _funName;
	newEvent.key = _key;
	newEvent.m_event = _func;
	m_keyFrameEvent.push_back(newEvent);
}
