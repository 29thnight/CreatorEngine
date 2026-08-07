#include "Animation.h"
#include "GameObject.h"
#include "Animator.h"
void Animation::InvokeEvent()
{
	/*
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
	}*/
}

void Animation::InvokeEvent(Animator* _ownerAnimator, float _curAnimatonProgress, float _preAnimationProgress)
{
	// C++ 스크립트 은퇴(9-4): 수신자였던 ModuleBehavior가 사라져 키프레임 이벤트는
	// 현재 전달 대상이 없다. C#(ScriptComponent) 전달 경로는 ClrHost에 아직 없어
	// 후속 과제 — 그때까지 이벤트 데이터(m_keyFrameEvent)는 보존만 한다.
	(void)_ownerAnimator; (void)_curAnimatonProgress; (void)_preAnimationProgress;
}

void Animation::AddEvent()
{
	KeyFrameEvent newEvent;
	std::string baseName = "newEvent";
	std::string realName = baseName;
	int index = 1;
	// �ߺ� �̸��� �����ϸ� ���� ���̱�
	while (FindEventName(realName))
	{
		realName = baseName + "(" + std::to_string(index) + ")";
		index++;
	}

	newEvent.m_eventName = realName;
	m_keyFrameEvent.push_back(newEvent);
}

void Animation::AddEvent(KeyFrameEvent _event)
{
	for (auto& event : m_keyFrameEvent)
	{
		if (event.m_eventName == _event.m_eventName)
		{
			event.m_scriptName = _event.m_scriptName;
			event.m_funName = _event.m_funName;
			event.key = _event.key;
			return;
		}
	}


	m_keyFrameEvent.push_back(_event);
}

void Animation::DeleteEvent(KeyFrameEvent _event)
{
	KeyFrameEvent* target = FindEvent(_event);
	if (target != nullptr)
	{
		auto it = std::remove_if(m_keyFrameEvent.begin(), m_keyFrameEvent.end(),
			[&](KeyFrameEvent& e) {
				return &e == target;  
			});

		if (it != m_keyFrameEvent.end())
		{
			m_keyFrameEvent.erase(it, m_keyFrameEvent.end());
		}
	}

}

void Animation::DeleteEvent(int _index)
{
	if (_index >= 0 && _index < static_cast<int>(m_keyFrameEvent.size()))
	{
		m_keyFrameEvent.erase(m_keyFrameEvent.begin() + _index);
	}
}

KeyFrameEvent* Animation::FindEvent(KeyFrameEvent event)
{

	for (auto& _event : m_keyFrameEvent)
	{
		if(_event == event)
			return &_event;
	}
}

KeyFrameEvent* Animation::FindEvent(const std::string& _eventName, const std::string& _scriptName, const std::string& _funName, float progressPercent)
{
	for (auto& _event : m_keyFrameEvent)
	{
		if (_event.m_eventName == _eventName && _event.m_scriptName == _scriptName && _event.m_funName == _funName && _event.key == progressPercent)
		{
			return &_event;
		}

	}

	
}

bool Animation::FindEventName(std::string Name)
{


	for (auto& _event : m_keyFrameEvent)
	{
		if (_event.m_eventName == Name)
			return true;
	}
	return false;
}



void Animation::SetEvent(const std::string& _eventName,const std::string& _scriptName, const std::string& _funName, float progressPercent)
{
	KeyFrameEvent newEvent;
	for (auto& event : m_keyFrameEvent)
	{
		if (event.m_eventName == _eventName)
		{
			event.m_scriptName = _scriptName;
			event.m_funName = _funName;
			event.key = progressPercent;
			return;
		}	
	}		
	newEvent.m_eventName = _eventName;
	newEvent.m_scriptName = _scriptName;
	newEvent.m_funName = _funName;
	newEvent.key = progressPercent;
	m_keyFrameEvent.push_back(newEvent);
}
