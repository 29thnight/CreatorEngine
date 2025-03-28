#pragma once
#include "ImGUiRegisterClass.h"
#define EDITOR

namespace ImGui
{
#if defined(EDITOR)
	static inline void ContextRegister(const std::string& name, std::function<void()> function)
	{
		ImGuiRegister::GetInstance()->Register(name, function);
	}

	static inline void ContextRegister(const std::string& name, std::function<void()> function, ImGuiWindowFlags flags)
	{
		ImGuiRegister::GetInstance()->Register(name, function, flags);
	}

	static inline void ContextRegister(const std::string& name, bool isPopup, std::function<void()> function)
	{
		ImGuiRegister::GetInstance()->Register(name, isPopup, function);
	}

	static inline void ContextRegister(const std::string& name, bool isPopup, std::function<void()> function, ImGuiWindowFlags flags)
	{
		ImGuiRegister::GetInstance()->Register(name, isPopup, function, flags);
	}

	static inline void ContextRegister(const std::string& name, const std::string& subName, std::function<void()> function)
	{
		ImGuiRegister::GetInstance()->Register(name, subName, function);
	}

	static inline void ContextRegister(const std::string& name, const std::string& subName, std::function<void()> function, ImGuiWindowFlags flags)
	{
		ImGuiRegister::GetInstance()->Register(name, subName, function, flags);
	}

	static inline void ContextUnregister(const std::string& name)
	{
		ImGuiRegister::GetInstance()->Unregister(name);
	}

	static inline void ContextUnregister(const std::string& name, const std::string& subName)
	{
		ImGuiRegister::GetInstance()->Unregister(name, subName);
	}

	static inline ImGuiRenderContext& GetContext(const std::string& name)
	{
		return ImGuiRegister::GetInstance()->GetContext(name);
	}
#else
	static inline void ContextRegister(const std::string& name, std::function<void()> function) {}
	static inline void ContextRegister(const std::string& name, std::function<void()> function, ImGuiWindowFlags flags) {}
	static inline void ContextRegister(const std::string& name, bool isPopup, std::function<void()> function) {}
	static inline void ContextRegister(const std::string& name, bool isPopup, std::function<void()> function, ImGuiWindowFlags flags) {}
	static inline void ContextRegister(const std::string& name, const std::string& subName, std::function<void()> function) {}
	static inline void ContextRegister(const std::string& name, const std::string& subName, std::function<void()> function, ImGuiWindowFlags flags) {}
	static inline void ContextUnregister(const std::string& name) {}
	static inline void ContextUnregister(const std::string& name, const std::string& subName) {}
	static inline ImGuiRenderContext& GetContext(const std::string& name) { return ImGuiRenderContext(); }
#endif
}
