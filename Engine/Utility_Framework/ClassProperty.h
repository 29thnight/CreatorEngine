#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

class Noncopyable
{
protected:
	Noncopyable() = default;
	~Noncopyable() = default;

protected:
	Noncopyable(const Noncopyable&) = delete;
	Noncopyable(Noncopyable&&) = delete;
	Noncopyable& operator=(const Noncopyable&) = delete;
	Noncopyable& operator=(Noncopyable&&) = delete;
};

/// 프로세스 전역 단일 인스턴스.
///
/// SingletonManager.dll(DLLCore::Singleton)을 대체한다. 그쪽은 DLL마다 따로
/// 생기는 정적 변수를 한곳으로 모으려고 타입 해시 → void* 표를 DLL이 들고
/// 있었다. 그런데 엔진 모듈은 전부 StaticLibrary라 하나의 EXE로 링크되고,
/// 정적 변수는 애초에 하나뿐이다 — 그 표가 풀던 문제가 존재하지 않는다.
/// 남은 것은 할당마다 치르는 DLL 임포트 썽크와, 관리해야 할 프로젝트 하나였다.
///
/// 옛 ClassProperty 판에서 고친 결함:
///   · GetInstance()가 const shared_ptr&를 돌려줬다. 호출부가 흔히 쓰는
///     `static auto Xxx = Xxx::GetInstance();`가 번역 단위마다 소유권을 한 벌씩
///     복사해 가므로, Destroy()를 불러도 실제로 파괴되지 않았다. 소유는 이 클래스
///     하나가 갖고 밖으로는 관찰용 포인터만 내보낸다.
///   · Destroy()·GetIfAlive()·IsAlive()가 없었다. 종료 경로는 "살아 있으면 쓰고
///     아니면 건너뛴다"를 판단할 수단이 필요하다.
///   · std::call_once는 되감을 수 없다. 파괴 후 재생성이 원천 불가였고, 그래서
///     사실상 파괴가 없는 설계였다. 원자 포인터 + 이중 검사로 바꾼다.
///   · 가상 소멸자가 모든 싱글톤에 vtable을 강제했다. 파괴는 항상 Destroy()가
///     T*로 수행하므로 기반의 가상 소멸자는 필요 없다.
///
/// 전역 접근자 사용 시 주의
/// ---------------------------------------------------------------------------
/// 헤더마다 `static auto Xxx = XxxClass::GetInstance();` 형태의 전역 포인터가
/// 있다(SceneManagers, DataSystems, Debug 등). 이 값은 번역 단위마다 복사된
/// 원시 포인터라서 Destroy() 이후에도 파괴된 인스턴스의 주소를 그대로 들고
/// 있고, nullptr 검사로는 걸러지지 않는다. 실제로 종료 단계의 훅이 이런 포인터를
/// 역참조해 프로세스 종료 중 접근 위반이 난 적이 있다.
///
/// 따라서 소멸자 / atexit / 크래시 핸들러처럼 수명 끝자락에서 도는 코드는
/// 전역 포인터 대신 GetIfAlive()를 써야 한다.
///     if (auto* scene = SceneManager::GetIfAlive()) { scene->...; }
template <typename T>
class Singleton : public Noncopyable
{
protected:
	Singleton() = default;
	~Singleton() = default;

public:
	/// 없으면 만들어서 돌려준다. 소유는 이 클래스가 갖는다 — 호출부는 관찰만 한다.
	static T* GetInstance()
	{
		T* instance = s_instance.load(std::memory_order_acquire);
		if (nullptr == instance)
		{
			std::lock_guard<std::mutex> guard{ s_mutex };
			instance = s_instance.load(std::memory_order_relaxed);
			if (nullptr == instance)
			{
				instance = new T();
				s_instance.store(instance, std::memory_order_release);
			}
		}
		return instance;
	}

	/// 살아 있으면 현재 인스턴스를, 파괴되었으면 nullptr을 반환한다.
	/// GetInstance()와 달리 없을 때 새로 만들지 않는다.
	static T* GetIfAlive() noexcept { return s_instance.load(std::memory_order_acquire); }
	static bool IsAlive() noexcept { return nullptr != GetIfAlive(); }

	/// 파괴한다. 이후 GetInstance()는 새 인스턴스를 만든다(옛 DLLCore 판과 같은 규약).
	static void Destroy()
	{
		std::lock_guard<std::mutex> guard{ s_mutex };
		T* instance = s_instance.exchange(nullptr, std::memory_order_acq_rel);
		delete instance;
	}

private:
	static std::atomic<T*> s_instance;
	static std::mutex      s_mutex;
};

template <typename T>
std::atomic<T*> Singleton<T>::s_instance{ nullptr };

template <typename T>
std::mutex Singleton<T>::s_mutex{};
