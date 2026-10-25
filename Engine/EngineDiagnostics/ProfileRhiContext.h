#pragma once

#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace ce
{
    // 값만 넘긴다. CPU task/session, GPU submission 번호와 recording identity는
    // 서로 다른 축이며, 없는 값을 현재 프레임이나 마지막 제출로 채우지 않는다.
    struct profile_rhi_context
    {
        std::uint64_t recording_id = 0;
        std::uint64_t source_engine_frame = 0;
        std::uint64_t frame_id = 0;
        std::uint64_t view_id = 0;
        std::uint64_t device_incarnation = 0;
        std::uint64_t backend_generation = 0;
        std::uint32_t queue_id = 0;
        std::uint32_t target_index = UINT32_MAX;
        std::uint8_t queue_kind = 0;
        bool source_engine_frame_available = false;

        bool valid() const
        {
            return recording_id != 0 && queue_kind <= 2 &&
                   (source_engine_frame_available || source_engine_frame == 0) &&
                   ((device_incarnation == 0) == (queue_id == 0));
        }

        bool operator==(const profile_rhi_context&) const = default;
    };

#if CE_SHIPPING
    inline constexpr profile_rhi_context current_rhi_context{};

    class profile_rhi_context_scope
    {
      public:
        explicit profile_rhi_context_scope(profile_rhi_context) noexcept {}
        profile_rhi_context_scope(const profile_rhi_context_scope&) = delete;
        profile_rhi_context_scope& operator=(const profile_rhi_context_scope&) = delete;
    };
#else
    inline thread_local profile_rhi_context current_rhi_context{};

    class profile_rhi_context_scope
    {
      public:
        explicit profile_rhi_context_scope(profile_rhi_context value) noexcept : m_previous(current_rhi_context)
        {
            current_rhi_context = value;
        }

        ~profile_rhi_context_scope() { current_rhi_context = m_previous; }
        profile_rhi_context_scope(const profile_rhi_context_scope&) = delete;
        profile_rhi_context_scope& operator=(const profile_rhi_context_scope&) = delete;

      private:
        profile_rhi_context m_previous;
    };
#endif
} // namespace ce
