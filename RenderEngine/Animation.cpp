#include "Animation.h"


void Animation::InvokeEvent()
{
	
	if (m_keyFrameEvent.empty())
		return;

	for (auto& event : m_keyFrameEvent)
	{
		if (curAnimationProgress >= preAnimationProgress)
		{
			if (preAnimationProgress <= event.key && event.key < curAnimationProgress)
			{
				event.m_event();
			}
		}
		else
		{
			if ((event.key >= preAnimationProgress && event.key <= 1.0f) ||
				(event.key >= 0.0f && event.key < curAnimationProgress))
			{
				event.m_event();
			}
		}
	}
}

void Animation::SetEvent(const std::string& _funName, float progressPercent, std::function<void()> _func)
{
	KeyFrameEvent newEvent;
	newEvent.funName = _funName;
	newEvent.key = progressPercent;
	newEvent.m_event = _func;
	m_keyFrameEvent.push_back(newEvent);
}
