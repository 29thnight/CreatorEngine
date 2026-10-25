#pragma once
#include "../../RenderArena.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Arena.h"
#else
import Creator.Core.Arena;
#endif

#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Platform/WindowsSDK/Creator.GraphicsSDK.h"
#else
import "Creator.GraphicsSDK.h";
#endif
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

// One producer records CPU payloads. The RHI owner executes them once; the
// submission retirement owner keeps payloads and captured resources until the
// GPU fence completes. Returning the lease resets and reuses the arena blocks.
class DX12PendingCommands final
{
  public:
    explicit DX12PendingCommands(const ce::RenderArenaPool& pool) : m_storage(pool.Acquire()) {}
    ~DX12PendingCommands()
    {
        while (m_first)
        {
            auto* node = m_first;
            m_first = node->next;
            node->destroy(node);
        }
    }
    DX12PendingCommands(const DX12PendingCommands&) = delete;
    DX12PendingCommands& operator=(const DX12PendingCommands&) = delete;

    template<class Function>
    void Add(Function&& function)
    {
        if (m_executed)
        {
            throw std::logic_error("Executed pending commands are immutable.");
        }
        using Payload = Command<std::decay_t<Function>>;
        auto* memory = m_storage->Resource(0).allocate(sizeof(Payload), alignof(Payload));
        auto* command = std::construct_at(static_cast<Payload*>(memory), std::forward<Function>(function));
        if (m_last)
        {
            m_last->next = command;
        }
        else
        {
            m_first = command;
        }
        m_last = command;
        ++m_count;
    }

    template<class Value>
    std::span<const Value> Copy(std::span<const Value> values)
    {
        static_assert(std::is_trivially_copyable_v<Value>);
        if (values.empty())
        {
            return {};
        }
        auto* memory = m_storage->Resource(1).allocate(values.size_bytes(), alignof(Value));
        std::memcpy(memory, values.data(), values.size_bytes());
        return {static_cast<const Value*>(memory), values.size()};
    }

    void Execute(ID3D12GraphicsCommandList* list)
    {
        if (m_executed || !list)
        {
            throw std::logic_error("Pending commands require one RHI execution with an open native list.");
        }
        m_executed = true;
        for (auto* command = m_first; command; command = command->next)
        {
            command->execute(command, list);
        }
    }

    uint64_t Count() const { return m_count; }

  private:
    struct Node
    {
        Node* next{};
        void (*execute)(Node*, ID3D12GraphicsCommandList*){};
        void (*destroy)(Node*){};
    };
    template<class Function>
    struct Command final : Node
    {
        explicit Command(Function value) : function(std::move(value))
        {
            this->execute = [](Node* node, ID3D12GraphicsCommandList* list)
            { static_cast<Command*>(node)->function(list); };
            this->destroy = [](Node* node) { std::destroy_at(static_cast<Command*>(node)); };
        }
        Function function;
    };
    ce::RenderArenaLease m_storage;
    Node* m_first{};
    Node* m_last{};
    uint64_t m_count{};
    bool m_executed{};
};
