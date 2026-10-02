#pragma once

#include <memory>

namespace Editor
{
// Reserve the destination slot before a command commits files or domain state.
// Failed operations preserve both history stacks and the original command.
template<class Stack, class Command>
void ExecuteUndoHistory(Stack& undo, Stack& redo, std::unique_ptr<Command> command)
{
    undo.push(nullptr);
    try
    {
        command->Redo();
    }
    catch (...)
    {
        undo.pop();
        throw;
    }

    undo.top() = std::move(command);
    while (!redo.empty())
        redo.pop();
}

template<class Stack>
void TransferUndoHistory(Stack& source, Stack& destination, bool undo)
{
    if (source.empty())
        return;

    destination.push(nullptr);
    try
    {
        if (undo)
            source.top()->Undo();
        else
            source.top()->Redo();
    }
    catch (...)
    {
        destination.pop();
        throw;
    }

    destination.top() = std::move(source.top());
    source.pop();
}
} // namespace Editor
