#include "RenderThreadScheduling.h"
#if defined(_WIN32)
#if defined(CE_CONTRACT_STANDALONE)
#include "../Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#endif

namespace ce::render
{
#if defined(_WIN32)
    namespace
    {
        struct scheduling_api
        {
            using register_task = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
            using release_task = BOOL(WINAPI*)(HANDLE);
            using set_priority = BOOL(WINAPI*)(HANDLE, int);
            register_task acquire{};
            release_task release{};
            set_priority prioritize{};
            DWORD error{};
            scheduling_api() noexcept
            {
                // Process lifetime: the RHI service can outlive individual renderers.
                const auto module = LoadLibraryExW(L"avrt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (!module)
                {
                    error = GetLastError();
                    return;
                }
                acquire = reinterpret_cast<register_task>(GetProcAddress(module, "AvSetMmThreadCharacteristicsW"));
                release = reinterpret_cast<release_task>(GetProcAddress(module, "AvRevertMmThreadCharacteristics"));
                prioritize = reinterpret_cast<set_priority>(GetProcAddress(module, "AvSetMmThreadPriority"));
                if (!acquire || !release || !prioritize)
                {
                    error = ERROR_PROC_NOT_FOUND;
                }
            }
        };
        const scheduling_api& api() noexcept
        {
            static const scheduling_api value;
            return value;
        }
    } // namespace
#endif

    scheduling_scope::scheduling_scope(thread_role role) noexcept
    {
#if defined(_WIN32)
        const auto* name = role == thread_role::render       ? L"Creator.Render"
                           : role == thread_role::submission ? L"Creator.RHI.Submission"
                                                             : L"Creator.RHI.Translation";
        SetThreadDescription(GetCurrentThread(), name);
        if (role == thread_role::translation)
        {
            return;
        }
        const auto& functions = api();
        if (functions.error)
        {
            error_ = functions.error;
            return;
        }
        DWORD index{};
        task_ = functions.acquire(L"Games", &index);
        if (!task_)
        {
            error_ = GetLastError();
        }
        else if (!functions.prioritize(task_, 1)) // AVRT_PRIORITY_HIGH within the Games task.
        {
            error_ = GetLastError();
            functions.release(task_);
            task_ = nullptr;
        }
#else
        (void)role;
#endif
    }
    scheduling_scope::~scheduling_scope()
    {
        reset();
    }
    bool scheduling_scope::reset() noexcept
    {
#if defined(_WIN32)
        if (task_)
        {
            if (!api().release(task_))
            {
                error_ = GetLastError();
                return false;
            }
            task_ = nullptr;
        }
#endif
        return true;
    }
} // namespace ce::render
