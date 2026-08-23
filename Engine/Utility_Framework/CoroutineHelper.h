#pragma once
#include <coroutine>
#include <functional>
#include <vector>
#include <optional>
#include <cassert>
#include <type_traits>
#include "LinkedListLib.hpp"

enum class YieldInstructionType 
{
    None,
    WaitForFixedUpdate,
    Null,
    WaitForSeconds,
    WaitForFrames,
    WaitUntil,
    WaitForSignal,
    WaitForEndOfFrame,
    OnRender
};

struct YieldInstruction
{
    YieldInstructionType type = YieldInstructionType::None;
    float timeRemaining = 0.0f;
    int frameRemaining = 0;
    std::function<bool()> condition;
    bool* signal = nullptr;

    friend YieldInstruction WaitForSeconds(float sec);

    friend YieldInstruction WaitForFrames(int frames);

    friend YieldInstruction WaitUntil(std::function<bool()> func);

    friend YieldInstruction WaitForSignal(bool* signalFlag);

    friend YieldInstruction WaitForFixedUpdate();

    friend YieldInstruction WaitForEndOfFrame();

    bool Tick(float deltaTime)
    {
        switch (type)
        {
        case YieldInstructionType::WaitForSeconds:
            timeRemaining -= deltaTime;
            return timeRemaining <= 0.0f;
        case YieldInstructionType::WaitForFrames:
            --frameRemaining;
            return frameRemaining <= 0;
        case YieldInstructionType::WaitUntil:
            return condition && condition();
        case YieldInstructionType::WaitForSignal:
            return signal && *signal;
        case YieldInstructionType::WaitForFixedUpdate:
        case YieldInstructionType::WaitForEndOfFrame:
        case YieldInstructionType::Null:
        case YieldInstructionType::None:
        case YieldInstructionType::OnRender:
            return true; // 처리 큐 전환 후 1프레임 대기
        default:
            return true;
        }
    }
};

template<typename T = YieldInstruction>
struct Coroutine {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type
    {
        std::optional<T> current_value;

        Coroutine get_return_object() 
        {
            return Coroutine{ handle_type::from_promise(*this) };
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        std::suspend_always yield_value(T value) 
        {
            current_value = std::move(value);
            return {};
        }

        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    handle_type handle;

    explicit Coroutine(handle_type h) : handle(h) {}
    Coroutine(const Coroutine&) = delete;
    Coroutine(Coroutine&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    ~Coroutine() 
    {
        if (handle)
            handle.destroy();
    }

    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine& operator=(Coroutine&& other) noexcept 
    {
        if (this != &other) 
        {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    bool resume() 
    {
        if (!handle.done())
            handle.resume();
        return !handle.done();
    }

    T& current() const 
    {
        static T nullValue = T{};

        if (handle.promise().current_value.has_value())
        {
            return handle.promise().current_value.value();
        }
        else
        {
			return nullValue;
        }
    }

    bool is_done() const 
    {
        return handle.done();
    }
};

