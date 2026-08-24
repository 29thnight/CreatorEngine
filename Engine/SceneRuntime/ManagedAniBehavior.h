#pragma once
#include "AniBehavior.h"
#include "ClrHost.h"

class Entity;

/// C#으로 쓴 애니메이션 상태 스크립트를 네이티브 상태 머신에 끼워 넣는 어댑터.
///
/// AnimationState는 AniBehavior* 하나만 알면 되므로, 관리 인스턴스를 감싼 이것을
/// 대신 돌려주면 상태 머신 쪽은 손댈 필요가 없다 — 네이티브 인터페이스는 그대로 두고
/// 구현만 관리 측으로 옮기는, ScriptComponent와 같은 어댑터 구조다.
///
/// Enter/Update/Exit는 곧바로 경계를 넘지 않고 큐에 쌓는다. 틱 경계에서
/// ClrHost::FlushAniEvents가 한 번에 넘긴다.
class ManagedAniBehavior : public AniBehavior
{
public:
	explicit ManagedAniBehavior(std::string_view typeName)
	{
		m_name = std::string(typeName);
		m_instanceId = ClrHost::Get().CreateAniBehaviour(typeName);
	}

	~ManagedAniBehavior() override
	{
		if (HasInstance()) ClrHost::Get().DestroyAniBehaviour(m_instanceId);
	}

	ManagedAniBehavior(const ManagedAniBehavior&) = delete;
	ManagedAniBehavior& operator=(const ManagedAniBehavior&) = delete;

	bool HasInstance() const { return m_instanceId >= 0; }
	int  GetInstanceId() const { return m_instanceId; }

	void Enter() override { Queue(ClrHost::AniEventKind::Enter, 0.f); }
	void Update(float deltaTime) override { Queue(ClrHost::AniEventKind::Update, deltaTime); }
	void Exit() override { Queue(ClrHost::AniEventKind::Exit, 0.f); }

private:
	// 소유 오브젝트는 컨트롤러 → Animator → GameObject로 거슬러 올라가 찾는다.
	// 생성 시점에는 m_ownerController가 아직 비어 있어 매번 여기서 구한다.
	Entity* ResolveOwner() const;

	void Queue(ClrHost::AniEventKind kind, float deltaTime)
	{
		if (!HasInstance()) return;
		ClrHost::Get().QueueAniEvent(m_instanceId, kind, deltaTime, ResolveOwner());
	}

	int m_instanceId{ -1 };
};
