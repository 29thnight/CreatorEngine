// Synthetic dispatcher graphs supplement the actual SDK simulation gates.
#include <chrono>
#include <functional>
#include <semaphore>

extern "C" bool ce_physics_wake_probe()
{
    using namespace std::chrono_literals;
    struct task final : physx::PxBaseTask
    {
        std::function<void()> on_run, on_release;
        std::atomic<int> runs{0}, releases{0};

        void run() override { ++runs; if (on_run) on_run(); }
        void release() override { ++releases; if (on_release) on_release(); }
        const char* getName() const override { return "WakeProbe"; }
        void addReference() override {}
        void removeReference() override {}
        int32_t getReference() const override { return 1; }
    };

    for (auto workers : {1u, 2u, 8u})
    {
        for (auto capacity : {1u, 4u})
        {
            std::array<task, 256> chain;
            ce::physics::task_dispatcher dispatcher(workers, capacity, {});
            for (std::size_t i = 0; i + 1 < chain.size(); ++i)
                chain[i].on_release = [&, i] { dispatcher.submitTask(chain[i + 1]); };

            dispatcher.submitTask(chain.front());
            dispatcher.drain();
            for (auto& item : chain)
                if (item.runs != 1 || item.releases != 1) return false;
        }
    }

    {
        std::array<task, 65> fanout;
        ce::physics::task_dispatcher dispatcher(2, 1, {});
        fanout[0].on_release = [&] {
            for (auto& item : std::span(fanout).subspan(1)) dispatcher.submitTask(item);
        };

        dispatcher.submitTask(fanout[0]);
        dispatcher.drain();
        for (auto& item : fanout)
            if (item.runs != 1 || item.releases != 1) return false;
    }

    {
        task parent, child;
        std::binary_semaphore completed(0);
        std::atomic<bool> nested_completed{false};
        ce::physics::task_dispatcher dispatcher(2, 1, {});
        child.on_release = [&] { completed.release(); };
        parent.on_run = [&] {
            dispatcher.submitTask(child);
            nested_completed = completed.try_acquire_for(2s);
        };

        dispatcher.submitTask(parent);
        dispatcher.drain();
        if (!nested_completed || parent.runs != 1 || child.runs != 1 ||
            parent.releases != 1 || child.releases != 1) return false;
    }

    return true;
}
