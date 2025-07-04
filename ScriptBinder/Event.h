#pragma once
#include "Core.Minimal.h"

class Event
{
public:
	Event() = default;
	~Event() = default;

public:
	void AddListener(const std::function<void()>& listener)
	{
		m_listeners.push_back(listener);
	}

	void RemoveListener(const std::function<void()>& listener)
	{
		auto it = std::remove_if(m_listeners.begin(), m_listeners.end(),
			[&listener](const std::function<void()>& l) { return l.target<void()>() == listener.target<void()>(); });
		m_listeners.erase(it, m_listeners.end());
	}

	void Notify()
	{
		for (const auto& listener : m_listeners)
		{
			if (listener)
			{
				listener();
			}
		}
	}

private:
	std::vector<std::function<void()>> m_listeners;
};