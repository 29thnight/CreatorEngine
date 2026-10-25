#pragma once

#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Concurrency.Jobs.h"
#else
import Creator.Concurrency.Jobs;
#endif
#include "../EngineDiagnostics/ProfileScope.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace material_graph
{
    // Declare after every referenced local. Every accepted task is drained before
    // those locals can be destroyed, including submission failures and early returns.
    class ScenePreparationTasks final
    {
      public:
        ScenePreparationTasks(job_scheduler& scheduler, bool parallel) : scheduler_(scheduler), parallel_(parallel) {}
        ~ScenePreparationTasks()
        {
            for (std::size_t index = 0; index < count_; ++index)
            {
                try
                {
                    if (work_[index].valid())
                    {
                        work_[index].wait_assisting();
                    }
                }
                catch (...)
                {
                }
            }
        }
        ScenePreparationTasks(const ScenePreparationTasks&) = delete;
        ScenePreparationTasks& operator=(const ScenePreparationTasks&) = delete;

        template<class F>
        bool Add(ce::marker_id marker, F&& task, std::string& error)
        {
            try
            {
                return AddIndexed(
                    marker, 1, [task = std::forward<F>(task)](std::size_t, std::string& failure) mutable
                    { return task(failure); }, error);
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                return false;
            }
        }
        template<class F>
        bool AddIndexed(ce::marker_id marker, std::size_t count, F&& task, std::string& error)
        {
            try
            {
                if (!count)
                {
                    return true;
                }
                ce::cpu_span_context cpuContext{};
#if !CE_SHIPPING
                cpuContext = ce::current_cpu_context;
#endif
                // 호출자를 여기서 한 번 더 type-erasure하면 캡처가 큰 작업마다 별도 할당이 붙는다.
                // scheduler에 넘길 때만 소유 callback으로 만들고, inline 경로는 직접 호출한다.
                auto execute = [marker, cpuContext, task = std::forward<F>(task)](std::size_t index) mutable
                {
                    ce::profile_context_scope context{cpuContext};
                    ce::profile_scope profile{marker};
                    std::string failure;
                    if (!task(index, failure))
                    {
                        throw std::runtime_error(failure.empty() ? "Scene preparation task failed." : failure);
                    }
                };
                if (!parallel_)
                {
                    for (std::size_t index = 0; index < count; ++index)
                    {
                        execute(index);
                    }
                }
                else
                {
                    if (count_ == work_.size())
                    {
                        throw std::runtime_error("Scene preparation task admission limit exceeded.");
                    }
                    // Reserve the owning slot before admission; no container growth
                    // or allocation can lose the handle of already accepted work.
                    auto& slot = work_[count_++];
                    // A leaf preparation has no dependency graph to construct.
                    // 각 index의 결과와 실패 문자열은 호출자가 서로 다른 저장소에 둔다.
                    // callback 하나를 공유하므로 작은 leaf마다 별도 admission을 만들지 않는다.
                    slot = scheduler_.submit_indexed(count, std::move(execute));
                }
                return true;
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                return false;
            }
        }
        bool Join(std::string& error)
        {
            ce::profile_scope profile{ce::marker<"MaterialSceneJoinAssist">()};
            bool success = true;
            for (std::size_t index = 0; index < count_; ++index)
            {
                try
                {
                    if (work_[index].valid())
                    {
                        work_[index].wait_assisting();
                    }
                }
                catch (const std::exception& failure)
                {
                    if (success)
                    {
                        error = failure.what();
                    }
                    success = false;
                }
                catch (...)
                {
                    if (success)
                    {
                        error = "Unknown scene preparation task failure.";
                    }
                    success = false;
                }
                work_[index] = {};
            }
            count_ = 0;
            return success;
        }

      private:
        job_scheduler& scheduler_;
        bool parallel_{};
        std::array<job_handle, 16> work_;
        std::size_t count_{};
    };
} // namespace material_graph
