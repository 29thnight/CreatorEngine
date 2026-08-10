#pragma once
#include "DLLAcrossSingleton.h"
#include "Core.ThreadPool.h"

// ── 엔진 공용 작업자 풀 (PHASE 4-3 슬라이스 3) ──
//
// 이 풀은 SceneManager가 new로 하나 만들어 쥐고 있었다. 그런데 쓰는 쪽은
// 씬만이 아니다 — 자산 시스템(층 3)도 번들을 병렬로 읽는 데 썼고, 그래서
// 렌더 계층이 게임플레이 매니저 헤더를 여는 이유가 됐다.
//
// 풀의 타입은 처음부터 층 1 것이었다(Core.ThreadPool.h). 인스턴스만 층 4에
// 얹혀 있었을 뿐이다. 그것을 타입 옆으로 내려놓는다.
//
// 수명은 바꾸지 않았다. SceneManager::ManagerInitialize가 Startup을,
// 그 소멸자가 Shutdown을 부른다 — 전에 new/delete 하던 바로 그 자리다.
class WorkerPool : public DLLCore::Singleton<WorkerPool>
{
private:
	friend class DLLCore::Singleton<WorkerPool>;

	WorkerPool() = default;
	~WorkerPool() { Shutdown(); }

public:
	using Pool = ThreadPool<std::function<void()>>;

	void Startup()
	{
		if (nullptr == m_pool)
		{
			m_pool = new Pool();
		}
	}

	void Shutdown()
	{
		delete m_pool;      // ThreadPool 소멸자가 작업자 스레드를 거둔다
		m_pool = nullptr;
	}

	bool IsRunning() const noexcept { return nullptr != m_pool; }

	// 풀이 서기 전에 일이 들어오면 조용히 버리는 대신 그 자리에서 처리한다.
	// 전에는 nullptr 역참조였다 — 순서가 어긋나면 터졌다는 뜻이다.
	template<typename F>
	void Enqueue(F&& task)
	{
		if (nullptr == m_pool)
		{
			task();
			return;
		}
		m_pool->Enqueue(std::forward<F>(task));
	}

	void NotifyAllAndWait()
	{
		if (nullptr != m_pool)
		{
			m_pool->NotifyAllAndWait();
		}
	}

private:
	Pool* m_pool{ nullptr };
};

static auto WorkerPools = WorkerPool::GetInstance();
