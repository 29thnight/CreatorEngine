#pragma once

#include "../Utility_Framework/JobScheduler.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include "../Utility_Framework/StandardLibrary.h"

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

        bool Add(ce::marker_id marker, std::function<bool(std::string&)> task, std::string& error)
        {
            try
            {
                ce::cpu_span_context cpuContext{};
#if !CE_SHIPPING
                cpuContext = ce::current_cpu_context;
#endif
                auto execute = [marker, cpuContext, task = std::move(task)]
                {
                    ce::profile_context_scope context{cpuContext};
                    ce::profile_scope profile{marker};
                    std::string failure;
                    if (!task(failure))
                    {
                        throw std::runtime_error(failure.empty() ? "Scene preparation task failed." : failure);
                    }
                };
                if (!parallel_)
                {
                    execute();
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
                    slot = scheduler_.submit_indexed(1, [execute = std::move(execute)](std::size_t) { execute(); });
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
