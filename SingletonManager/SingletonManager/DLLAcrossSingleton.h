#pragma once
#include "Export.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace DLLCore
{
	class ISingleton
	{
	private:
		static std::mutex mu;
		static std::unordered_map<uint64_t, void*> instance;
	protected:
		struct Result
		{
			void* ptr;
			bool needInit;
		};

		SINGLETON_API static auto CreateInstance(uint64_t hash, std::size_t size) -> Result;
		SINGLETON_API static void DeleteInstance(uint64_t hash);
	};

	// 전역 접근자 사용 시 주의
	// ---------------------------------------------------------------------------
	// 이 엔진은 헤더마다 `static auto Xxx = XxxClass::GetInstance();` 형태의 전역
	// 포인터를 두고 있다(SceneManagers, DataSystems, Debug 등). 이 값은 번역 단위마다
	// 복사된 원시 포인터라서 Destroy() 이후에도 파괴된 인스턴스의 주소를 그대로 들고
	// 있고, nullptr 검사로는 걸러지지 않는다. 실제로 종료 단계의 훅이 이런 포인터를
	// 역참조해 프로세스 종료 중 접근 위반이 발생한 적이 있다.
	//
	// 또한 Destroy() 후 GetInstance()를 부르면 인스턴스를 새로 만들어버리므로,
	// 종료 경로에서는 "살아 있으면 쓰고 아니면 건너뛴다"는 판단이 필요하다.
	//
	// 따라서 소멸자 / atexit / 크래시 핸들러처럼 수명 끝자락에서 도는 코드는
	// 전역 포인터 대신 GetIfAlive()를 써야 한다.
	//     if (auto* scene = SceneManager::GetIfAlive()) { scene->...; }
	template<typename T, bool shareDLL = true>
	class Singleton : private ISingleton
	{
	private:
		static std::atomic<T*> instance;
		static std::mutex mu;
	protected:
		Singleton() = default;
		~Singleton() = default;
	public:
		static auto GetInstance() -> T*;
		static void Destroy();

		// 살아 있으면 현재 인스턴스를, 파괴되었으면 nullptr을 반환한다.
		// GetInstance()와 달리 없을 때 새로 만들지 않는다.
		static T* GetIfAlive() noexcept { return instance.load(std::memory_order_acquire); }
		static bool IsAlive() noexcept { return GetIfAlive() != nullptr; }
	};

	template<typename T>
	class Singleton<T, false>
	{
	private:
		static std::atomic<T*> instance;
	protected:
		Singleton() = default;
	public:
		static auto GetInstance() -> T*;
		static void Destroy();

		static T* GetIfAlive() noexcept { return instance.load(std::memory_order_acquire); }
		static bool IsAlive() noexcept { return GetIfAlive() != nullptr; }
	};

	template<typename T, bool shareDLL>
	std::atomic<T*> Singleton<T, shareDLL>::instance{};
	template<typename T, bool shareDLL>
	std::mutex Singleton<T, shareDLL>::mu{};

	template<typename T>
	std::atomic<T*> Singleton<T, false>::instance{};
	/// @brief �̱��� �ν��Ͻ��� �����ϰų� �����´�.
	/// 
	/// @details ���� dll�� ��Ƽ ������ ȯ�濡���� lock�� ���� �ʰ� memory_order�� �̿��Ͽ� lock-free�� ����.
	/// @details �ٸ� dll�� ��Ƽ ������ ȯ�濡���� ISingleton�� CreateInstance���ο��� ���� �Ἥ ����.
	/// 
	/// @tparam T Ÿ��
	/// @tparam shareDLL DLL�� �޸� ������ ��������
	/// @return ��ü ������
	template<typename T, bool shareDLL>
	auto Singleton<T, shareDLL>::GetInstance() -> T*
	{
		T* instancePtr = instance.load(std::memory_order::memory_order_acquire);
		if (instancePtr == nullptr)
		{
			std::lock_guard<std::mutex> lockGuard{ mu };
			instancePtr = instance.load(std::memory_order::memory_order_relaxed);
			if (instancePtr == nullptr)
			{
				if constexpr (shareDLL)
				{
					uint64_t hash = typeid(T).hash_code();
					Result result = CreateInstance(hash, sizeof(T));
					void* resultPtr = result.ptr;
					if (result.needInit)
						new (resultPtr) T();
					instancePtr = reinterpret_cast<T*>(resultPtr);
				}
				else
					instancePtr = new T{};
				instance.store(instancePtr, std::memory_order::memory_order_release);
			}
		}
		return instancePtr;
	}

	/// @brief ���� �� �ִ� �̱��� �ν��Ͻ��� �޸𸮸� �����Ѵ�.
	/// @tparam T Ÿ��
	template<typename T, bool shareDLL>
	void Singleton<T, shareDLL>::Destroy()
	{
		T* instancePtr = instance.load(std::memory_order::memory_order_acquire);
		if (instancePtr != nullptr)
		{
			std::lock_guard<std::mutex> guard{ mu };
			instancePtr = instance.load(std::memory_order::memory_order_relaxed);
			if (instancePtr != nullptr)
			{
				if constexpr (shareDLL)
				{
					instancePtr->~T();
					DeleteInstance(typeid(T).hash_code());
				}
				else
					delete instancePtr;
				instance.store(nullptr, std::memory_order::memory_order_release);
			}
		}
	}
}