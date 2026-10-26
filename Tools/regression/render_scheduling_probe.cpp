#include "../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#include "../../Engine/RenderEngine/RenderThreadScheduling.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace
{
    void Check(bool value)
    {
        if (!value)
        {
            throw std::runtime_error("Render scheduling lifetime check failed");
        }
    }
    void Verify()
    {
        const auto before = GetThreadPriority(GetCurrentThread());
        for (unsigned pass = 0; pass < 16; ++pass)
        {
            ce::render::scheduling_scope task{pass & 1 ? ce::render::thread_role::render
                                                       : ce::render::thread_role::submission};
            Check(task.active() && !task.error());
            Check(task.reset() && !task.active() && task.reset());
            Check(GetThreadPriority(GetCurrentThread()) == before);
        }
        try
        {
            const ce::render::scheduling_scope task{ce::render::thread_role::render};
            Check(task.active());
            throw 1;
        }
        catch (int)
        {
        }
        Check(GetThreadPriority(GetCurrentThread()) == before);
        const ce::render::scheduling_scope translation{ce::render::thread_role::translation};
        Check(!translation.active() && !translation.error());
        Check(GetThreadPriority(GetCurrentThread()) == before);
    }
} // namespace
int main()
{
    static_assert(!std::is_copy_constructible_v<ce::render::scheduling_scope>);
    static_assert(!std::is_move_constructible_v<ce::render::scheduling_scope>);
    try
    {
        Verify();
        std::array<bool, 4> passed{};
        std::array<std::thread, 4> threads;
        for (size_t i = 0; i < threads.size(); ++i)
        {
            threads[i] = std::thread(
                [&, i]
                {
                    try
                    {
                        Verify();
                        passed[i] = true;
                    }
                    catch (...)
                    {
                    }
                });
        }
        for (auto& thread : threads)
        {
            thread.join();
        }
        for (const bool value : passed)
        {
            Check(value);
        }
        std::cout << "RENDER_SCHEDULING_OK registrations=85 threads=5 priorityRestored=1 idempotent=1 exception=1 "
                     "translationNormal=1\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
