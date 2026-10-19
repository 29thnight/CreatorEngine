#pragma once

#include "../../../Utility_Framework/JobScheduler.h"
#include "../../../EngineDiagnostics/ProfileScope.h"

// 합류 지원은 소유자 스레드에서도 풀 작업을 실행한다. 같은 mutex의 재진입은
// try_lock 자체의 선행조건을 어기므로, 잠그기 전에 스택의 선언 범위를 확인한다.
class render_graph_declaration_lock final
{
  public:
    explicit render_graph_declaration_lock(std::mutex& mutex) : lock_(mutex, std::defer_lock)
    {
        for (auto* scope = current_; scope; scope = scope->previous_)
        {
            if (scope->lock_.mutex() == &mutex)
            {
                return;
            }
        }
        if (thread_pool::is_worker_thread())
        {
            if (!lock_.try_lock())
            {
                return;
            }
        }
        else
        {
            lock_.lock();
        }
        previous_ = current_;
        current_ = this;
    }

    ~render_graph_declaration_lock()
    {
        if (lock_.owns_lock())
        {
            current_ = previous_;
        }
    }

    render_graph_declaration_lock(const render_graph_declaration_lock&) = delete;
    render_graph_declaration_lock& operator=(const render_graph_declaration_lock&) = delete;
    bool owns_lock() const { return lock_.owns_lock(); }

  private:
    inline static thread_local render_graph_declaration_lock* current_{};
    std::unique_lock<std::mutex> lock_;
    render_graph_declaration_lock* previous_{};
};

// 선언자가 출력 바인딩을 준비하는 동안 독립적인 업로드 데이터를 채운다.
// callable과 그 입력/출력은 이 객체보다 먼저 선언해 예외 경로에서도 합류 뒤에 파괴한다.
class render_graph_declaration_task final
{
  public:
    explicit render_graph_declaration_task(job_scheduler& scheduler = ce::get_job_scheduler()) : scheduler_(scheduler)
    {
    }

    ~render_graph_declaration_task()
    {
        if (!work_.valid())
        {
            return;
        }
        try
        {
            // 오류 정리에서 계측 등록이나 지원 실행을 다시 시작하지 않는다.
            // 계측 자체가 실패한 경우에도 callable과 저장소의 수명을 끝까지 지킨다.
            work_.wait();
        }
        catch (...)
        {
            // 선언자의 원래 오류를 보존하되, 수락된 작업의 참조는 먼저 회수한다.
        }
    }

    render_graph_declaration_task(const render_graph_declaration_task&) = delete;
    render_graph_declaration_task& operator=(const render_graph_declaration_task&) = delete;

    template<class F>
    void start(std::size_t count, bool parallel, F& task)
    {
        if (work_.valid())
        {
            throw std::logic_error("A declaration task must join before another admission.");
        }
        if (!count)
        {
            return;
        }
        if (!parallel || thread_pool::is_worker_thread() || !scheduler_.is_running())
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                task(index);
            }
            return;
        }
#if !CE_SHIPPING
        cpu_context_ = ce::current_cpu_context;
#endif
        // 항목별 std::function/vector를 만들지 않는다. 한 번 수락한 묶음은
        // 합류 전까지 살아 있는 callable을 빌리고 기존 풀의 워커를 사용한다.
        work_ = scheduler_.submit_indexed(count,
                                          [this, callable = &task](std::size_t index)
                                          {
                                              ce::profile_context_scope context{cpu_context_};
                                              (*callable)(index);
                                          });
    }

    void join()
    {
        if (!work_.valid())
        {
            return;
        }
        const job_handle work = std::exchange(work_, {});
        try
        {
            ce::profile_scope profile{ce::marker<"RenderGraphDeclareJoin">()};
            work.wait_assisting();
        }
        catch (...)
        {
            // 계측 생성이나 합류 지원의 오류도 수락된 작업을 버리게 해서는 안 된다.
            // 작업의 오류는 완료 뒤 전달되므로, 원래 오류를 보존하며 먼저 회수한다.
            try
            {
                work.wait();
            }
            catch (...)
            {
            }
            throw;
        }
    }

  private:
    job_scheduler& scheduler_;
    job_handle work_;
    ce::cpu_span_context cpu_context_{};
};
