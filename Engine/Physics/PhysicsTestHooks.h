#pragma once
#if defined(CE_PHYSICS_TESTING)
#include <atomic>
namespace ce::physics::test
{
// Standalone verifier only. Not compiled into the engine's Physics project.
enum class failure_point
{
    none,
    foundation,
    physics,
    dispatcher,
    scene,
    allocation,
    cuda_context,
    gpu_scene,
    body_shape,
    body_generation_limit,
    snapshot_allocation,
    contact_storage_limit,
    character_manager,
    character_creation,
    character_generation_limit,
    step_fetch
};

inline std::atomic<failure_point> next_failure{failure_point::none};
inline bool consume(failure_point point)
{
    return next_failure.compare_exchange_strong(point, failure_point::none);
}

} // namespace ce::physics::test

#endif
