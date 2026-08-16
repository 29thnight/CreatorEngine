#pragma once
// CT3: 숨은 순서 의존을 끊는다 — 이 헤더는 Meta::Property(ReflectionType.h)와
// std::function을 쓰면서 include가 0개였고, ReflectionRegister.h가 자기보다
// 앞줄에서 ReflectionType.h를 포함해 줄 때만 컴파일됐다.
#include "ReflectionType.h"
#include <functional>
#include <memory>

#ifndef interface
#define interface struct
#endif

namespace Meta
{
	interface IUndoableCommand
	{
		virtual ~IUndoableCommand() = default;
		virtual void Undo() = 0;
		virtual void Redo() = 0;
	};

	template<typename T>
	class PropertyChangeCommand : public IUndoableCommand
	{
	public:
		PropertyChangeCommand(void* target, const Meta::Property& prop, T oldValue, T newValue)
			: m_target(target), m_property(prop), m_oldValue(oldValue), m_newValue(newValue) {
		}
		~PropertyChangeCommand() override = default;

		void Undo() override { m_property.setter(m_target, m_oldValue); }
		void Redo() override { m_property.setter(m_target, m_newValue); }

	private:
		void* m_target;
		Meta::Property m_property;
		T m_oldValue;
		T m_newValue;
	};

	class CustomChangeCommand : public IUndoableCommand
	{
	public:
		CustomChangeCommand(std::function<void()> undoFunc, std::function<void()> redoFunc)
			: m_undoFunc(std::move(undoFunc)), m_redoFunc(std::move(redoFunc)) {
		}

		void Undo() override { if (m_undoFunc) m_undoFunc(); }
		void Redo() override { if (m_redoFunc) m_redoFunc(); }

	private:
		std::function<void()> m_undoFunc;
		std::function<void()> m_redoFunc;
	};

}