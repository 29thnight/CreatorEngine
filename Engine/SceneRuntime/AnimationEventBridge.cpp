// I5-D4e-2 — 애니메이션 이벤트의 C++↔CLR 경계.
//
// 구 형태는 Animation(공유 자산) 멤버의 이벤트 표면(InvokeEvent·CRUD)이었다 —
// postLoad가 씬 오버라이드를 공유 자산에 재주입하고 여기서 발화하는 구조라,
// 같은 스켈레톤을 공유하는 Animator 간 이벤트 오염(마지막 로드 승자)이 있었다
// (D0a 판정). 이제 소유가 Animator의 클립 오버라이드(m_clipOverrides)로
// 옮겨졌고, 이 파일은 그 오버라이드의 CRUD와 발화(CLR 큐잉)를 구현한다.
// 죽은 표면(SetEvent·문자열 FindEvent — 호출자 0, return 누락 UB)은 이주하지
// 않았다.
#include "Animator.h"
#include "Entity.h"
#include "ScriptComponent.h"
#include "ClrHost.h"
#include "Skeleton.h"
#include "../RenderEngine/Experiment/Model.h"

AnimatorClipOverride* Animator::FindClipOverride(int clipIndex)
{
	for (AnimatorClipOverride& clipOverride : m_clipOverrides)
	{
		if (clipOverride.clipIndex == clipIndex) return &clipOverride;
	}
	return nullptr;
}

const AnimatorClipOverride* Animator::FindClipOverride(int clipIndex) const
{
	for (const AnimatorClipOverride& clipOverride : m_clipOverrides)
	{
		if (clipOverride.clipIndex == clipIndex) return &clipOverride;
	}
	return nullptr;
}

AnimatorClipOverride& Animator::EnsureClipOverride(int clipIndex)
{
	if (AnimatorClipOverride* found = FindClipOverride(clipIndex))
	{
		return *found;
	}
	AnimatorClipOverride& created = m_clipOverrides.emplace_back();
	created.clipIndex = clipIndex;
	return created;
}

bool Animator::IsClipLooping(int clipIndex) const
{
	// 오버라이드가 정본, 없으면 자산값 — experiment 핸들이 있으면 그쪽
	// (원본 보존), 없으면 legacy 재주입 이전의 자산값이다. 재생 두 경로
	// (legacy 재귀·experiment 틱)가 같은 함수를 보므로 판정이 갈리지 않는다.
	if (const AnimatorClipOverride* clipOverride = FindClipOverride(clipIndex))
	{
		if (clipOverride->loopOverride.has_value())
		{
			return *clipOverride->loopOverride;
		}
	}
	if (m_experimentModel)
	{
		if (const experiment::Skeleton* skeleton = m_experimentModel->TryGetSkeleton())
		{
			if (clipIndex >= 0 && static_cast<std::size_t>(clipIndex)
				< skeleton->clips.size())
			{
				return skeleton->clips[static_cast<std::size_t>(clipIndex)].looping;
			}
		}
	}
	if (m_Skeleton && clipIndex >= 0
		&& static_cast<std::size_t>(clipIndex) < m_Skeleton->m_animations.size())
	{
		return m_Skeleton->m_animations[static_cast<std::size_t>(clipIndex)].m_isLoop;
	}
	return true;
}

void Animator::SetClipLooping(int clipIndex, bool looping)
{
	EnsureClipOverride(clipIndex).loopOverride = looping;
}

void Animator::AddClipEvent(int clipIndex)
{
	AnimatorClipOverride& clipOverride = EnsureClipOverride(clipIndex);
	std::string baseName = "newEvent";
	std::string realName = baseName;
	int suffix = 1;
	const auto nameExists = [&clipOverride](const std::string& name)
	{
		for (const KeyFrameEvent& event : clipOverride.events)
		{
			if (event.m_eventName == name) return true;
		}
		return false;
	};
	while (nameExists(realName))
	{
		realName = baseName + "(" + std::to_string(suffix) + ")";
		++suffix;
	}
	KeyFrameEvent newEvent;
	newEvent.m_eventName = realName;
	clipOverride.events.push_back(newEvent);
}

void Animator::DeleteClipEvent(int clipIndex, int eventIndex)
{
	AnimatorClipOverride* clipOverride = FindClipOverride(clipIndex);
	if (nullptr == clipOverride) return;
	if (eventIndex >= 0
		&& eventIndex < static_cast<int>(clipOverride->events.size()))
	{
		clipOverride->events.erase(clipOverride->events.begin() + eventIndex);
	}
}

std::size_t Animator::InvokeClipEvents(int clipIndex, float currentProgress,
	float previousProgress)
{
	const AnimatorClipOverride* clipOverride = FindClipOverride(clipIndex);
	if (nullptr == clipOverride || clipOverride->events.empty()) return 0;

	// 이벤트를 받을 오브젝트: 애니메이터가 붙은 것이 자식이면 부모로 올라간다.
	// (구 C++ 경로와 같은 규칙 — 스크립트는 보통 캐릭터 루트에 붙는다)
	Entity* owner = GetOwner();
	std::vector<ScriptComponent*> scripts;
	if (nullptr != owner)
	{
		const Entity::Index parentIndex = owner->GetParentIndex();
		if (parentIndex != 0)
		{
			Entity* parent = owner->OwnerSceneFindIndex(parentIndex);
			if (nullptr != parent) owner = parent;
		}
		scripts = owner->GetComponents<ScriptComponent>();
	}

	// 매칭 판정과 큐잉을 분리한다 — 계수는 CLR·스크립트 유무와 무관하게
	// 정확해야 게이트가 헤드리스에서 발화 규칙을 판정할 수 있다(큐잉 행동은
	// 구 InvokeEvent와 동일: 스크립트·CLR이 없으면 아무것도 안 보낸다).
	const bool looping = IsClipLooping(clipIndex);
	std::size_t matchedCount = 0;
	for (const KeyFrameEvent& event : clipOverride->events)
	{
		bool shouldTrigger = false;
		if (currentProgress > previousProgress)
		{
			// 일반 진행
			shouldTrigger = (previousProgress < event.key
				&& event.key <= currentProgress);
		}
		else if (looping)
		{
			// 되감긴 경우 — 구간이 끝과 처음으로 갈라진다
			shouldTrigger = (event.key > previousProgress && event.key <= 1.0f)
				|| (event.key >= 0.0f && event.key <= currentProgress);
		}
		if (!shouldTrigger) continue;
		++matchedCount;

		if (scripts.empty()) continue;
		auto& clr = ClrHost::Get();
		if (!clr.IsReady()) continue;

		// 발생 시점에 바로 부르지 않고 큐에 담는다 — 애니메이션 갱신은 잡
		// 스레드에서도 돌고, 경계는 틱당 한 번만 넘는다는 규약이 있다(설계
		// 문서 02절).
		for (ScriptComponent* script : scripts)
		{
			if (nullptr == script || !script->HasInstance()) continue;
			clr.QueueScriptMessage(script->GetInstanceId(), event.m_funName);
		}
	}
	return matchedCount;
}
