#pragma once
#include <iostream>
#include <functional>
#include <vector>

template <typename T, typename Ret, typename... Args>
concept CallableWithSignature = requires(T t, Args... args) {
    { std::invoke(t, args...) } -> std::convertible_to<Ret>;
};

template <typename Ret, typename... Args>
class Delegate
{
public:
    using CallbackType = std::function<Ret(Args...)>;

    void Add(CallbackType&& callback)
    {
        callbacks.emplace_back(std::move(callback));
    }

    void RemoveAll()
    {
        callbacks.clear();
    }

    template <CallableWithSignature<Ret, Args...> F>
    void Add(F&& func)
    {
        callbacks.emplace_back(std::forward<F>(func));
    }

    void Invoke(Args... args)
    {
        for (auto& cb : callbacks)
        {
            cb(args...);
        }
    }

    void operator()(Args... args)
    {
        Invoke(std::forward<Args>(args)...);
    }

private:
    std::vector<CallbackType> callbacks;
};

// 안전한 Delegate 생성을 위한 make_delegate 함수
template <typename Ret, typename... Args>
auto make_delegate()
{
    return Delegate<Ret, Args...>{};
}
