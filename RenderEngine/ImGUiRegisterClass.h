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

private:
    std::unordered_map<std::string, ImGuiRenderContext> m_contexts;
};

