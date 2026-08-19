#include "Animation.h"
#include "GameObject.h"
#include "Animator.h"
#include "ScriptComponent.h"
#include "ClrHost.h"
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
	if (m_keyFrameEvent.empty() || nullptr == _ownerAnimator) return;

	// 이벤트를 받을 오브젝트: 애니메이터가 붙은 것이 자식이면 부모로 올라간다.
	// (구 C++ 경로와 같은 규칙 — 스크립트는 보통 캐릭터 루트에 붙는다)
	Entity* owner = _ownerAnimator->GetOwner();
	if (nullptr == owner) return;
	if (owner->m_parentIndex != 0)
	{
		Entity* parent = Entity::FindIndex(owner->m_parentIndex);
		if (nullptr != parent) owner = parent;
	}

	// 붙어 있는 C# 스크립트를 미리 모아 둔다. 이벤트가 여러 건이어도 순회는 한 번이면 된다.
	auto scripts = owner->GetComponents<ScriptComponent>();
	if (scripts.empty()) return;

	auto& clr = ClrHost::Get();
	if (!clr.IsReady()) return;

	for (const auto& event : m_keyFrameEvent)
	{
		bool shouldTrigger = false;

		if (_curAnimatonProgress > _preAnimationProgress)
		{
			// 일반 진행
			shouldTrigger = (_preAnimationProgress < event.key && event.key <= _curAnimatonProgress);
		}
		else if (m_isLoop)
		{
			// 되감긴 경우 — 구간이 끝과 처음으로 갈라진다
			shouldTrigger = (event.key > _preAnimationProgress && event.key <= 1.0f)
				|| (event.key >= 0.0f && event.key <= _curAnimatonProgress);
		}

		if (!shouldTrigger) continue;

		// 발생 시점에 바로 부르지 않고 큐에 담는다 — 애니메이션 갱신은 잡 스레드에서도
		// 돌고, 경계는 틱당 한 번만 넘는다는 규약이 있다(설계 문서 02절).
		for (auto* script : scripts)
		{
			if (nullptr == script || !script->HasInstance()) continue;
			clr.QueueScriptMessage(script->GetInstanceId(), event.m_funName);
		}
	}
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
