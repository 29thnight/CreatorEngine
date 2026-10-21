#pragma once
#include <cstdint>

// Translation jobs are backend work delegated by the RHI submission owner.
class RHIBackendExecutionScope final
{
  public:
    RHIBackendExecutionScope() { ++s_depth; }
    ~RHIBackendExecutionScope() { --s_depth; }
    RHIBackendExecutionScope(const RHIBackendExecutionScope&) = delete;
    RHIBackendExecutionScope& operator=(const RHIBackendExecutionScope&) = delete;
    static bool IsActive() { return s_depth != 0; }

  private:
    inline static thread_local uint32_t s_depth{};
};
