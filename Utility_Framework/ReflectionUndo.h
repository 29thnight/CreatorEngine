#pragma once
// 에디터 Undo/Redo 스택 (CT3에서 ReflectionRegister.h로부터 분리).
//
// UndoManager는 리플렉션 "등록" 코어와 무관한 에디터 편의 계층인데 같은
// 헤더에 살면서 MetaStateCommand·<stack>을 등록 코어의 모든 소비자에게
// 실어 날랐다. 분리 후 소비자는 Reflection.hpp 사슬(ReflectionFunction.h)
// 이 이 헤더를 물어 주므로 기존 include 관행이 그대로 동작한다.
#include "MetaStateCommand.h"
#include "DLLAcrossSingleton.h"
#include <stack>
#include <memory>

namespace Meta
{
    class UndoManager : public DLLCore::Singleton<UndoManager>
    {
    private:
        UndoManager() = default;
        ~UndoManager() = default;
        friend DLLCore::Singleton<UndoManager>;

    public:
        void Execute(std::unique_ptr<IUndoableCommand> cmd)
        {
            if (false == m_isGameMode)
            {
                cmd->Redo();
                m_undoStack.push(std::move(cmd));
                while (!m_redoStack.empty()) m_redoStack.pop(); // Redo stack 초기화
            }
            else
            {
                cmd->Redo();
                m_gameModeUndoStack.push(std::move(cmd));
                while (!m_gameModeRedoStack.empty()) m_gameModeRedoStack.pop();
            }
        }

        void Undo()
        {
            if (false == m_isGameMode)
            {
                if (m_undoStack.empty()) return;
                auto cmd = std::move(m_undoStack.top());
                m_undoStack.pop();
                cmd->Undo();
                m_redoStack.push(std::move(cmd));
            }
            else
            {
                if (m_gameModeUndoStack.empty()) return;
                auto cmd = std::move(m_gameModeUndoStack.top());
                m_gameModeUndoStack.pop();
                cmd->Undo();
                m_gameModeRedoStack.push(std::move(cmd));
            }
        }

        void Redo()
        {
            if (false == m_isGameMode)
            {
                if (m_redoStack.empty()) return;
                auto cmd = std::move(m_redoStack.top());
                m_redoStack.pop();
                cmd->Redo();
                m_undoStack.push(std::move(cmd));
            }
            else
            {
                if (m_gameModeRedoStack.empty()) return;
                auto cmd = std::move(m_gameModeRedoStack.top());
                m_gameModeRedoStack.pop();
                cmd->Redo();
                m_gameModeUndoStack.push(std::move(cmd));
            }
        }

        void Clear()
        {
            while (!m_undoStack.empty()) m_undoStack.pop();
            while (!m_redoStack.empty()) m_redoStack.pop();
        }

        void ClearGameMode()
        {
            while (!m_gameModeUndoStack.empty()) m_gameModeUndoStack.pop();
            while (!m_gameModeRedoStack.empty()) m_gameModeRedoStack.pop();
        }

        bool m_isGameMode = false;

    private:
        std::stack<std::unique_ptr<IUndoableCommand>> m_undoStack;
        std::stack<std::unique_ptr<IUndoableCommand>> m_redoStack;

        std::stack<std::unique_ptr<IUndoableCommand>> m_gameModeUndoStack;
        std::stack<std::unique_ptr<IUndoableCommand>> m_gameModeRedoStack;
    };

    inline auto UndoCommandManager = UndoManager::GetInstance();

    inline void UndoSystemInitialize()
    {
        UndoManager::GetInstance();
    }

    inline void UndoSystemFinalize()
    {
        UndoManager::Destroy();
    }
}
