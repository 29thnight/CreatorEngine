#pragma once
#include <iostream>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>
#include <optional>
#include <algorithm>
#include <concepts>
#include <future>

namespace Core
{
	class DelegateHandle
	{
	public:
		DelegateHandle() : id_(0) {}
		explicit DelegateHandle(std::uint32_t id) : id_(id) {}
		bool IsValid() const { return id_ != 0; }
		std::uint32_t GetID() const { return id_; }
		bool operator==(const DelegateHandle& other) const { return id_ == other.id_; }

		void Reset() { id_ = 0; }
	private:
		std::uint32_t id_;
	};

	template <typename T, typename Ret, typename... Args>
	concept CallableWithSignature = requires(T t, Args... args)
	{
		{ std::invoke(t, args...) } -> std::convertible_to<Ret>;
	};

	template <typename T, typename Ret, typename... Args>
	struct RawCallback {
		T* instance;
		Ret(T::* member)(Args...);
		int priority = 0;
	};

	template <typename T, typename Ret, typename... Args>
	struct SharedCallback {
		std::shared_ptr<T> instance;
		Ret(T::* member)(Args...);
		int priority = 0;
	};

	template <typename Callable>
	struct LambdaCallback {
		Callable lambda;
		int priority = 0;
	};

	template <typename Ret, typename... Args>
	class Delegate
	{
	public:
		Delegate() : nextID_(1) {}
		~Delegate();

		DelegateHandle AddLambda(CallableWithSignature<Ret, Args...> auto&& func, int priority = 0);
		template <typename T>
		DelegateHandle AddShared(const std::shared_ptr<T>& instance, Ret(T::* member)(Args...), int priority = 0);
		template <typename T>
		DelegateHandle AddRaw(T* instance, Ret(T::* member)(Args...), int priority = 0);
		void Remove(DelegateHandle& handle);
		void Clear();
		void Broadcast(Args... args);
		void UnsafeBroadcast(Args... args);
		void TargetInvoke(DelegateHandle& DelegateHandle, Args... args);
		template <typename R = Ret>
		std::vector<std::future<R>> AsyncBroadcast(Args... args);

		Delegate& operator()(Args... args)
		{
			Broadcast(args...);
			return *this;
		}

		Delegate& operator-=(DelegateHandle& handle)
		{
			Remove(handle);
			return *this;
		}

	private:
		struct CallbackInfo
		{
			DelegateHandle handle;
			std::function<Ret(Args...)> callback;
			int priority{};
		};

		DelegateHandle AddInternal(std::function<Ret(Args...)> func, int priority);

		std::atomic_flag atomic_flag_ = ATOMIC_FLAG_INIT;
		std::vector<CallbackInfo> callbacks_;
		std::uint32_t nextID_;
		bool isStopped_ = false;
	};
}

#include "Delegate.inl"
