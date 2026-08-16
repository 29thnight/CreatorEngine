#ifndef DYNAMICCPP_EXPORTS
#include "ScriptObjectRegistry.h"
#include "GameObject.h"

ScriptObjectRegistry& ScriptObjectRegistry::Get()
{
	static ScriptObjectRegistry instance;
	return instance;
}

ScriptObjectHandle ScriptObjectRegistry::Register(GameObject* object)
{
	if (nullptr == object)
	{
		return {};
	}

	std::lock_guard<std::mutex> guard(m_mutex);

	// 역방향 map 대신 선형 탐색(트랙 E4 — ScriptObjectRegistry.h 클래스 주석 참고).
	// object는 위에서 이미 nullptr을 걸렀으므로, 비어 있는(tombstone) 슬롯의
	// object==nullptr과 절대 겹치지 않는다.
	for (uint32_t i = 0; i < m_slots.size(); ++i)
	{
		if (m_slots[i].object == object)
		{
			return { i, m_slots[i].generation };
		}
	}

	uint32_t index;
	if (!m_freeSlots.empty())
	{
		index = m_freeSlots.back();
		m_freeSlots.pop_back();
	}
	else
	{
		index = static_cast<uint32_t>(m_slots.size());
		m_slots.push_back({});
	}

	Slot& slot = m_slots[index];
	slot.object = object;

	// 세대 0은 무효를 뜻하므로 건너뛴다. 재사용마다 1씩 올라간다.
	if (0 == slot.generation)
	{
		slot.generation = 1;
	}

	return { index, slot.generation };
}

void ScriptObjectRegistry::Unregister(GameObject* object)
{
	if (nullptr == object)
	{
		return;
	}

	std::lock_guard<std::mutex> guard(m_mutex);

	for (uint32_t i = 0; i < m_slots.size(); ++i)
	{
		if (m_slots[i].object != object)
		{
			continue;
		}

		Slot& slot = m_slots[i];
		slot.object = nullptr;

		// 세대를 올려 이 슬롯을 가리키던 기존 핸들을 전부 무효화한다.
		// 0으로 되돌아가면 "무효" 값과 겹치므로 건너뛴다.
		++slot.generation;
		if (0 == slot.generation)
		{
			slot.generation = 1;
		}

		m_freeSlots.push_back(i);
		return;
	}
	// 못 찾으면 조용히 넘어간다 — 이미 Unregister된 객체를 다시 부르는 경로가
	// 있다(GameObject::Destroy의 재귀 파괴 + 과거 ClrHost.cpp의 명시적 호출이
	// 겹치던 자리). idempotent해야 두 번째 호출이 안전하다.
}

GameObject* ScriptObjectRegistry::Resolve(ScriptObjectHandle handle) const
{
	if (!handle.IsValid())
	{
		return nullptr;
	}

	std::lock_guard<std::mutex> guard(m_mutex);

	if (handle.index >= m_slots.size())
	{
		return nullptr;
	}

	const Slot& slot = m_slots[handle.index];
	if (slot.generation != handle.generation)
	{
		return nullptr;   // 슬롯이 재사용됨 = 낡은 핸들
	}

	return slot.object;
}

void ScriptObjectRegistry::Clear()
{
	std::lock_guard<std::mutex> guard(m_mutex);

	// 슬롯 자체는 남기고 세대만 올린다. 밖에 나가 있는 핸들이 되살아나지 않게 하기 위함이다.
	for (size_t i = 0; i < m_slots.size(); ++i)
	{
		if (nullptr != m_slots[i].object)
		{
			m_slots[i].object = nullptr;
			++m_slots[i].generation;
			if (0 == m_slots[i].generation)
			{
				m_slots[i].generation = 1;
			}
			m_freeSlots.push_back(static_cast<uint32_t>(i));
		}
	}
}

size_t ScriptObjectRegistry::LiveCount() const
{
	std::lock_guard<std::mutex> guard(m_mutex);

	// 역방향 map이 없으므로(트랙 E4) 슬롯을 훑어 센다 — 진단용 호출이라
	// 빈도가 낮고, O(n)이어도 문제 되지 않는다.
	size_t count = 0;
	for (const auto& slot : m_slots)
	{
		if (nullptr != slot.object)
		{
			++count;
		}
	}
	return count;
}
#endif // !DYNAMICCPP_EXPORTS
