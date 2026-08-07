#pragma once
#include "SpinLock.h"
#include "Delegate.h"
#include "Benchmark.hpp"
#include "LogSystem.h"

namespace Core
{
	namespace detail
	{
		// 콜백에서 새어 나온 예외를 로그로 남긴다 (PHASE 9-5).
		//
		// 예전에는 std::cout으로 찍었다. 그 출력은 로그 파일에도, 크래시 리포트에도
		// 남지 않아 나중에 원인을 되짚을 수 없었고, 콘솔이 없는 실행에서는 아무 데도
		// 가지 않았다. 구독자 하나가 던진 예외를 나머지를 위해 삼키는 것은 유지하되,
		// 삼킨 사실은 남긴다 — 삼키면서 흔적도 없으면 그건 사고를 지우는 것이다.
		inline void ReportDelegateException(const char* phase, const char* what)
		{
			try
			{
				Debug->LogError(std::string("[Delegate] ") + phase + " 중 예외: " + what);
			}
			catch (...)
			{
				// 로그조차 불가능한 상황(초기화 전·종료 중)에서는 조용히 넘긴다.
			}
		}
	}
	template<typename Ret, typename ...Args>
	inline Delegate<Ret, Args...>::~Delegate()
	{
		// 여기서 Clear를 부르지 않는다.
		//
		// 소멸자가 도는 시점에는 이 델리게이트를 붙들고 있던 것들도 함께 무너지는
		// 중이라, 콜백을 파괴하다 그 소멸자가 다시 이 객체를 만질 수 있다.
		// 그때는 이미 멤버가 파괴 중이므로 되돌릴 방법이 없다.
		//
		// callbacks_는 vector라 알아서 파괴된다. 명시적으로 끊어야 하는 곳은
		// 소유자가 살아 있는 동안의 Clear이고, 그건 호출부가 부른다.
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
			// 진행 중일 수 있는 순회를 끊는다.
			isStopped_.store(true, std::memory_order_release);
			doomed.swap(callbacks_);
		}
		// 여기서 doomed가 파괴된다 — 락 밖이다.

		// ★ 이 플래그는 되돌리지 않는다. 되돌릴 수 없는 것이 의도다.
		//
		// 9-5에서 "Clear 후 재사용 불가"를 결함으로 보고 되돌리게 고쳤다가 종료 순서
		// 회귀가 6회 중 1회 실패했다(그 전 세 번은 6/6이었다). 되짚어 보니 이 래치는
		// 종료 가드로 쓰이고 있었다:
		//
		//   Broadcast는 콜백 목록을 복사해 두고 락 밖에서 순회한다. 종료 중에 Clear가
		//   불리면 그 복사본은 여전히 유효하지만 콜백이 가리키는 대상은 무너지는 중이다.
		//   래치가 서 있어야 다음 반복에서 break로 빠져나온다. 되돌리면 그 창이
		//   나노초로 줄어 순회가 그대로 이어지고, 파괴 중인 객체를 호출한다.
		//
		// 즉 "재사용 불가"는 결함이 아니라 성질이다. 실제로 Clear한 델리게이트를 다시
		// 쓰는 곳도 없다. 언젠가 필요해지면 그때 별도 연산으로 명시해야 한다 —
		// Clear가 조용히 재사용을 허용하면 종료 경로가 다시 열린다.
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
				if (isStopped_.load(std::memory_order_acquire)) break;

				info.callback(args...); 
			}
			catch (const std::exception& e) { detail::ReportDelegateException("Broadcast", e.what()); continue; }
		}
	}

}
