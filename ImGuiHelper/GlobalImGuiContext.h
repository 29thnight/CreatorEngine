#pragma once
#include "DLLAcrossSingleton.h"
#include <ImGui.h>

class GlobalImGuiContext : public DLLCore::Singleton<GlobalImGuiContext>
{
private:
	friend class DLLCore::Singleton<GlobalImGuiContext>;
	GlobalImGuiContext() = default;
	~GlobalImGuiContext() = default;
public:
	ImGuiContext* GetContext() const { return m_context; }
	void SetContext(ImGuiContext* context) { m_context = context; }

	ImGuiMemAllocFunc p_alloc_func = nullptr;
	ImGuiMemFreeFunc p_free_func = nullptr;
	void* p_user_data = nullptr;

private:
	ImGuiContext* m_context = nullptr;

};