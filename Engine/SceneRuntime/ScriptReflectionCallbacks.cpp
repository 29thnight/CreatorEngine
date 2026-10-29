#include "../Containers/ContainerAPI.h"
#include "ScriptReflectionCallbacks.h"
#include "BlackBoard.h"
#include <reflgen/json/writer.h>

namespace
{
    using ce::script::binding::ScriptCallbackDispatch;
    constexpr std::size_t MaximumCallbacks = 65536;
    constexpr std::size_t MaximumPayloadBytes = 1024 * 1024;

    struct callback_registry;
    struct callback_state
    {
        std::weak_ptr<callback_registry> registry;
        std::uint64_t token{};
        std::uint64_t generation{};
        std::uint64_t context{};
        int kind{};
        ScriptCallbackDispatch dispatch{};
        bool enabled = true;
        unsigned in_flight{};
    };

    struct callback_registry
    {
        std::mutex mutex;
        std::thread::id owner;
        bool active{};
        std::uint64_t generation{};
        std::uint64_t next_generation{};
        std::uint64_t next_token{};
        unsigned in_flight{};
        std::unordered_map<std::uint64_t, std::shared_ptr<callback_state>> callbacks;
        std::deque<std::uint64_t> retired;

        bool on_owner() const noexcept
        {
            return active && owner == std::this_thread::get_id();
        }
        void retire(const std::shared_ptr<callback_state>& state)
        {
            if (!state->enabled && state->in_flight == 0 && state->dispatch)
            {
                // Queue first. Allocation failure must retain the managed root.
                retired.push_back(state->token);
                state->dispatch = nullptr;
                callbacks.erase(state->token);
            }
        }
    };

    // Defined in this translation unit, not a header singleton. Counters and
    // retired notifications survive rebinding so old tokens never alias a new
    // session and a missed notification can be drained by the next binding.
    const auto g_callbacks = std::make_shared<callback_registry>();

    template<class Invoke>
    int InvokeCallback(std::shared_ptr<callback_state> state, Invoke&& invoke) noexcept
    {
        auto registry = state->registry.lock();
        if (!registry) return 0;
        ScriptCallbackDispatch dispatch{};
        std::uint64_t context{};
        int kind{};
        try
        {
            std::lock_guard lock(registry->mutex);
            if (!registry->on_owner() || !state->enabled || !state->dispatch
                || state->generation != registry->generation) return 0;
            ++state->in_flight;
            ++registry->in_flight;
            dispatch = state->dispatch;
            context = state->context;
            kind = state->kind;
        }
        catch (...) { return 0; }

        int result = 0;
        // Never hold the native mutex across user code. A callback may dispose
        // itself, replace another callback, or perform nested engine calls.
        try { result = invoke(dispatch, context, kind); }
        catch (...) { result = 0; }

        try
        {
            std::lock_guard lock(registry->mutex);
            --state->in_flight;
            --registry->in_flight;
            registry->retire(state);
        }
        catch (...) {} // Keep a disabled state rooted if retirement allocation fails.
        return result;
    }

    std::string SnapshotBlackBoard(const BlackBoard& board)
    {
        std::string output;
        reflgen::json::writer writer(output);
        const auto checkStringBudget = [&output](std::string_view text)
        {
            if (output.size() > MaximumPayloadBytes)
                throw std::length_error("BlackBoard callback snapshot exceeds 1 MiB");
            const auto remaining = MaximumPayloadBytes - output.size();
            if (remaining < 2 || text.size() > remaining - 2)
                throw std::length_error("BlackBoard callback snapshot exceeds 1 MiB");
            std::size_t encoded = 2; // Quotes. Count escapes before allocating.
            for (const unsigned char byte : text)
            {
                const std::size_t width = byte == '"' || byte == '\\' || byte == '\b'
                    || byte == '\f' || byte == '\n' || byte == '\r' || byte == '\t'
                    ? 2 : byte < 0x20 ? 6 : 1;
                if (width > remaining - encoded)
                    throw std::length_error("BlackBoard callback snapshot exceeds 1 MiB");
                encoded += width;
            }
        };
        const auto& values = board.GetValues();
        writer.begin_object(values.size());
        for (const auto& [name, value] : values)
        {
            checkStringBudget(name);
            writer.write_key(name);
            writer.begin_object(2);
            writer.write_key("type");
            writer.write_int(static_cast<int>(value.Type));
            writer.write_key("value");
            switch (value.Type)
            {
            case BlackBoardType::None: writer.write_null(); break;
            case BlackBoardType::Bool: writer.write_bool(value.BoolValue); break;
            case BlackBoardType::Int: writer.write_int(value.IntValue); break;
            case BlackBoardType::Float: writer.write_float32(value.FloatValue); break;
            case BlackBoardType::String:
            case BlackBoardType::Entity:
            case BlackBoardType::Transform:
                checkStringBudget(value.StringValue);
                writer.write_string(value.StringValue); break;
            case BlackBoardType::Vector2:
                writer.begin_array(2);
                writer.write_float32(value.Vec2Value.x); writer.write_float32(value.Vec2Value.y);
                writer.end_array(); break;
            case BlackBoardType::Vector3:
                writer.begin_array(3);
                writer.write_float32(value.Vec3Value.x); writer.write_float32(value.Vec3Value.y);
                writer.write_float32(value.Vec3Value.z);
                writer.end_array(); break;
            case BlackBoardType::Vector4:
                writer.begin_array(4);
                writer.write_float32(value.Vec4Value.x); writer.write_float32(value.Vec4Value.y);
                writer.write_float32(value.Vec4Value.z); writer.write_float32(value.Vec4Value.w);
                writer.end_array(); break;
            default: throw std::invalid_argument("unknown native BlackBoard value kind");
            }
            writer.end_object();
            if (output.size() > MaximumPayloadBytes)
                throw std::length_error("BlackBoard callback snapshot exceeds 1 MiB");
        }
        writer.end_object();
        if (output.size() > MaximumPayloadBytes)
            throw std::length_error("BlackBoard callback snapshot exceeds 1 MiB");
        return output;
    }

    template<class Function, class Callable>
    reflgen::value_view MakeCallback(const reflgen::value_descriptor& expected, Callable&& callable)
    {
        // Reuse the canonical registration descriptor rather than instantiating
        // an engine record descriptor in this consumer translation unit.
        if (expected.id() != reflgen::type_id_of<Function>()
            || expected.name() != reflgen::type_name_of<Function>()
            || expected.size() != sizeof(Function) || expected.alignment() != alignof(Function)) return {};
        auto storage = std::make_shared<Function>(std::forward<Callable>(callable));
        return reflgen::value_view(expected, storage.get(), storage.get(), storage);
    }

    std::uint64_t __stdcall BeginGeneration() noexcept
    {
        try
        {
            std::lock_guard lock(g_callbacks->mutex);
            if (!g_callbacks->on_owner() || g_callbacks->generation != 0
                || g_callbacks->in_flight != 0
                || g_callbacks->next_generation == std::numeric_limits<std::uint64_t>::max()) return 0;
            return g_callbacks->generation = ++g_callbacks->next_generation;
        }
        catch (...) { return 0; }
    }

    std::uint64_t __stdcall RegisterCallback(std::uint64_t generation, int kind,
        ScriptCallbackDispatch dispatch, std::uint64_t context) noexcept
    {
        try
        {
            std::lock_guard lock(g_callbacks->mutex);
            if (!g_callbacks->on_owner() || generation == 0 || generation != g_callbacks->generation
                || kind < 0 || kind > 4 || !dispatch || g_callbacks->callbacks.size() >= MaximumCallbacks
                || g_callbacks->next_token == std::numeric_limits<std::uint64_t>::max()) return 0;
            auto state = std::make_shared<callback_state>();
            state->registry = g_callbacks;
            state->token = ++g_callbacks->next_token;
            state->generation = generation;
            state->kind = kind;
            state->dispatch = dispatch;
            state->context = context;
            g_callbacks->callbacks.emplace(state->token, state);
            return state->token;
        }
        catch (...) { return 0; }
    }

    int __stdcall DisableCallback(std::uint64_t token) noexcept
    {
        try
        {
            std::lock_guard lock(g_callbacks->mutex);
            if (!g_callbacks->on_owner()) return -1;
            const auto found = g_callbacks->callbacks.find(token);
            if (found == g_callbacks->callbacks.end()) return 0;
            const auto state = found->second;
            state->enabled = false;
            g_callbacks->retire(state);
            return 1;
        }
        catch (...) { return -1; }
    }

    std::uint64_t __stdcall TakeRetired() noexcept
    {
        try
        {
            std::lock_guard lock(g_callbacks->mutex);
            if (!g_callbacks->on_owner()) return 0;
            // Retry a retirement that could not allocate its notification.
            for (auto iterator = g_callbacks->callbacks.begin(); iterator != g_callbacks->callbacks.end();)
            {
                const auto state = (iterator++)->second;
                g_callbacks->retire(state);
            }
            if (g_callbacks->retired.empty()) return 0;
            const auto token = g_callbacks->retired.front();
            g_callbacks->retired.pop_front();
            return token;
        }
        catch (...) { return 0; }
    }

    int __stdcall EndGeneration(std::uint64_t generation) noexcept
    {
        try
        {
            std::lock_guard lock(g_callbacks->mutex);
            if (!g_callbacks->on_owner() || g_callbacks->in_flight != 0) return -1;
            if (generation == 0 || generation != g_callbacks->generation) return 0;
            // Reject active calls before changing anything. Reload from inside
            // a callback must leave its existing generation and roots intact.
            for (const auto& [token, state] : g_callbacks->callbacks) state->enabled = false;
            while (!g_callbacks->callbacks.empty())
            {
                const auto state = g_callbacks->callbacks.begin()->second;
                g_callbacks->retire(state);
            }
            g_callbacks->generation = 0;
            return 1;
        }
        catch (...) { return -1; }
    }
}

namespace ce::script::binding
{
    void RegisterReflectionCallbackApi(ScriptApiTable& table)
    {
        {
            std::lock_guard lock(g_callbacks->mutex);
            if (g_callbacks->in_flight != 0)
                throw std::logic_error("cannot rebind the native callback session during an active callback");
            if (g_callbacks->active && g_callbacks->owner != std::this_thread::get_id())
                throw std::logic_error("cannot rebind the native callback session from a worker thread");
            // Validate before invalidating the previous session. A rejected
            // reentrant/worker rebind leaves every old callback usable.
            g_callbacks->active = false;
            g_callbacks->generation = 0;
            for (const auto& [token, state] : g_callbacks->callbacks) state->enabled = false;
            for (auto iterator = g_callbacks->callbacks.begin(); iterator != g_callbacks->callbacks.end();)
            {
                const auto state = (iterator++)->second;
                g_callbacks->retire(state);
            }
            g_callbacks->owner = std::this_thread::get_id();
            g_callbacks->active = true;
        }
        table.NativeCallback_BeginGeneration = &BeginGeneration;
        table.NativeCallback_Register = &RegisterCallback;
        table.NativeCallback_Disable = &DisableCallback;
        table.NativeCallback_TakeRetired = &TakeRetired;
        table.NativeCallback_EndGeneration = &EndGeneration;
    }

    void EndReflectionCallbackSession() noexcept
    {
        try
        {
            std::lock_guard lock(g_callbacks->mutex);
            g_callbacks->active = false;
            g_callbacks->generation = 0;
            for (const auto& [token, state] : g_callbacks->callbacks) state->enabled = false;
            for (auto iterator = g_callbacks->callbacks.begin(); iterator != g_callbacks->callbacks.end();)
            {
                const auto state = (iterator++)->second;
                g_callbacks->retire(state);
            }
        }
        catch (...) {} // Closed gates remain closed even if notification allocation fails.
    }
}

namespace ce::script::reflection
{
    reflgen::value_view ResolveCallback(std::uint64_t token, const reflgen::value_descriptor& expected)
    {
        std::shared_ptr<callback_state> state;
        {
            std::lock_guard lock(g_callbacks->mutex);
            if (!g_callbacks->on_owner()) return {};
            const auto found = g_callbacks->callbacks.find(token);
            if (found == g_callbacks->callbacks.end() || !found->second->enabled
                || found->second->generation != g_callbacks->generation) return {};
            state = found->second;
        }
        switch (state->kind)
        {
        case 0:
            return MakeCallback<ce::owned_func<void()>>(expected, [state]
            {
                InvokeCallback(state, [](auto dispatch, auto context, int kind)
                { return dispatch(context, kind, 0, 0, nullptr, 0); });
            });
        case 1:
            return MakeCallback<ce::owned_func<void(float)>>(expected, [state](float value)
            {
                InvokeCallback(state, [value](auto dispatch, auto context, int kind)
                { return dispatch(context, kind, value, 0, nullptr, 0); });
            });
        case 2:
            return MakeCallback<ce::owned_func<void(math::vector2)>>(expected, [state](math::vector2 value)
            {
                InvokeCallback(state, [value](auto dispatch, auto context, int kind)
                { return dispatch(context, kind, value.x, value.y, nullptr, 0); });
            });
        case 3:
            return MakeCallback<ce::owned_func<void(const std::string&)>>(expected, [state](const std::string& value)
            {
                InvokeCallback(state, [&value](auto dispatch, auto context, int kind)
                {
                    if (value.size() > MaximumPayloadBytes) return 0;
                    return dispatch(context, kind, 0, 0, value.data(), static_cast<int>(value.size()));
                });
            });
        case 4:
            return MakeCallback<ConditionFunc>(expected, [state](const BlackBoard& board)
            {
                return InvokeCallback(state, [&board](auto dispatch, auto context, int kind)
                {
                    const auto snapshot = SnapshotBlackBoard(board);
                    return dispatch(context, kind, 0, 0, snapshot.data(), static_cast<int>(snapshot.size()));
                }) != 0;
            });
        default: return {};
        }
    }
}
