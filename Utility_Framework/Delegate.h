#pragma once
#include <iostream>
#include <functional>
#include <vector>
#include <utility>
#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <mutex>
#include <future>

template <typename T, typename Ret, typename... Args>
concept CallableWithSignature = requires(T t, Args... args) 
{
    { std::invoke(t, args...) } -> std::convertible_to<Ret>;
};

template <typename Ret, typename... Args>
class Delegate
{
public:
    using CallbackType = std::function<Ret(Args...)>;
    using CallbackID = std::size_t;

    struct CallbackInfo
    {
        CallbackID id;
        CallbackType callback;
        int priority;  // 높은 숫자가 높은 우선순위 (예: 10 > 0)
    };

    using CallbackContainer = std::vector<CallbackInfo>;
    template <typename T>
    using DelegateInvokeReturn = std::conditional_t<std::is_void_v<T>, bool, std::optional<T>>;

    class Connection 
    {
    public:
        Connection() = default;
        Connection(Delegate* delegate, CallbackID id)
            : delegate_(delegate), id_(id) 
        {
        }
        ~Connection() { Disconnect(); }

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& other) noexcept
            : delegate_(other.delegate_), id_(other.id_) 
        {
            other.delegate_ = nullptr;
        }
        Connection& operator=(Connection&& other) noexcept 
        {
            if (this != &other) 
            {
                Disconnect();
                delegate_ = other.delegate_;
                id_ = other.id_;
                other.delegate_ = nullptr;
            }
            return *this;
        }

        void Disconnect() 
        {
            if (delegate_) 
            {
                delegate_->Remove(id_);
                delegate_ = nullptr;
            }
        }

        bool IsConnected() const { return delegate_ != nullptr; }

    private:
        Delegate* delegate_ = nullptr;
        CallbackID id_ = 0;
    };

    Connection Add(CallbackType&& callback, int priority = 0) 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        CallbackID id = nextCallbackID++;
        CallbackInfo info{ id, std::move(callback), priority };
        auto it = std::lower_bound(callbacks.begin(), callbacks.end(), info,
            [](const CallbackInfo& a, const CallbackInfo& b) 
            {
                return a.priority > b.priority; 
            });
        callbacks.insert(it, info);
        return Connection(this, id);
    }

    template <CallableWithSignature<Ret, Args...> F>
    Connection Add(F&& func, int priority = 0) 
    {
        return Add(CallbackType(std::forward<F>(func)), priority);
    }

    void Remove(CallbackID id) 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(callbacks.begin(), callbacks.end(),
            [id](const CallbackInfo& info) { return info.id == id; });
        callbacks.erase(it, callbacks.end());
    }

    void RemoveAll() 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks.clear();
    }

    void Invoke(Args... args) 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& info : callbacks) 
        {
            try
            {
                info.callback(args...);
            }
			catch (const std::exception& e)
			{
				std::cerr << "Exception in Delegate callback: " << e.what() << std::endl;
			}
			catch (...)
			{
				std::cerr << "Unknown exception in Delegate callback." << std::endl;
			}
        }
    }

    void operator()(Args... args) 
    {
        Invoke(std::forward<Args>(args)...);
    }

    template<typename R = Ret>
    DelegateInvokeReturn<R> Invoke(CallbackID id, Args... args) 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(callbacks.begin(), callbacks.end(),
            [id](const CallbackInfo& info) { return info.id == id; });
        if (it != callbacks.end()) 
        {
            try 
            {
                if constexpr (std::is_void_v<R>) 
                {
                    it->callback(args...);
                    return true;
                }
                else 
                {
                    return it->callback(args...);
                }
            }
            catch (const std::exception& ex) 
            {
                std::cerr << "Exception in callback " << it->id << ": " << ex.what() << std::endl;
                if constexpr (std::is_void_v<R>) 
                {
                    return false;
                }
                else {
                    return std::nullopt;
                }
            }
            catch (...) 
            {
                std::cerr << "Unknown exception in callback " << it->id << std::endl;
                if constexpr (std::is_void_v<R>) 
                {
                    return false;
                }
                else 
                {
                    return std::nullopt;
                }
            }
        }
        if constexpr (std::is_void_v<R>)
            return false;
        else
            return std::nullopt;
    }

    std::vector<std::future<Ret>> InvokeAsync(Args... args) 
    {
        CallbackContainer localCallbacks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            localCallbacks = callbacks;
        }
        std::vector<std::future<Ret>> futures;
        futures.reserve(localCallbacks.size());
        for (const auto& info : localCallbacks) 
        {
            futures.push_back(std::async(std::launch::async, info.callback, args...));
        }
        return futures;
    }

    template<typename R = Ret>
    std::optional<std::future<R>> InvokeAsync(CallbackID id, Args... args) 
    {
        CallbackType callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find_if(callbacks.begin(), callbacks.end(),
                [id](const CallbackInfo& info) { return info.id == id; });
            if (it != callbacks.end()) 
            {
                callback = it->callback;
            }
            else 
            {
                return std::nullopt;
            }
        }
        return std::async(std::launch::async, callback, args...);
    }

private:
    Delegate() : nextCallbackID(0) {}

    CallbackContainer callbacks;
    CallbackID nextCallbackID;
    mutable std::mutex mutex_;

    template <typename R, typename... A>
    friend auto make_delegate() -> Delegate<R, A...>;
};

template <typename Ret, typename... Args>
static inline auto make_delegate() -> Delegate<Ret, Args...>
{
    return Delegate<Ret, Args...>{};
}
