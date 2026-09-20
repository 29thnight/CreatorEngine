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
#include "AnimationPlayback.h"
#include "Entity.h"
#include "ScriptComponent.h"
#include "ClrHost.h"
#include "Assets/ModelAnimationSampler.h" // PHASE 3.75 MBC8: typed 클립 계량

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
	// 오버라이드가 없으면 generation 자산값(MBC9: 유일한 출처).
	if (const assets::ModelAnimationAsset* clip = TypedClip(clipIndex))
		return clip->looping;
	return true;
}

void Animator::SetClipLooping(int clipIndex, bool looping)
{
	EnsureClipOverride(clipIndex).loopOverride = looping;
}

namespace
{
	void ReportClipPath(bool* outViaExperiment, AnimatorDataPath* outPath,
		AnimatorDataPath path) noexcept
	{
		if (outViaExperiment) *outViaExperiment = false;
		if (outPath) *outPath = path;
	}
}

std::size_t Animator::GetClipCount(bool* outViaExperiment,
	AnimatorDataPath* outPath) const
{
	if (nullptr != TypedSkeleton())
	{
		ReportClipPath(outViaExperiment, outPath, AnimatorDataPath::Generation);
		return TypedClipCount();
	}
	ReportClipPath(outViaExperiment, outPath, AnimatorDataPath::None);
	return 0;
}

std::string Animator::GetClipName(int clipIndex, bool* outViaExperiment,
	AnimatorDataPath* outPath) const
{
	ReportClipPath(outViaExperiment, outPath, AnimatorDataPath::None);
	if (clipIndex < 0 || nullptr == TypedSkeleton()) return std::string{};
	ReportClipPath(outViaExperiment, outPath, AnimatorDataPath::Generation);
	const assets::ModelAnimationAsset* clip = TypedClip(clipIndex);
	return clip ? clip->name : std::string{};
}

std::size_t Animator::GetClipFrameCount(int clipIndex,
	bool* outViaExperiment, AnimatorDataPath* outPath) const
{
	ReportClipPath(outViaExperiment, outPath, AnimatorDataPath::None);
	if (clipIndex < 0 || nullptr == TypedSkeleton()) return 0;
	ReportClipPath(outViaExperiment, outPath, AnimatorDataPath::Generation);
	const assets::ModelAnimationAsset* clip = TypedClip(clipIndex);
	return clip ? assets::animation::CountUniqueKeyTimes(*clip) : 0u;
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

std::size_t Animator::InvokeClipEvents(int clipIndex, double currentProgress,
	double previousProgress)
{
	const AnimatorClipOverride* clipOverride = FindClipOverride(clipIndex);
	if (nullptr == clipOverride || clipOverride->events.empty()
        || currentProgress == previousProgress) return 0;

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

    return animation::ForEachCrossedEvent(
        std::span<const KeyFrameEvent>(clipOverride->events),
        previousProgress, currentProgress, IsClipLooping(clipIndex),
        [](const KeyFrameEvent& event) { return event.key; },
        [&scripts](const KeyFrameEvent& event)
        {
            if (scripts.empty()) return;
            auto& clr = ClrHost::Get();
            if (!clr.IsReady()) return;
            for (ScriptComponent* script : scripts)
            {
                if (!script || !script->HasInstance()) continue;
                // Delivery remains in RuntimeFrame after the animation join.
                clr.QueueScriptMessage(script->GetInstanceId(), event.m_funName);
            }
        });
}
