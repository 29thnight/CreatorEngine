#include "PrsmBindingContext.h"

#include "Entity.h"
#include "ScriptObjectRegistry.h"

#include <thread>

namespace prsm_engine
{
    namespace
    {
        thread_local const CallContext* t_current = nullptr;
        std::thread::id g_gameThread{};
    }

    void BindGameThread() noexcept
    {
        g_gameThread = std::this_thread::get_id();
    }

    bool OnGameThread() noexcept
    {
        return g_gameThread != std::thread::id{} && g_gameThread == std::this_thread::get_id();
    }

    CallScope::CallScope(Entity* owner, std::uint64_t ownerHandle) noexcept
        : m_context{ owner, ownerHandle }, m_previous(t_current)
    {
        t_current = &m_context;
    }

    CallScope::~CallScope()
    {
        t_current = m_previous;
    }

    const CallContext* CallScope::Current() noexcept
    {
        return t_current;
    }

    Entity* ResolveEntity(std::uint64_t handle) noexcept
    {
        const ScriptObjectHandle decoded{ static_cast<std::uint32_t>(handle),
            static_cast<std::uint32_t>(handle >> 32) };
        if (!decoded.IsValid())
        {
            return nullptr;
        }
        Entity* entity = ScriptObjectRegistry::Get().Resolve(decoded);
        // 파괴 표시가 선 엔티티는 같은 프레임 안에서도 스크립트에 보이지 않는다(C# IsAlive 와 같다).
        return (nullptr != entity && !entity->IsDestroyMark()) ? entity : nullptr;
    }
} // namespace prsm_engine
