#pragma once
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