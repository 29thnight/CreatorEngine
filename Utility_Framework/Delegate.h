#pragma once
#include <atomic>
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

		// 여기 있던 UnsafeBroadcast·TargetInvoke·AsyncBroadcast를 걷어냈다 (PHASE 9-5).
		//
		// 셋 다 사용처가 0이었고, 둘은 이 엔진이 세운 규약을 정면으로 깨는 함정이었다:
		//   · UnsafeBroadcast — 락 없이 callbacks_를 순회한다. 다른 스레드가 Add/Remove
		//     하는 중이면 컨테이너가 발밑에서 재할당된다.
		//   · AsyncBroadcast  — 콜백을 std::async로 흩뿌린다. "관리 상태는 게임 스레드에서만"
		//     이라는 규약(PHASE 9가 세운 것)을 쓰는 쪽이 알아채지 못한 채 깨게 된다.
		// 쓰이지 않는 함정은 지우는 편이 낫다 — 남겨 두면 언젠가 누군가 이름만 보고 쓴다.

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

		// 브로드캐스트를 도중에 끊는 신호 (PHASE 9-5).
		//
		// 원자 변수인 이유: 읽는 쪽은 순회 중이라 락 밖이고, 쓰는 쪽(Clear)은 락 안이다.
		// 평범한 bool이면 그 조합이 형식상 데이터 경합이고, 컴파일러가 루프 밖으로
		// 끌어올려 캐시해 버리면 Clear가 걸어 둔 신호를 영영 못 볼 수도 있다.
		//
		// 한 번 서면 되돌리지 않는다 — 종료 가드이기 때문이다. 자세한 근거는
		// Delegate.inl의 Clear 주석에 있다(되돌리게 고쳤다가 종료 회귀로 드러났다).
		std::atomic<bool> isStopped_{ false };
	};
}

#include "Delegate.inl"
