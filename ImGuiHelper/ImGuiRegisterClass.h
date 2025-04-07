#pragma once
#include "ImGuiContext.h"
#include "ClassProperty.h"

class ImGuiRegister : public Singleton<ImGuiRegister>
{
private:
	friend class Singleton;
	friend class ImGuiRenderer;
public:
	void Register(const std::string_view& name, std::function<void()> function)
	{
		if (m_contexts.find(name.data()) == m_contexts.end())
		{
			m_contexts.emplace(name, ImGuiRenderContext(name));
		}

		m_contexts[name.data()].AddContext(function);
	}

	void Register(const std::string_view& name, std::function<void()> function, ImGuiWindowFlags flags)
	{
		if (m_contexts.find(name.data()) == m_contexts.end())
		{
			m_contexts.emplace(name, ImGuiRenderContext(name, flags));
		}
		m_contexts[name.data()].AddContext(function);
	}

	void Register(const std::string_view& name, bool isPopup, std::function<void()> function)
	{
		if (m_contexts.find(name.data()) == m_contexts.end())
		{
			m_contexts.emplace(name, ImGuiRenderContext(name));
		}
		m_contexts[name.data()].AddContext(function);
		m_contexts[name.data()].m_isPopup = isPopup;

		if (isPopup)
		{
			m_contexts[name.data()].m_opened = true; //지금은 열려있는 상태로 설정해서 테스트 진행하고 나중에 닫는 방식으로 변경
			m_contexts[name.data()].m_flags |= ImGuiWindowFlags_NoCollapse;
		}
	}

	void Register(const std::string_view& name, bool isPopup, std::function<void()> function, ImGuiWindowFlags flags)
	{
		if (m_contexts.find(name.data()) == m_contexts.end())
		{
			m_contexts.emplace(name, ImGuiRenderContext(name, flags));
		}
		m_contexts[name.data()].AddContext(function);
		m_contexts[name.data()].m_isPopup = isPopup;
		if (isPopup)
		{
			m_contexts[name.data()].m_opened = true; //지금은 열려있는 상태로 설정해서 테스트 진행하고 나중에 닫는 방식으로 변경
			m_contexts[name.data()].m_flags |= ImGuiWindowFlags_NoCollapse;
		}
	}

	void Register(const std::string_view& name, const std::string_view& subName, std::function<void()> function)
	{
		if (m_contexts.find(name.data()) == m_contexts.end())
		{
			m_contexts.emplace(name, ImGuiRenderContext(name));
		}
		m_contexts[name.data()].AddSubContext(subName.data(), function);
	}

	void Register(const std::string_view& name, const std::string_view& subName, std::function<void()> function, ImGuiWindowFlags flags)
	{
		if (m_contexts.find(name.data()) == m_contexts.end())
		{
			m_contexts.emplace(name, ImGuiRenderContext(name, flags));
		}
		m_contexts[name.data()].AddSubContext(subName.data(), function);
	}

	void Unregister(const std::string_view& name)
	{
		m_contexts.erase(name.data());
	}

	void Unregister(const std::string_view& name, const std::string_view& subName)
	{
		auto it = m_contexts.find(name.data());
		if (it != m_contexts.end())
		{
			it->second.RemoveSubContext(subName.data());
		}
	}

	ImGuiRenderContext& GetContext(const std::string_view& name)
	{
		return m_contexts[name.data()];
	}

private:
	std::unordered_map<std::string, ImGuiRenderContext> m_contexts;
};

