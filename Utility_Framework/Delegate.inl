#pragma once
#include "SpinLock.h"
#include "Delegate.h"

namespace Core
{
	template <typename Ret, typename... Args>
	auto Delegate<Ret, Args...>::AddLambda(CallableWithSignature<Ret, Args...> auto&& func, int priority) -> DelegateHandle
	{
		return AddInternal(std::function<Ret(Args...)>(std::forward<decltype(func)>(func)), priority);
	}

	template <typename Ret, typename... Args>
	template <typename T>
	auto Delegate<Ret, Args...>::AddShared(const std::shared_ptr<T>& instance, Ret(T::* member)(Args...), int priority) -> DelegateHandle
	{
		std::weak_ptr<T> weakInstance = instance;
		return AddInternal([weakInstance, member](Args... args) {
			if (auto shared = weakInstance.lock())
				(shared.get()->*member)(args...);
			}, priority);
	}

	template <typename Ret, typename... Args>
	template <typename T>
	auto Delegate<Ret, Args...>::AddRaw(T* instance, Ret(T::* member)(Args...), int priority) -> DelegateHandle
	{
		return AddInternal([instance, member](Args... args) {
			if (instance)
				(instance->*member)(args...);
			}, priority);
	}

	template <typename Ret, typename... Args>
	auto Delegate<Ret, Args...>::AddInternal(std::function<Ret(Args...)> func, int priority) -> DelegateHandle
	{
		SpinLock lock(atomic_flag_);
		DelegateHandle handle(nextID_++);
		CallbackInfo info{ handle, std::move(func), priority };
		auto it = std::lower_bound(callbacks_.begin(), callbacks_.end(), info, [](const CallbackInfo& a, const CallbackInfo& b) {
			return a.priority > b.priority;
			});
		callbacks_.insert(it, std::move(info));
		return handle;
	}

	template <typename Ret, typename... Args>
	void Delegate<Ret, Args...>::Remove(DelegateHandle& handle)
	{
		SpinLock lock(atomic_flag_);
		if (callbacks_.size() == 0) return;
		callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
			[&handle](const CallbackInfo& info) { return info.handle == handle; }), callbacks_.end());

		handle.Reset();
	}

	template <typename Ret, typename... Args>
	void Delegate<Ret, Args...>::Clear()
	{
		SpinLock lock(atomic_flag_);
		callbacks_.clear();
	}

	template <typename Ret, typename... Args>
	void Delegate<Ret, Args...>::Broadcast(Args... args)
	{
		SpinLock lock(atomic_flag_);
		for (auto& info : callbacks_)
		{
			try { info.callback(args...); }
			catch (const std::exception& e) { std::cerr << "Delegate Exception: " << e.what() << std::endl; continue; }
		}
	}

	template<typename Ret, typename ...Args>
	inline void Delegate<Ret, Args...>::TargetInvoke(DelegateHandle& DelegateHandle, Args ...args)
	{
		SpinLock lock(atomic_flag_);
		auto it = std::find_if(callbacks_.begin(), callbacks_.end(),
			[&DelegateHandle](const CallbackInfo& info) { return info.handle == DelegateHandle; });
		if (it != callbacks_.end())
		{
			try { it->callback(args...); }
			catch (const std::exception& e) { std::cerr << "Delegate Exception: " << e.what() << std::endl; }
		}
	}

	template <typename Ret, typename... Args>
	template <typename R>
	auto Delegate<Ret, Args...>::AsyncBroadcast(Args... args) -> std::vector<std::future<R>>
	{
		std::vector<CallbackInfo> localCallbacks;
		{
			SpinLock lock(atomic_flag_);
			localCallbacks = callbacks_;
		}
		std::vector<std::future<R>> futures;
		futures.reserve(localCallbacks.size());
		for (auto& info : localCallbacks)
			futures.emplace_back(std::async(std::launch::async, info.callback, args...));
		return futures;
	}
}
