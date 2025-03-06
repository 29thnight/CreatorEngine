#pragma once
#include <condition_variable>
#include <future>
#include <functional>
#include <queue>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include "ClassProperty.h"

namespace Thread
{
	// Task 클래스 정의
	template<typename ResultType>
	class Task
	{
	public:
		explicit Task(std::shared_ptr<std::packaged_task<ResultType()>> task)
			: mTask(std::move(task)), mFuture(mTask->get_future()) {
		}

		// then 메서드 정의
		template<typename NextFunc>
		auto then(NextFunc&& nextFunc) -> Task<decltype(nextFunc(std::declval<ResultType>()))>
		{
			using NextResultType = decltype(nextFunc(std::declval<ResultType>()));

			// 다음 작업 생성 : 현재 작업의 결과를 받아 다음 작업을 Chain 형식으로 연결
			auto nextTask = std::make_shared<std::packaged_task<NextResultType()>>(
				[prevFuture = std::move(mFuture), nextFunc = std::forward<NextFunc>(nextFunc)]() mutable
				{
					return nextFunc(prevFuture.get());
				});

			return Task<NextResultType>(nextTask);
		}

		// 결과 Future 반환
		std::future<ResultType> get_future()
		{
			return mTask->get_future();
		}

		// 작업 실행
		void execute()
		{
			(*mTask)();
		}

	private:
		std::shared_ptr<std::packaged_task<ResultType()>> mTask;
		std::future<ResultType> mFuture;
	};

	// ThreadPool 클래스 정의(싱글톤) : 외부생성 불가, 생성자 및 소멸자 private
	class ThreadPool final : public Singleton<ThreadPool>
	{
	private:
		friend class Singleton;
		template<typename T>
		friend auto CreateTask(T task) -> Task<decltype(task())>;

		explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency() - 2);
		~ThreadPool() override;

		template<typename T>
		auto AddTask(T task) -> Task<decltype(task())>;
		void WorkerLoop();

	private:
		std::vector<std::thread> mThreads;
		std::condition_variable mSignal;
		std::mutex mMutex;
		bool mStopThreads = false;

		std::queue<std::function<void()>> mTaskQueue;
	};
	// ThreadPool 접근 및 작업 생성 함수
	template<typename T>
	inline auto CreateTask(T task) -> Task<decltype(task())>
	{
		return ThreadPool::GetInstance()->AddTask(std::move(task));
	}
}
