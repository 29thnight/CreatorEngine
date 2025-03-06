#include "Core.Thread.h"

namespace Thread
{
	// ThreadPool 생성자
	ThreadPool::ThreadPool(size_t numThreads)
	{
		for (size_t i = 0; i < numThreads; ++i)
		{
			mThreads.emplace_back([this]() { WorkerLoop(); });
		}
	}

	// ThreadPool 소멸자
	ThreadPool::~ThreadPool()
	{
		{
			std::unique_lock<std::mutex> lock(mMutex);
			mStopThreads = true;
		}
		mSignal.notify_all();

		for (std::thread& thread : mThreads)
		{
			if (thread.joinable())
				thread.join();
		}
	}

	// AddTask 함수 구현
	template<typename T>
	auto ThreadPool::AddTask(T task) -> Task<decltype(task())>
	{
		using TaskReturnType = decltype(task());
		auto pTask = std::make_shared<std::packaged_task<TaskReturnType()>>(std::move(task));
		{
			std::unique_lock<std::mutex> lock(mMutex);
			mTaskQueue.emplace([pTask]() { (*pTask)(); });
		}
		mSignal.notify_one();
		return Task<TaskReturnType>(pTask);
	}

	// Worker 스레드 루프
	void ThreadPool::WorkerLoop()
	{
		while (true)
		{
			std::function<void()> task;
			{
				std::unique_lock<std::mutex> lock(mMutex);
				mSignal.wait(lock, [this]() { return mStopThreads || !mTaskQueue.empty(); });

				if (mStopThreads && mTaskQueue.empty())
					return;

				task = std::move(mTaskQueue.front());
				mTaskQueue.pop();
			}

			// 작업 실행
			task();
		}
	}
}
