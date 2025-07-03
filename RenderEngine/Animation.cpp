#include "Animation.h"
#include "GameObject.h"
#include "Animator.h"
#include "ModuleBehavior.h"
#include "TestAniScprit.h"
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

void Animation::InvokeEvent(Animator* _ownerAnimator)
{
	if (m_keyFrameEvent.empty())
		return;
	GameObject* owner = _ownerAnimator->GetOwner();
	TestAniScprit* Script = owner->GetComponent<TestAniScprit>();
	auto typeName = Meta::Find(Script->GetTypeID());
	void* voidPtr = static_cast<void*>(Script);
	for (auto& event : m_keyFrameEvent)
	{
		if (curAnimationProgress >= preAnimationProgress)
		{
			if (preAnimationProgress <= event.key && event.key < curAnimationProgress)
			{
				Meta::InvokeMethodByMetaName(voidPtr, *typeName, event.m_funName, { });
			}
		}
		else
		{
			if ((event.key >= preAnimationProgress && event.key <= 1.0f) ||
				(event.key >= 0.0f && event.key < curAnimationProgress))
			{
				Meta::InvokeMethodByMetaName(voidPtr, *typeName, event.m_funName, { });
			}
		}
	}

}



void Animation::SetEvent(const std::string& _funName, float progressPercent, std::function<void()> _func)
{
	KeyFrameEvent newEvent;
	newEvent.m_funName = _funName;
	newEvent.key = progressPercent;
	newEvent.m_event = _func;
	m_keyFrameEvent.push_back(newEvent);
}

void Animation::SetEvent(const std::string& _scriptName, const std::string& _funName, float progressPercent)
{
	KeyFrameEvent newEvent;
	newEvent.m_scriptName = _scriptName;
	newEvent.m_funName = _funName;
	newEvent.key = progressPercent;
	m_keyFrameEvent.push_back(newEvent);
}
