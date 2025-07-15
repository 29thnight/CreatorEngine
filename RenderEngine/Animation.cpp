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

	std::vector<ModuleBehavior*> scripts{};
	ModuleBehavior* script = nullptr;
	for (auto& component : owner->m_components)
	{
		if (nullptr == component)
			continue;
		script =  dynamic_cast<ModuleBehavior*>(component.get()); 
		if (script != nullptr) break;
	}

	if (script == nullptr) return;
	auto typeName = Meta::Find(script->GetHashedName().ToString());
	void* voidPtr = static_cast<void*>(script);
	/*for (auto& event : m_keyFrameEvent)
	{
		bool shouldTrigger = false;
		if (curAnimationProgress >= preAnimationProgress)
		{
			if (preAnimationProgress < event.key && event.key <= curAnimationProgress)
			{
				shouldTrigger = true;
			}
		}
		else
		{
			if ((event.key > preAnimationProgress && event.key <= 1.0f) ||
				(event.key >= 0.0f && event.key <= curAnimationProgress))
			{
				shouldTrigger = true;
			}
		}

		if (shouldTrigger)
		{
			Meta::InvokeMethodByMetaName(voidPtr, *typeName, event.m_funName, {});
		}
	}*/

	for (const auto& event : m_keyFrameEvent)
	{
		bool shouldTrigger = false;

		if (curAnimationProgress >= preAnimationProgress)
		{
			// 일반 진행
			if (preAnimationProgress < event.key && event.key <= curAnimationProgress)
			{
				shouldTrigger = true;
			}
		}
		else if (m_isLoop)
		{
			// 루프된 경우
			if ((event.key > preAnimationProgress && event.key <= 1.0f) ||
				(event.key >= 0.0f && event.key <= curAnimationProgress))
			{
				shouldTrigger = true;
			}
		}
		else
		{
			// 루프 아님 + cur < pre 는 float 오차 → 무시
		}

		if (shouldTrigger)
		{
			Meta::InvokeMethodByMetaName(voidPtr, *typeName, event.m_funName, {});
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
