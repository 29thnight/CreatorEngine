#pragma once
#include "SpinLock.h"
#include "Delegate.h"
#include "Benchmark.hpp"

namespace Core
{
	template<typename Ret, typename ...Args>
	inline Delegate<Ret, Args...>::~Delegate()
	{
		//Clear();
	}
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
		// ★ 콜백 파괴를 락 밖으로 뺀다.
		//
		// 예전에는 callbacks_.clear()를 락 안에서 했다. 그 clear가 람다를
		// 파괴하는데, 람다가 잡고 있던 것의 소멸자가 같은 델리게이트의
		// Remove를 부르면 같은 스핀락을 다시 잡으려 든다. SpinLock은
		// 재진입 불가이고 대기에 상한이 없으므로 거기서 영원히 돈다.
		//
		// swap으로 소유권만 옮기고 락을 놓은 뒤에 파괴하면, 그 연쇄가
		// 일어나도 락이 비어 있어 통과한다.
		std::vector<CallbackInfo> doomed;
		{
			SpinLock lock(atomic_flag_);
			isStopped_ = true;
			doomed.swap(callbacks_);
		}
		// 여기서 doomed가 파괴된다 — 락 밖이다.
	}

	template <typename Ret, typename... Args>
	void Delegate<Ret, Args...>::Broadcast(Args... args)
	{
		std::vector<CallbackInfo> callbacksToInvoke;
		{
			SpinLock lock(atomic_flag_);
			callbacksToInvoke = callbacks_;
		}

		for (auto& info : callbacksToInvoke)
		{
			try 
			{ 
				if (isStopped_) break;

				info.callback(args...); 
			}
			catch (const std::exception& e) { std::cout << "Delegate Exception: " << e.what() << std::endl; continue; }
		}
	}

	template<typename Ret, typename ...Args>
	inline void Delegate<Ret, Args...>::UnsafeBroadcast(Args ...args)
	{
		for (auto& info : callbacks_)
		{
			try 
			{ 
				if (isStopped_) break;

				info.callback(args...); 
			}
			catch (const std::exception& e) { std::cout << "Delegate Exception: " << e.what() << std::endl; continue; }
		}
	}

	template<typename Ret, typename ...Args>
	inline void Delegate<Ret, Args...>::TargetInvoke(DelegateHandle& DelegateHandle, Args ...args)
	{
		std::vector<CallbackInfo> callbacksToInvoke;
		{
			SpinLock lock(atomic_flag_);
			callbacksToInvoke = callbacks_;
		}

		auto it = std::find_if(callbacksToInvoke.begin(), callbacksToInvoke.end(),
			[&DelegateHandle](const CallbackInfo& info) { return info.handle == DelegateHandle; });
		if (it != callbacksToInvoke.end())
		{
			try 
			{ 
				if (isStopped_) return;

				it->callback(args...); 
			}
			catch (const std::exception& e) { std::cout << "Delegate Exception: " << e.what() << std::endl; }
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
