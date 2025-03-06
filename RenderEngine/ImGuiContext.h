#pragma once
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>
#include "Core.Definition.h"

class ImGuiRenderContext
{
public:
    ImGuiRenderContext() = default;
    ImGuiRenderContext(const std::string_view& name) : m_name(name) {}
	ImGuiRenderContext(const std::string_view& name, ImGuiWindowFlags flags) : m_name(name), m_flags(flags) {}

    void AddContext(std::function<void()> function)
    {
        m_contexts.push_back(function);
    }

    void AddSubContext(const std::string& name, std::function<void()> function)
    {
        m_subContexts.emplace(name, function);
    }

    void RemoveSubContext(const std::string& name)
    {
        auto it = m_subContexts.find(name);
        if (it != m_subContexts.end())
        {
            m_subContexts.erase(it);
            std::cout << "Sub-context \"" << name << "\" removed successfully.\n";
        }
        else
        {
            std::cout << "Sub-context \"" << name << "\" not found.\n";
        }
    }
    
    void Render()
    {
        if (ImGui::Begin(m_name.data(), 0, m_flags))
        {
            for (auto& context : m_contexts)
            {
                context();
            }

            for (auto& [name, context] : m_subContexts)
            {
                if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    context();
                    ImGui::TreePop();
                }
            }
        }
        ImGui::End();
    }

private:
    std::string m_name;
    std::vector<std::function<void()>> m_contexts;
    std::unordered_map<std::string, std::function<void()>> m_subContexts;
	ImGuiWindowFlags m_flags = ImGuiWindowFlags_AlwaysAutoResize;
};
