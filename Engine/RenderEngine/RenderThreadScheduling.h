#pragma once
#include <cstdint>

namespace ce::render
{
    enum class thread_role
    {
        render,
        submission,
        translation
    };

    // Owned by the thread entry scope. Registration and release use that same thread.
    // Translation and general workers keep the normal OS scheduling class.
    class scheduling_scope final
    {
      public:
        explicit scheduling_scope(thread_role role) noexcept;
        ~scheduling_scope();
        scheduling_scope(const scheduling_scope&) = delete;
        scheduling_scope& operator=(const scheduling_scope&) = delete;
        scheduling_scope(scheduling_scope&&) = delete;
        scheduling_scope& operator=(scheduling_scope&&) = delete;
        bool active() const noexcept { return task_ != nullptr; }
        std::uint32_t error() const noexcept { return error_; }
        bool reset() noexcept;

      private:
        void* task_{};
        std::uint32_t error_{};
    };
} // namespace ce::render
