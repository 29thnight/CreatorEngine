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

	if (auto found = m_lookup.find(object); found != m_lookup.end())
	{
		const uint32_t index = found->second;
		return { index, m_slots[index].generation };
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

	m_lookup.emplace(object, index);
	return { index, slot.generation };
}

void ScriptObjectRegistry::Unregister(GameObject* object)
{
	if (nullptr == object)
	{
		return;
	}

	std::lock_guard<std::mutex> guard(m_mutex);

	auto found = m_lookup.find(object);
	if (found == m_lookup.end())
	{
		return;
	}

	const uint32_t index = found->second;
	m_lookup.erase(found);

	Slot& slot = m_slots[index];
	slot.object = nullptr;

	// 세대를 올려 이 슬롯을 가리키던 기존 핸들을 전부 무효화한다.
	// 0으로 되돌아가면 "무효" 값과 겹치므로 건너뛴다.
	++slot.generation;
	if (0 == slot.generation)
	{
		slot.generation = 1;
	}

	m_freeSlots.push_back(index);
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

	m_lookup.clear();
}

size_t ScriptObjectRegistry::LiveCount() const
{
	std::lock_guard<std::mutex> guard(m_mutex);
	return m_lookup.size();
}
#endif // !DYNAMICCPP_EXPORTS
