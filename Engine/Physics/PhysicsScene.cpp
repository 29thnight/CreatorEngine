#include "PhysicsScene.h"
#include "PhysicsTestHooks.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include <physx/PxPhysicsAPI.h>
#include <physx/gpu/PxGpu.h>
#include <physx/characterkinematic/PxControllerManager.h>
#include <physx/characterkinematic/PxCapsuleController.h>
#pragma comment(lib, "PhysXCharacterKinematic_static_64.lib")
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <concepts>
#include <cstdio>
#include <format>
#include <limits>
#include <mutex>
#include <ranges>
#include <thread>
#include <vector>
#include <array>
#include <mdspan>
#include <mathematics/views.hpp>

namespace ce::physics
{
namespace
{
template<class T>
concept sdk_resource = requires(T& value) { value.release(); };

template<sdk_resource T>
struct release_sdk
{
    void operator()(T* value) const noexcept
    {
        if (value)
            value->release();
    }
};

template<sdk_resource T>
using sdk_owner = std::unique_ptr<T, release_sdk<T>>;

class sdk_errors final : public physx::PxErrorCallback
{
  public:
    std::atomic<std::uint32_t> count{0}, last{0};

    void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
    {
        last.store(static_cast<std::uint32_t>(code), std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[Physics SDK] %s (%s:%d)\n", message, file, line);
    }
};

struct sdk_runtime;
struct runtime_registry
{
    std::mutex mutex;
    std::condition_variable released;
    std::weak_ptr<sdk_runtime> weak;
    bool live = false;
};

struct sdk_runtime
{
    physx::PxDefaultAllocator allocator;
    sdk_errors errors;
    sdk_owner<physx::PxFoundation> foundation;
    sdk_owner<physx::PxPhysics> physics;
    runtime_registry* registry = nullptr;

    ~sdk_runtime()
    {
        if (!registry)
            return;

        std::lock_guard lock(registry->mutex);
        physics.reset();
        foundation.reset();
        registry->live = false;
        registry->released.notify_all();
    }
};

// PhysX Foundation is process-wide. Shared lifetime is an implementation
// constraint, not a global engine API. Scenes keep the SDK alive independently.
result<std::shared_ptr<sdk_runtime>> acquire_runtime(std::source_location location)
{
    // Registry metadata has process lifetime; SDK allocations do not.
    static auto* registry = new runtime_registry;
    std::unique_lock lock(registry->mutex);
    // A final shared_ptr can expire before its deleter releases Foundation.
    // Wait for that teardown instead of creating a second Foundation.
    for (;;)
    {
        if (auto runtime = registry->weak.lock())
            return runtime;
        if (!registry->live)
            break;
        registry->released.wait(lock);
    }

    auto runtime = std::make_shared<sdk_runtime>();
    runtime->foundation.reset(PxCreateFoundation(PX_PHYSICS_VERSION, runtime->allocator, runtime->errors));
    if (!runtime->foundation)
        return std::unexpected(error{error_code::backend_initialization, runtime->errors.last.load(),
                                     "PhysX Foundation creation failed", location});
#if defined(CE_PHYSICS_TESTING)
    if (test::consume(test::failure_point::foundation))
        return std::unexpected(error{error_code::backend_initialization, 0, "Injected Foundation failure", location});
#endif
    runtime->physics.reset(
        PxCreatePhysics(PX_PHYSICS_VERSION, *runtime->foundation, physx::PxTolerancesScale{}, false, nullptr));
    if (!runtime->physics)
        return std::unexpected(error{error_code::backend_initialization, runtime->errors.last.load(),
                                     "PhysX SDK creation failed", location});
#if defined(CE_PHYSICS_TESTING)
    if (test::consume(test::failure_point::physics))
        return std::unexpected(error{error_code::backend_initialization, 0, "Injected SDK failure", location});
#endif

    runtime->registry = registry;
    registry->weak = runtime;
    registry->live = true;
    registry->released.notify_all();
    return runtime;
}

class task_dispatcher final : public physx::PxCpuDispatcher
{
  public:
    task_dispatcher(std::uint32_t count, std::uint32_t capacity, scene_id identity)
        : m_queue(capacity), m_identity(identity)
    {
        m_workers.reserve(count);
        for (auto index : std::views::iota(0u, count))
            m_workers.emplace_back([this, index, identity](std::stop_token stop) {
#if !CE_SHIPPING
                const bool registered = ce::profiler().is_initialized();
                if (registered)
                    ce::profiler().register_thread(std::format("[Physics {} Worker {}]", identity.value, index).c_str(),
                                                   ce::track_kind::physics_worker, index);
#endif
                bool active_batch = false;

                for (;;)
                {
                    task_entry task;
                    {
                        std::unique_lock lock(m_mutex);
                        if (m_size == 0 && active_batch)
                        {
#if !CE_SHIPPING
                            // Keep this batch counted until publication finishes.
                            // Only collecting batches can obstruct remaining SDK work.
                            const bool concurrentPublication = m_outstanding != 0 &&
                                ce::profiler().state() == ce::recorder_state::recording;
                            if (concurrentPublication)
                                lock.unlock();

                            ce::profiler().publish_thread();

                            if (concurrentPublication)
                                lock.lock();
#endif
                            active_batch = false;
                            --m_active_batches;

                            if (m_outstanding == 0 && m_active_batches == 0)
                                m_idle.notify_all();
                        }

                        if (!m_wake.wait(lock, stop, [this] { return m_size != 0; }))
                            break;

                        if (!active_batch)
                        {
                            active_batch = true;
                            ++m_active_batches;
                        }

                        task = m_queue[m_head];
                        m_head = (m_head + 1) % m_queue.size();
                        --m_size;
                    }
                    execute(task);
                }
#if !CE_SHIPPING
                if (registered)
                    ce::profiler().unregister_thread();
#endif
            });
    }

    ~task_dispatcher() override
    {
        drain();
        for (auto& worker : m_workers)
            worker.request_stop();
        m_wake.notify_all();
        m_workers.clear(); // Join before mutex/queue/profiler registration teardown.
    }

    void submitTask(physx::PxBaseTask& task) override
    {
        const task_entry entry{&task,
                               {m_identity.value, current_tick.load(std::memory_order_relaxed),
                                next_task.fetch_add(1, std::memory_order_relaxed)}};
        if (entry.context.task == 0 || entry.context.task == UINT64_MAX)
            std::terminate(); // Dispatcher cannot reject an SDK dependency or reuse its identity.

        ce::profile_context_scope context{entry.context};
        ce::profile_instant(ce::marker<"Physics.TaskSubmit">());
        submitted.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(m_mutex);
            ++m_outstanding;
            if (m_size < m_queue.size())
            {
                m_queue[(m_head + m_size) % m_queue.size()] = entry;
                ++m_size;
                m_wake.notify_one();
                return;
            }
        }
        // Bounded queue saturation cannot discard an SDK dependency or allocate.
        inline_tasks.fetch_add(1, std::memory_order_relaxed);
        execute(entry);
    }

    physx::PxU32 getWorkerCount() const override { return static_cast<physx::PxU32>(m_workers.size()); }

    void drain()
    {
        std::unique_lock lock(m_mutex);
        m_idle.wait(lock, [this] { return m_outstanding == 0 && m_active_batches == 0; });
    }

    std::atomic<std::uint64_t> current_tick{0};
    std::atomic<std::uint64_t> submitted{0}, completed{0}, inline_tasks{0};

  private:
    struct task_entry
    {
        physx::PxBaseTask* task = nullptr;
        ce::cpu_span_context context;
    };

    void execute(const task_entry& entry)
    {
        ce::profile_context_scope context{entry.context};
        {
            ce::profile_scope scope{ce::marker<"Physics.PhysXTask">()};
            entry.task->run();
            entry.task->release(); // SDK task ownership ends here, including its dependency release.
        }
        ce::profile_instant(ce::marker<"Physics.TaskComplete">());

        std::lock_guard lock(m_mutex);
        completed.fetch_add(1, std::memory_order_relaxed);
        --m_outstanding;
        if (m_outstanding == 0 && m_active_batches == 0)
            m_idle.notify_all();
    }

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::condition_variable m_idle;
    std::vector<task_entry> m_queue;
    scene_id m_identity;
    std::atomic<std::uint64_t> next_task{1};
    std::size_t m_head = 0, m_size = 0, m_outstanding = 0;
    // drain includes each worker's final publication, not only SDK task release.
    std::size_t m_active_batches = 0;
    std::vector<std::jthread> m_workers;
};

result<scene_id> allocate_scene_id(std::source_location location)
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    do
    {
        if (value == (std::numeric_limits<std::uint64_t>::max)())
            return std::unexpected(error{error_code::capacity_exceeded, 0, "Scene identity exhausted", location});
    } while (!next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));
    return scene_id{value};
}

bool finite(math::vector3 value)
{
    return std::ranges::all_of(math::components(value), [](float component) { return std::isfinite(component); });
}

// Reject point clouds without three-dimensional volume before SDK tolerance
// handling can inflate them into a different shape. All tests use relative units.
bool convex_has_volume(std::span<const math::vector3> points)
{
    using vector = std::array<double, 3>;
    const auto first = math::components(points.front());
    const auto offset = [&](math::vector3 point) -> vector {
        const auto value = math::components(point);
        return {double(value[0]) - first[0], double(value[1]) - first[1], double(value[2]) - first[2]};
    };
    const auto dot = [](const vector& a, const vector& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const auto cross = [](const vector& a, const vector& b) -> vector {
        return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };

    vector axis{};
    double lengthSquared = 0;
    for (const auto point : points)
    {
        const auto value = offset(point);
        const double length = dot(value, value);
        if (length > lengthSquared)
        {
            lengthSquared = length;
            axis = value;
        }
    }
    if (lengthSquared == 0)
        return false;

    vector normal{};
    double areaSquared = 0;
    for (const auto point : points)
    {
        const auto value = cross(axis, offset(point));
        const double area = dot(value, value);
        if (area > areaSquared)
        {
            areaSquared = area;
            normal = value;
        }
    }
    if (areaSquared <= lengthSquared * lengthSquared * 1e-12)
        return false;

    return std::ranges::any_of(points, [&](const auto point) {
        const double volume = dot(normal, offset(point));
        return volume * volume > areaSquared * lengthSquared * 1e-12;
    });
}

// SDK defaults assume metre-sized hulls. Thin assets need an area threshold based
// on their own extent; authoring coordinates and the zero-area check stay intact.
void configure_convex_area(physx::PxCookingParams& params, std::span<const math::vector3> points)
{
    const auto first = math::components(points.front());
    std::array<double, 3> low{first[0], first[1], first[2]}, high = low;
    for (const auto point : points)
    {
        const auto components = math::components(point);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            low[axis] = (std::min)(low[axis], double(components[axis]));
            high[axis] = (std::max)(high[axis], double(components[axis]));
        }
    }

    const double extent = (std::max)({high[0] - low[0], high[1] - low[1], high[2] - low[2]});
    params.areaTestEpsilon = static_cast<float>(
        std::clamp(extent * extent * 1e-6, double(std::numeric_limits<float>::min()), double(params.areaTestEpsilon)));
}

bool valid_pose(const pose& value)
{
    const auto& q = value.rotation;
    const float length = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return finite(value.position) && std::isfinite(length) && std::abs(length - 1.f) < 1e-4f;
}

physx::PxVec3 sdk(math::vector3 value)
{
    return {value.x, value.y, value.z};
}

math::vector3 engine(physx::PxVec3 value)
{
    return {value.x, value.y, value.z};
}

physx::PxTransform sdk(const pose& value)
{
    const auto& q = value.rotation;
    return {sdk(value.position), physx::PxQuat{q.x, q.y, q.z, q.w}};
}

pose engine(const physx::PxTransform& value)
{
    return {engine(value.p), {value.q.x, value.q.y, value.q.z, value.q.w}};
}

// Buffers return only after the last strong snapshot reference disappears.
// Each acquisition gets a fresh control block: an old weak_ptr cannot lock a reused buffer.
class snapshot_pool final : public std::enable_shared_from_this<snapshot_pool>
{
  public:
    explicit snapshot_pool(std::uint32_t capacity) : m_capacity(capacity) { m_free.reserve(capacity); }

    result<std::shared_ptr<tick_snapshot>> acquire(std::source_location location)
    {
        std::unique_ptr<tick_snapshot> buffer;
        {
            std::lock_guard lock(m_mutex);
            if (!m_free.empty())
            {
                buffer = std::move(m_free.back());
                m_free.pop_back();
            }
            else
            {
                if (m_created == m_capacity)
                    return std::unexpected(error{error_code::capacity_exceeded, 0,
                                                 "Snapshot readers exhausted the buffer pool", location});
                buffer = std::make_unique<tick_snapshot>();
                ++m_created;
            }
        }
        const std::weak_ptr<snapshot_pool> pool = shared_from_this();
        return std::shared_ptr<tick_snapshot>(buffer.release(), [pool](tick_snapshot* value) noexcept {
            if (auto owner = pool.lock())
            {
                std::lock_guard lock(owner->m_mutex);
                owner->m_free.emplace_back(value); // Reserved; deleter never allocates.
            }
            else
                delete value; // Snapshots remain owned even after PhysicsScene/pool destruction.
        });
    }

    void statistics(step_statistics& value)
    {
        std::lock_guard lock(m_mutex);
        value.snapshot_buffers = m_created;
        value.snapshot_buffers_in_use = m_created - static_cast<std::uint32_t>(m_free.size());
    }

  private:
    std::mutex m_mutex;
    std::vector<std::unique_ptr<tick_snapshot>> m_free;
    std::uint32_t m_capacity, m_created = 0;
};

class scoped_scene_phase final
{
  public:
    scoped_scene_phase(std::atomic<scene_phase>& state, scene_phase phase) noexcept
        : m_state(state), m_previous(state.exchange(phase, std::memory_order_acq_rel))
    {
    }

    ~scoped_scene_phase() { m_state.store(m_previous, std::memory_order_release); }
    scoped_scene_phase(const scoped_scene_phase&) = delete;
    scoped_scene_phase& operator=(const scoped_scene_phase&) = delete;

  private:
    std::atomic<scene_phase>& m_state;
    scene_phase m_previous;
};

struct shape_identity
{
    body_handle body;
    shape_id shape;
    bool sensor;
};

struct body_record
{
    body_kind kind;
    body_handle handle;
    std::uint64_t changed_tick = 0;
    std::vector<shape_identity> identities;
    std::vector<physx::PxShape*> shapes;
    std::vector<std::shared_ptr<const CollisionGeometry>> assets;
    sdk_owner<physx::PxRigidActor> actor; // Release before its userData and cooked asset owners.
};

struct character_record
{
    character_desc definition;
    character_state state;
    std::uint64_t changed_tick = 0;
    sdk_owner<physx::PxController> controller; // Released before its stable userData.
};

struct character_slot
{
    std::unique_ptr<character_record> record;
    std::uint32_t generation = 1, last_destroyed = 0, next_free = UINT32_MAX;
};

class character_filter final : public physx::PxQueryFilterCallback, public physx::PxControllerFilterCallback
{
  public:
    explicit character_filter(const character_desc& definition) : desc(definition) {}

    physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* shape,
                                          const physx::PxRigidActor*, physx::PxHitFlags&) override
    {
        const auto* identity = static_cast<const shape_identity*>(shape->userData);
        if (!identity || identity->sensor)
            return physx::PxQueryHitType::eNONE; // CCT-vs-CCT uses the separate manager callback.
        const auto data = shape->getSimulationFilterData();
        return (desc.belongs_to & data.word1) && (data.word0 & desc.collides_with) ? physx::PxQueryHitType::eBLOCK
                                                                                   : physx::PxQueryHitType::eNONE;
    }

    physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&, const physx::PxShape*,
                                           const physx::PxRigidActor*) override
    {
        return physx::PxQueryHitType::eBLOCK;
    }

    bool filter(const physx::PxController& a, const physx::PxController& b) override
    {
        const auto* left = static_cast<const character_record*>(a.getUserData());
        const auto* right = static_cast<const character_record*>(b.getUserData());
        return left && right && (left->definition.belongs_to & right->definition.collides_with) &&
               (right->definition.belongs_to & left->definition.collides_with);
    }

  private:
    const character_desc& desc;
};

class event_collector final : public physx::PxSimulationEventCallback
{
  public:
    event_collector(std::uint32_t capacity, const std::vector<std::unique_ptr<body_record>>& retired)
        : m_retired(retired), m_capacity(capacity)
    {
        events.reserve(capacity);
    }

    std::mutex mutex;
    std::vector<collision_event> events;
    step_statistics statistics;

    void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
    void onWake(physx::PxActor**, physx::PxU32) override {}
    void onSleep(physx::PxActor**, physx::PxU32) override {}
    void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, physx::PxU32) override {}

    void onContact(const physx::PxContactPairHeader&, const physx::PxContactPair* pairs, physx::PxU32 count) override
    {
        std::lock_guard lock(mutex);
        for (auto index : std::views::iota(0u, count))
        {
            const auto& pair = pairs[index];
            const auto first = identity(pair.shapes[0], pair.flags.isSet(physx::PxContactPairFlag::eREMOVED_SHAPE_0));
            const auto second = identity(pair.shapes[1], pair.flags.isSet(physx::PxContactPairFlag::eREMOVED_SHAPE_1));
            if (!first || !second)
            {
                ++statistics.unresolved_identities;
                continue;
            }

            collision_event event{};
            event.first = *first;
            event.second = *second;
            std::array<physx::PxContactPairPoint, contact_point_capacity> points;
            event.required_contacts = pair.contactCount;
            auto capacity = static_cast<physx::PxU32>(points.size());
#if defined(CE_PHYSICS_TESTING)
            if (test::consume(test::failure_point::contact_storage_limit))
                capacity = 1;
#endif
            event.contact_count = pair.extractContacts(points.data(), capacity);
            for (auto point : std::views::iota(0u, event.contact_count))
                event.contacts[point] = {engine(points[point].position), engine(points[point].normal),
                                         engine(points[point].impulse), points[point].separation};

            for (const auto [flag, kind] :
                 {std::pair{physx::PxPairFlag::eNOTIFY_TOUCH_FOUND, event_kind::contact_begin},
                  std::pair{physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS, event_kind::contact_persist},
                  std::pair{physx::PxPairFlag::eNOTIFY_TOUCH_LOST, event_kind::contact_end}})
            {
                if (pair.events.isSet(flag))
                {
                    event.kind = kind;
                    append(event);
                }
            }
        }
    }

    void onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count) override
    {
        std::lock_guard lock(mutex);
        for (auto index : std::views::iota(0u, count))
        {
            const auto& pair = pairs[index];
            const auto first =
                identity(pair.triggerShape, pair.flags.isSet(physx::PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER));
            const auto second =
                identity(pair.otherShape, pair.flags.isSet(physx::PxTriggerPairFlag::eREMOVED_SHAPE_OTHER));
            if (!first || !second)
            {
                ++statistics.unresolved_identities;
                continue;
            }

            collision_event event{};
            event.kind = pair.status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND ? event_kind::sensor_enter
                                                                               : event_kind::sensor_exit;
            event.first = *first;
            event.second = *second;
            append(event);
        }
    }

  private:
    std::optional<event_endpoint> identity(const physx::PxShape* shape, bool removed) const noexcept
    {
        if (removed)
        {
            // The actor/shape remains owned through fetch. Compare opaque addresses;
            // never dereference the SDK's removed-shape callback pointer.
            for (const auto& record : m_retired)
                for (auto index : std::views::iota(std::size_t{0}, record->shapes.size()))
                    if (record->shapes[index] == shape)
                    {
                        const auto& value = record->identities[index];
                        return event_endpoint{value.body, value.shape, value.sensor};
                    }
            return std::nullopt;
        }

        if (!shape || !shape->userData)
            return std::nullopt;

        const auto& value = *static_cast<const shape_identity*>(shape->userData);
        return event_endpoint{value.body, value.shape, value.sensor};
    }

    void append(collision_event event) noexcept
    {
        ++statistics.required_events;
        statistics.required_contacts += event.required_contacts;
        if (events.size() == m_capacity)
        {
            ++statistics.dropped_events;
            statistics.dropped_contacts += event.required_contacts;
            return;
        }

        statistics.dropped_contacts += event.required_contacts - event.contact_count;
        // Canonical endpoint order also canonicalizes manifold orientation.
        if (std::tuple{event.second.body, event.second.shape} < std::tuple{event.first.body, event.first.shape})
        {
            std::swap(event.first, event.second);
            for (auto index : std::views::iota(0u, event.contact_count))
            {
                auto& point = event.contacts[index];
                point.normal = {-point.normal.x, -point.normal.y, -point.normal.z};
                point.impulse = {-point.impulse.x, -point.impulse.y, -point.impulse.z};
            }
        }
        events.push_back(event); // Reserved at scene creation; callback path never allocates.
    }

    const std::vector<std::unique_ptr<body_record>>& m_retired;
    std::uint32_t m_capacity;
};

struct body_slot
{
    std::uint32_t generation = 1;
    std::uint32_t last_destroyed = 0;
    std::uint32_t next_free = UINT32_MAX;
    std::unique_ptr<body_record> record;
};

physx::PxFilterFlags simulation_filter(physx::PxFilterObjectAttributes a, physx::PxFilterData fa,
                                       physx::PxFilterObjectAttributes b, physx::PxFilterData fb,
                                       physx::PxPairFlags& flags, const void*, physx::PxU32)
{
    if (!(fa.word0 & fb.word1) || !(fb.word0 & fa.word1))
        return physx::PxFilterFlag::eSUPPRESS;

    flags = physx::PxFilterObjectIsTrigger(a) || physx::PxFilterObjectIsTrigger(b)
                ? physx::PxPairFlag::eTRIGGER_DEFAULT
                : physx::PxPairFlag::eCONTACT_DEFAULT | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND |
                      physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS | physx::PxPairFlag::eNOTIFY_TOUCH_LOST |
                      physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;
    return physx::PxFilterFlag::eDEFAULT;
}

class query_selection final : public physx::PxQueryFilterCallback
{
  public:
    explicit query_selection(const query_filter& value) : filter(value) {}

    physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* shape,
                                          const physx::PxRigidActor*, physx::PxHitFlags&) override
    {
        const auto* identity = static_cast<const shape_identity*>(shape->userData);
        if (!identity || !(shape->getQueryFilterData().word0 & filter.layers) ||
            (identity->sensor && !filter.include_sensors) || (filter.ignore && identity->body == filter.ignore))
            return physx::PxQueryHitType::eNONE;

        return physx::PxQueryHitType::eTOUCH;
    }

    physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&, const physx::PxShape*,
                                           const physx::PxRigidActor*) override
    {
        return physx::PxQueryHitType::eTOUCH;
    }

  private:
    query_filter filter;
};

template<class Hit>
class query_collector final : public physx::PxHitCallback<Hit>
{
  public:
    explicit query_collector(std::span<query_hit> out)
        : physx::PxHitCallback<Hit>(scratch.data(), static_cast<physx::PxU32>(scratch.size())), output(out)
    {
    }

    physx::PxAgain processTouches(const Hit* hits, physx::PxU32 count) override
    {
        for (const auto& hit : std::span(hits, count))
        {
            const auto& identity = *static_cast<const shape_identity*>(hit.shape->userData);
            if (summary.written < output.size())
            {
                auto& value = output[summary.written++];
                value = query_hit{identity.body, identity.shape};
                if constexpr (!std::same_as<Hit, physx::PxOverlapHit>)
                {
                    value.position = engine(hit.position);
                    value.normal = engine(hit.normal);
                    value.distance = hit.distance;
                    value.face = hit.faceIndex;
                    value.has_location = true;
                }
            }
            ++summary.required_capacity;
        }
        summary.truncated = summary.required_capacity > output.size();
        return true; // Continue even after caller overflow to count every hit.
    }

    query_result summary;

  private:
    std::array<Hit, 32> scratch;
    std::span<query_hit> output;
};

} // namespace

struct CollisionGeometry::implementation
{
    std::shared_ptr<sdk_runtime> runtime;
    geometry_kind kind;
    bool gpu_compatible = false;
    sdk_owner<physx::PxConvexMesh> convex;
    sdk_owner<physx::PxTriangleMesh> triangle;
    sdk_owner<physx::PxHeightField> heightfield;
};

CollisionGeometry::CollisionGeometry(std::unique_ptr<implementation> state) noexcept : m_state(std::move(state)) {}
CollisionGeometry::~CollisionGeometry() = default;
geometry_kind CollisionGeometry::kind() const noexcept
{
    return m_state->kind;
}

bool CollisionGeometry::gpu_compatible() const noexcept
{
    return m_state->gpu_compatible;
}

struct PhysicsSceneChannel::storage
{
    scene_id identity;
    std::atomic<bool> closed{false};
    std::atomic<std::uint64_t> tick{0};
    std::atomic<bool> failed{false};
    std::atomic<std::uint32_t> fetch_failure{0};
    std::mutex command_mutex;
    std::vector<command> queued, ready;
    std::array<std::uint64_t, 256> committed_sequences{};
    std::uint64_t closed_tick = 0;
    std::atomic<std::uint64_t> command_rejections{0}, command_overflows{0};
    std::uint32_t command_capacity = 0;
    std::atomic<std::shared_ptr<const tick_snapshot>> published;
};

struct PhysicsScene::implementation
{
    std::shared_ptr<PhysicsSceneChannel::storage> access = std::make_shared<PhysicsSceneChannel::storage>();
    std::shared_ptr<sdk_runtime> runtime;
#if PX_SUPPORT_GPU_PHYSX
    sdk_owner<physx::PxCudaContextManager> cuda;
#endif
    std::unique_ptr<event_collector> event_sink;
    std::unique_ptr<task_dispatcher> dispatcher;
    sdk_owner<physx::PxScene> scene;                         // Released before dispatcher and SDK.
    sdk_owner<physx::PxControllerManager> character_manager; // Released before the SDK scene.
    std::thread::id owner = std::this_thread::get_id();
    scene_id& identity = access->identity;
    bool stepping = false;
    std::atomic<std::uint64_t>& tick = access->tick;
    std::atomic<bool>& failed = access->failed;
    std::atomic<std::uint32_t>& fetch_failure = access->fetch_failure;
    std::atomic<scene_phase> phase{scene_phase::idle};
    bool gpu_requested = false;
    execution_backend backend = execution_backend::cpu;
    gpu_fallback_reason gpu_fallback = gpu_fallback_reason::none;
    std::vector<body_slot> bodies;
    std::uint32_t free_body_slot = UINT32_MAX;
    std::vector<character_slot> characters;
    std::uint32_t free_character_slot = UINT32_MAX;
    std::vector<std::unique_ptr<body_record>> retired;
    std::mutex& command_mutex = access->command_mutex;
    std::vector<command>& queued = access->queued;
    std::vector<command>& ready = access->ready;
    std::array<std::uint64_t, 256>& committed_sequences = access->committed_sequences;
    std::uint64_t& closed_tick = access->closed_tick;
    std::atomic<std::uint64_t>& command_rejections = access->command_rejections;
    std::atomic<std::uint64_t>& command_overflows = access->command_overflows;
    std::uint64_t body_count = 0, shape_count = 0, character_count = 0;
    std::uint64_t changed_bodies = 0, changed_shapes = 0, changed_characters = 0;
    std::uint64_t queries = 0, query_hits = 0, query_overflows = 0, query_scratch_peak = 0;
    std::uint64_t prior_submitted = 0, prior_completed = 0, prior_inline = 0, tick_buffer_peak = 0;

    void note_change(body_record& record)
    {
        const auto target = tick.load(std::memory_order_relaxed) + (phase.load() == scene_phase::commit ? 0 : 1);
        if (record.changed_tick == target)
            return;
        record.changed_tick = target;
        ++changed_bodies;
        changed_shapes += record.shapes.size();
    }

    void note_change(character_record& record)
    {
        const auto target = tick.load(std::memory_order_relaxed) + (phase.load() == scene_phase::commit ? 0 : 1);
        if (record.changed_tick == target)
            return;
        record.changed_tick = target;
        ++changed_characters;
    }

    void note_query(query_result result, std::size_t scratch_bytes)
    {
        ++queries;
        query_hits += result.required_capacity;
        query_overflows += result.truncated;
        query_scratch_peak = (std::max)(query_scratch_peak, static_cast<std::uint64_t>(scratch_bytes));
    }

    std::uint32_t& command_capacity = access->command_capacity;
    std::uint32_t event_capacity = 0;
    std::shared_ptr<snapshot_pool> snapshots;
    std::shared_ptr<tick_snapshot> pending;
    std::atomic<std::shared_ptr<const tick_snapshot>>& published = access->published;

    result<void> require_idle(std::source_location location) const
    {
        if (failed.load(std::memory_order_acquire))
            return std::unexpected(error{error_code::backend_initialization, fetch_failure.load(),
                                         "Scene is in terminal fetch failure", location});
        const auto current = phase.load(std::memory_order_acquire);
        if (owner != std::this_thread::get_id() || (current != scene_phase::idle && current != scene_phase::commit))
            return std::unexpected(
                error{error_code::wrong_phase, 0, "Operation requires the idle scene owner", location});
        return {};
    }

    result<void> require_read(std::source_location location) const
    {
        if (failed.load(std::memory_order_acquire))
            return std::unexpected(error{error_code::backend_initialization, fetch_failure.load(),
                                         "Scene is in terminal fetch failure", location});
        const auto current = phase.load(std::memory_order_acquire);
        if (owner != std::this_thread::get_id() ||
            (current != scene_phase::idle && current != scene_phase::query_read && current != scene_phase::publish))
            return std::unexpected(
                error{error_code::wrong_phase, 0, "Read requires a completed owner-side scene", location});
        return {};
    }

    result<body_record*> find(body_handle handle, std::source_location location) const
    {
        if (handle.scene != identity)
            return std::unexpected(error{error_code::wrong_scene, 0, "Body belongs to another scene", location});
        if (!handle || handle.slot >= bodies.size() || bodies[handle.slot].generation != handle.generation ||
            !bodies[handle.slot].record)
            return std::unexpected(error{error_code::stale_handle, 0, "Body handle is stale", location});
        return bodies[handle.slot].record.get();
    }

    result<character_record*> find(character_handle handle, std::source_location location) const
    {
        if (handle.scene != identity)
            return std::unexpected(error{error_code::wrong_scene, 0, "Character belongs to another scene", location});
        if (!handle || handle.slot >= characters.size() || characters[handle.slot].generation != handle.generation ||
            !characters[handle.slot].record)
            return std::unexpected(error{error_code::stale_handle, 0, "Character handle is stale", location});
        return characters[handle.slot].record.get();
    }

    result<void> validate_query_filter(const query_filter& filter, std::source_location location) const
    {
        if (filter.ignore)
            return find(filter.ignore, location).transform([](body_record*) {});
        return {};
    }

    result<physx::PxGeometryHolder> resolve(const geometry& form, std::source_location location) const;

    physx::PxSceneDesc cpu_description(const scene_config& config) const
    {
        physx::PxSceneDesc desc(runtime->physics->getTolerancesScale());
        desc.gravity = {config.gravity.x, config.gravity.y, config.gravity.z};
        desc.cpuDispatcher = dispatcher.get();
        desc.filterShader = simulation_filter;
        desc.simulationEventCallback = event_sink.get();
        desc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;
        return desc;
    }

    void try_gpu(const scene_config& config)
    {
        ce::profile_scope scope{ce::marker<"Physics.GpuInitialize">()};
#if PX_SUPPORT_GPU_PHYSX
#if defined(CE_PHYSICS_TESTING)
        if (test::consume(test::failure_point::cuda_context))
        {
            gpu_fallback = gpu_fallback_reason::context_unavailable;
            return;
        }
#endif
        cuda.reset(PxCreateCudaContextManager(*runtime->foundation, physx::PxCudaContextManagerDesc{}));
        if (!cuda || !cuda->contextIsValid())
        {
            cuda.reset();
            gpu_fallback = gpu_fallback_reason::context_unavailable;
            return;
        }

        auto desc = cpu_description(config);
        desc.cudaContextManager = cuda.get();
        desc.flags |= physx::PxSceneFlag::eENABLE_GPU_DYNAMICS | physx::PxSceneFlag::eENABLE_PCM;
        desc.broadPhaseType = physx::PxBroadPhaseType::eGPU;
        scene.reset(runtime->physics->createScene(desc));

#if defined(CE_PHYSICS_TESTING)
        if (test::consume(test::failure_point::gpu_scene))
            scene.reset();
#endif
        if (scene && scene->getFlags().isSet(physx::PxSceneFlag::eENABLE_GPU_DYNAMICS) &&
            scene->getBroadPhaseType() == physx::PxBroadPhaseType::eGPU && scene->getCudaContextManager() == cuda.get())
        {
            backend = execution_backend::gpu;
            return;
        }

        scene.reset();
        cuda.reset();
        gpu_fallback = gpu_fallback_reason::scene_rejected;
#else
        gpu_fallback = gpu_fallback_reason::unsupported_build;
#endif
    }
};

PhysicsScene::PhysicsScene(std::unique_ptr<implementation> state) noexcept : m_state(std::move(state)) {}

PhysicsScene::~PhysicsScene()
{
    if (m_state->owner != std::this_thread::get_id())
        std::terminate();
    // Close admission under the queue lock before joining SDK tasks. Channels do not
    // touch this object, so racing submit/read operations cannot extend SDK lifetime.
    {
        ce::profile_context_scope context{{m_state->identity.value, m_state->tick.load(), 0}};
        ce::profile_scope scope{ce::marker<"Physics.RequestClose">()};
        std::lock_guard lock(m_state->command_mutex);
        m_state->access->closed.store(true, std::memory_order_release);
    }
    if (m_state->stepping)
        (void)finish_step();

    {
        std::lock_guard lock(m_state->command_mutex);
        m_state->queued.clear();
        m_state->ready.clear();
    }

    m_state->phase.store(scene_phase::closing, std::memory_order_release);
    ce::profile_scope scope{ce::marker<"Physics.SceneDestroy">()};
    m_state->dispatcher->drain();
    m_state->scene->setSimulationEventCallback(nullptr);
}

result<std::unique_ptr<PhysicsScene>> PhysicsScene::create(const scene_config& config, std::source_location location)
{
    ce::profile_scope scope{ce::marker<"Physics.SceneCreate">()};
    if (!std::isfinite(config.gravity.x) || !std::isfinite(config.gravity.y) || !std::isfinite(config.gravity.z) ||
        config.snapshot_capacity < 2 || config.snapshot_capacity > 256 || config.command_capacity == 0 ||
        config.command_capacity > 65536 || config.event_capacity == 0 || config.event_capacity > 65536 ||
        config.workers > 256 || config.task_capacity == 0 || config.task_capacity > 1048576 ||
        (config.execution != execution_preference::cpu && config.execution != execution_preference::prefer_gpu))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid physics scene configuration", location});
    try
    {
        return acquire_runtime(location).and_then(
            [&](std::shared_ptr<sdk_runtime> runtime) -> result<std::unique_ptr<PhysicsScene>> {
                auto identity = allocate_scene_id(location);
                if (!identity)
                    return std::unexpected(identity.error());

                auto state = std::make_unique<implementation>();
                state->snapshots = std::make_shared<snapshot_pool>(config.snapshot_capacity);
                state->command_capacity = config.command_capacity;
                state->event_capacity = config.event_capacity;
                state->queued.reserve(config.command_capacity);
                state->ready.reserve(config.command_capacity);
                state->event_sink = std::make_unique<event_collector>(config.event_capacity, state->retired);
                state->runtime = std::move(runtime);
                state->identity = *identity;
                state->gpu_requested = config.execution == execution_preference::prefer_gpu;
                const auto logical = std::thread::hardware_concurrency();
                const auto workers = config.workers ? config.workers : (std::min)(256u, logical > 4 ? logical - 4 : 1u);
                state->dispatcher = std::make_unique<task_dispatcher>(workers, config.task_capacity, *identity);
#if defined(CE_PHYSICS_TESTING)
                if (test::consume(test::failure_point::allocation))
                    throw std::bad_alloc{};
                if (test::consume(test::failure_point::dispatcher))
                    return std::unexpected(
                        error{error_code::backend_initialization, 0, "Injected dispatcher failure", location});
#endif
                if (state->gpu_requested)
                    state->try_gpu(config);

                if (!state->scene)
                {
                    // A fresh descriptor prevents CUDA pointers and GPU flags leaking into CPU fallback.
                    ce::profile_scope cpu_scope{ce::marker<"Physics.CpuInitialize">()};
                    state->scene.reset(state->runtime->physics->createScene(state->cpu_description(config)));
                }
                if (!state->scene)
                    return std::unexpected(error{error_code::backend_initialization, state->runtime->errors.last.load(),
                                                 "PhysX scene creation failed", location});
#if defined(CE_PHYSICS_TESTING)
                if (test::consume(test::failure_point::scene))
                    return std::unexpected(
                        error{error_code::backend_initialization, 0, "Injected scene failure", location});
#endif
                return std::unique_ptr<PhysicsScene>(new PhysicsScene(std::move(state)));
            });
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Physics allocation failed", location});
    }
    catch (const std::system_error&)
    {
        return std::unexpected(
            error{error_code::backend_initialization, 0, "Physics worker creation failed", location});
    }
}

result<PhysicsSceneChannel> PhysicsScene::channel(std::source_location location) const
{
    if (m_state->owner != std::this_thread::get_id())
        return std::unexpected(error{error_code::wrong_phase, 0, "Channel acquisition requires the scene owner", location});

    return PhysicsSceneChannel{m_state->access};
}

scene_id PhysicsSceneChannel::identity() const noexcept
{
    return m_storage ? m_storage->identity : scene_id{};
}

bool PhysicsSceneChannel::is_closed() const noexcept
{
    return !m_storage || m_storage->closed.load(std::memory_order_acquire);
}

std::shared_ptr<const tick_snapshot> PhysicsSceneChannel::latest_snapshot() const noexcept
{
    return m_storage ? m_storage->published.load(std::memory_order_acquire) : nullptr;
}

result<void> PhysicsScene::submit_command(command value, std::source_location location)
{
    return PhysicsSceneChannel{m_state->access}.submit(std::move(value), location);
}

result<void> PhysicsSceneChannel::submit(command value, std::source_location location) const
{
    if (!m_storage)
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics channel is empty", location});

    const auto rejected = [&](error failure) -> result<void> {
        m_storage->command_rejections.fetch_add(1, std::memory_order_relaxed);
        if (failure.code == error_code::capacity_exceeded)
            m_storage->command_overflows.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(failure);
    };
    if (value.payload.valueless_by_exception())
        return rejected(error{error_code::invalid_argument, 0, "Command payload has no value", location});

    std::visit(
        [](auto& payload) {
            using T = std::remove_cvref_t<decltype(payload)>;
            if constexpr (std::same_as<T, create_body_command> || std::same_as<T, replace_body_command>)
                payload.definition.properties.shapes = {}; // A queued definition retains no borrowed span.
        },
        value.payload);

    const auto& stamp = value.stamp;
    if (stamp.scene != m_storage->identity)
        return rejected(error{error_code::wrong_scene, 0, "Command belongs to another scene", location});
    if (stamp.tick.value == 0 || stamp.sequence == 0 || stamp.producer >= m_storage->committed_sequences.size())
        return rejected(error{error_code::invalid_argument, 0, "Invalid command identity", location});

    std::lock_guard lock(m_storage->command_mutex);
    if (m_storage->closed.load(std::memory_order_acquire))
        return rejected(error{error_code::wrong_phase, 0, "Physics channel is closed", location});
    if (m_storage->failed.load(std::memory_order_acquire))
        return rejected(error{error_code::backend_initialization, m_storage->fetch_failure.load(),
                              "Scene is in terminal fetch failure", location});
    if (stamp.tick.value <= m_storage->closed_tick)
        return rejected(error{error_code::late_command, 0, "Command tick input is already closed", location});
    const auto same_sequence = [&](const command& pending) {
        return pending.stamp.producer == stamp.producer && pending.stamp.sequence == stamp.sequence;
    };
    if (stamp.sequence <= m_storage->committed_sequences[stamp.producer] ||
        std::ranges::any_of(m_storage->queued, same_sequence) || std::ranges::any_of(m_storage->ready, same_sequence))
        return rejected(
            error{error_code::duplicate_command, 0, "Producer sequence is already reserved or consumed", location});
    if (m_storage->queued.size() == m_storage->command_capacity)
        return rejected(error{error_code::capacity_exceeded, 0, "Command queue is full", location});

    value.m_received_tick = m_storage->tick.load(std::memory_order_relaxed);
    m_storage->queued.push_back(std::move(value)); // Storage and owned payload are already allocated.
    return {};
}

std::shared_ptr<const tick_snapshot> PhysicsScene::latest_snapshot() const noexcept
{
    return m_state->published.load(std::memory_order_acquire);
}

result<void> PhysicsScene::apply_force(body_handle body, math::vector3 linear, math::vector3 angular, force_mode mode,
                                       std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    auto record = m_state->find(body, location);
    if (!record)
        return std::unexpected(record.error());
    if ((*record)->kind != body_kind::dynamic || !finite(linear) || !finite(angular) || std::to_underlying(mode) > 3)
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Force requires a dynamic body and finite vectors", location});

    ce::profile_scope scope{ce::marker<"Physics.ApplyForce">()};
    constexpr std::array modes{physx::PxForceMode::eFORCE, physx::PxForceMode::eIMPULSE,
                               physx::PxForceMode::eACCELERATION, physx::PxForceMode::eVELOCITY_CHANGE};
    auto* actor = (*record)->actor->is<physx::PxRigidDynamic>();
    actor->addForce(sdk(linear), modes[std::to_underlying(mode)]);
    actor->addTorque(sdk(angular), modes[std::to_underlying(mode)]);
    m_state->note_change(**record);
    return {};
}

result<void> PhysicsScene::begin_step(float seconds, std::source_location location)
{
    if (m_state->failed.load(std::memory_order_acquire))
        return std::unexpected(error{error_code::backend_initialization, m_state->fetch_failure.load(),
                                     "Scene is in terminal fetch failure", location});
    if (m_state->owner != std::this_thread::get_id() || m_state->stepping ||
        m_state->phase.load(std::memory_order_acquire) != scene_phase::idle)
        return std::unexpected(error{error_code::wrong_phase, 0, "Scene owner/step phase violation", location});
    if (!std::isfinite(seconds) || seconds <= 0.f)
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Step duration must be finite and positive", location});
    const auto previous = m_state->tick.load(std::memory_order_relaxed);
    if (previous == UINT64_MAX)
        return std::unexpected(error{error_code::capacity_exceeded, 0, "Physics tick identity exhausted", location});
    const auto tick = previous + 1;

    // All publication storage is secured before closing input or applying a command.
    try
    {
#if defined(CE_PHYSICS_TESTING)
        if (test::consume(test::failure_point::snapshot_allocation))
            throw std::bad_alloc{};
#endif
        ce::profile_context_scope preparation_context{{m_state->identity.value, tick, 0}};
        ce::profile_scope preparation{ce::marker<"Physics.SnapshotPrepare">()};
        auto acquired = m_state->snapshots->acquire(location);
        if (!acquired)
            return std::unexpected(acquired.error());
        auto pending = std::move(*acquired);
        pending->commands.clear();
        pending->events.clear();
        pending->active_poses.clear();
        pending->characters.clear();
        pending->failure.reset();
        pending->step_succeeded = false;
        pending->statistics = {};
        pending->scene = m_state->identity;
        pending->tick = tick_id{tick};
        pending->events.reserve(m_state->event_capacity);
        std::lock_guard lock(m_state->command_mutex);
        const auto due = std::ranges::count_if(m_state->queued,
                                               [tick](const command& value) { return value.stamp.tick.value <= tick; });
        pending->commands.reserve(static_cast<std::size_t>(due) + m_state->command_capacity);
        pending->active_poses.reserve(m_state->bodies.size() + static_cast<std::size_t>(due));
        pending->characters.reserve(m_state->characters.size() + static_cast<std::size_t>(due));
        m_state->retired.reserve(m_state->retired.size() + static_cast<std::size_t>(due));
        m_state->pending = std::move(pending);
        m_state->closed_tick = tick;
        m_state->tick.store(tick, std::memory_order_relaxed);
        std::size_t future = 0;
        for (auto& value : m_state->queued)
        {
            if (value.stamp.tick.value <= tick)
                m_state->ready.push_back(std::move(value));
            else
            {
                if (&value != &m_state->queued[future])
                    m_state->queued[future] = std::move(value);
                ++future;
            }
        }
        m_state->queued.resize(future);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            error{error_code::out_of_memory, 0, "Tick publication storage allocation failed", location});
    }

    m_state->phase.store(scene_phase::commit, std::memory_order_release);
    m_state->dispatcher->current_tick.store(tick, std::memory_order_relaxed);
    ce::profile_context_scope context{{m_state->identity.value, tick, 0}};
    ce::profile_scope_begin(ce::marker<"PhysicsTick">());
    {
        ce::profile_scope scope{ce::marker<"Physics.CommandCommit">()};
        {
            std::lock_guard lock(m_state->command_mutex);
            std::ranges::sort(m_state->ready, {}, [](const command& value) { return value.stamp.order(); });
        }
        for (const auto& value : m_state->ready)
        {
            command_outcome outcome{value.stamp, {}, std::nullopt};
            outcome.waited_ticks = tick - value.m_received_tick;
            m_state->pending->statistics.max_command_wait_ticks =
                (std::max)(m_state->pending->statistics.max_command_wait_ticks, outcome.waited_ticks);
            {
                std::lock_guard lock(m_state->command_mutex);
                auto& sequence = m_state->committed_sequences[value.stamp.producer];
                if (value.stamp.sequence <= sequence)
                    outcome.failure = error{error_code::duplicate_command, 0,
                                            "Producer sequence precedes committed history", location};
                else
                    sequence = value.stamp.sequence; // Failed commands also consume their sequence.
            }
            if (!outcome.failure)
            {
                const auto applied = std::visit(
                    [&](const auto& payload) -> result<void> {
                        using T = std::remove_cvref_t<decltype(payload)>;
                        if constexpr (std::same_as<T, create_body_command>)
                            return create_body(payload.definition.view(), location).transform([&](body_handle body) {
                                outcome.created = body;
                            });
                        else if constexpr (std::same_as<T, destroy_body_command>)
                            return destroy_body(payload.body, location).transform([&] {
                                outcome.retired = payload.body;
                            });
                        else if constexpr (std::same_as<T, replace_body_command>)
                            return replace_body(payload.body, payload.definition.view(), location)
                                .transform([&](body_handle body) {
                                    outcome.created = body;
                                    outcome.retired = payload.body;
                                });
                        else if constexpr (std::same_as<T, set_pose_command>)
                            return set_pose(payload.body, payload.value, location);
                        else if constexpr (std::same_as<T, set_velocity_command>)
                            return set_velocity(payload.body, payload.linear, payload.angular, location);
                        else if constexpr (std::same_as<T, set_kinematic_target_command>)
                            return set_kinematic_target(payload.body, payload.value, location);
                        else if constexpr (std::same_as<T, create_character_command>)
                            return create_character(payload.definition, location)
                                .transform([&](character_handle handle) { outcome.character_created = handle; });
                        else if constexpr (std::same_as<T, destroy_character_command>)
                            return destroy_character(payload.character, location).transform([&] {
                                outcome.character_retired = payload.character;
                            });
                        else if constexpr (std::same_as<T, move_character_command>)
                            return move_character(payload.character, payload.movement, location)
                                .transform([](const character_state&) {});
                        else if constexpr (std::same_as<T, teleport_character_command>)
                            return teleport_character(payload.character, payload.position, location);
                        else
                            return apply_force(payload.body, payload.linear, payload.angular, payload.mode, location);
                    },
                    value.payload);
                if (!applied)
                    outcome.failure = applied.error();
            }
            if (outcome.failure)
                ++m_state->pending->statistics.commands_failed;
            else
                ++m_state->pending->statistics.commands_applied;
            m_state->pending->commands.push_back(outcome);
        }
        std::lock_guard lock(m_state->command_mutex);
        m_state->ready.clear();
    }
    ce::profile_scope scope{ce::marker<"Physics.SimulateSubmit">()};
    m_state->phase.store(scene_phase::simulating, std::memory_order_release);
    m_state->stepping = true;
    m_state->scene->simulate(seconds);
    return {};
}

result<void> PhysicsScene::finish_step(std::source_location location)
{
    if (m_state->owner != std::this_thread::get_id() || !m_state->stepping)
        return std::unexpected(error{error_code::wrong_phase, 0, "Scene owner/step phase violation", location});

    ce::profile_context_scope context{{m_state->identity.value, m_state->tick.load(std::memory_order_relaxed), 0}};
    physx::PxU32 failure = 0;
    bool fetched = false;
    {
        ce::profile_scope scope{ce::marker<"Physics.FetchWait">()};
        {
            ce::profile_scope fetch{ce::marker<"Physics.FetchResults">()};
            fetched = m_state->scene->fetchResults(true, &failure);
        }

        {
            ce::profile_scope drain{ce::marker<"Physics.DispatcherDrain">()};
            m_state->dispatcher->drain();
        }
    }
#if defined(CE_PHYSICS_TESTING)
    if (test::consume(test::failure_point::step_fetch))
        failure = 1;
#endif
    m_state->stepping = false;
    m_state->phase.store(scene_phase::publish, std::memory_order_release);
    auto& snapshot = *m_state->pending;
    snapshot.step_succeeded = fetched && failure == 0;
    {
        ce::profile_scope scope{ce::marker<"Physics.EventCollect">()};
        std::lock_guard lock(m_state->event_sink->mutex);
        snapshot.events.assign(m_state->event_sink->events.begin(), m_state->event_sink->events.end());
        const auto commands_applied = snapshot.statistics.commands_applied;
        const auto commands_failed = snapshot.statistics.commands_failed;
        const auto wait_ticks = snapshot.statistics.max_command_wait_ticks;
        snapshot.statistics = m_state->event_sink->statistics;
        snapshot.statistics.commands_applied = commands_applied;
        snapshot.statistics.commands_failed = commands_failed;
        snapshot.statistics.max_command_wait_ticks = wait_ticks;
        m_state->event_sink->events.clear();
        m_state->event_sink->statistics = {};
        std::ranges::sort(snapshot.events, {}, [](const collision_event& event) {
            return std::tuple{event.first.body, event.first.shape, event.second.body, event.second.shape, event.kind};
        });
    }
    if (snapshot.step_succeeded)
    {
        ce::profile_scope scope{ce::marker<"Physics.ActivePoseCollect">()};
        physx::PxU32 count = 0;
        auto* actors = m_state->scene->getActiveActors(count);
        for (auto index : std::views::iota(0u, count))
        {
            const auto* record = static_cast<const body_record*>(actors[index]->userData);
            if (!record)
                continue;
            const auto state = read_body(record->handle, location);
            if (state)
                snapshot.active_poses.push_back({record->handle, *state});
        }
    }
    else
    {
        std::lock_guard lock(m_state->command_mutex);
        snapshot.failure = error{error_code::backend_initialization, failure, "PhysX fetch failed", location};
        m_state->fetch_failure.store(failure, std::memory_order_relaxed);
        m_state->failed.store(true, std::memory_order_release);
        m_state->phase.store(scene_phase::failed, std::memory_order_release);
        for (const auto& value : m_state->queued)
        {
            command_outcome outcome{value.stamp, {}, snapshot.failure};
            outcome.cancelled = true;
            snapshot.commands.push_back(outcome);
            ++snapshot.statistics.commands_cancelled;
        }
        m_state->queued.clear();
    }
    if (snapshot.step_succeeded)
    {
        ce::profile_scope scope{ce::marker<"Physics.CharacterPoseCollect">()};
        for (auto index : std::views::iota(std::size_t{0}, m_state->characters.size()))
        {
            const auto& slot = m_state->characters[index];
            if (slot.record)
                snapshot.characters.push_back(
                    {character_handle{m_state->identity, static_cast<std::uint32_t>(index), slot.generation},
                     slot.record->state});
        }
    }
    if (snapshot.step_succeeded)
        m_state->retired.clear(); // Only after removed-shape reports have been copied.
    auto& statistics = snapshot.statistics;
    {
        ce::profile_scope statisticsScope{ce::marker<"Physics.SnapshotStatistics">()};
        statistics.bodies = m_state->body_count;
        statistics.shapes = m_state->shape_count;
        statistics.characters = m_state->character_count;
        statistics.active_bodies = snapshot.active_poses.size();
        for (const auto& value : snapshot.active_poses)
        {
            const auto record = m_state->find(value.body, location);
            if (record)
                statistics.active_shapes += (*record)->shapes.size();
        }
        statistics.changed_bodies = std::exchange(m_state->changed_bodies, 0);
        statistics.changed_shapes = std::exchange(m_state->changed_shapes, 0);
        statistics.changed_characters = std::exchange(m_state->changed_characters, 0);
        statistics.queries = std::exchange(m_state->queries, 0);
        statistics.query_hits = std::exchange(m_state->query_hits, 0);
        statistics.query_overflows = std::exchange(m_state->query_overflows, 0);
        statistics.query_scratch_peak_bytes = m_state->query_scratch_peak;
        statistics.events_stored = snapshot.events.size();
        for (const auto& event : snapshot.events)
            statistics.contacts_stored += event.contact_count;
        statistics.workers = m_state->dispatcher->getWorkerCount();
        const auto submitted = m_state->dispatcher->submitted.load();
        const auto completed = m_state->dispatcher->completed.load();
        const auto inline_tasks = m_state->dispatcher->inline_tasks.load();
        statistics.tasks_submitted = submitted - std::exchange(m_state->prior_submitted, submitted);
        statistics.tasks_completed = completed - std::exchange(m_state->prior_completed, completed);
        statistics.tasks_inline = inline_tasks - std::exchange(m_state->prior_inline, inline_tasks);
        statistics.tick_buffer_bytes = snapshot.commands.capacity() * sizeof(command_outcome) +
                                       snapshot.events.capacity() * sizeof(collision_event) +
                                       snapshot.active_poses.capacity() * sizeof(active_body_pose) +
                                       snapshot.characters.capacity() * sizeof(character_pose);
        m_state->tick_buffer_peak = (std::max)(m_state->tick_buffer_peak, statistics.tick_buffer_bytes);
        statistics.tick_buffer_peak_bytes = m_state->tick_buffer_peak;
        statistics.step_failed = !snapshot.step_succeeded;
        statistics.command_rejections = m_state->command_rejections.exchange(0);
        statistics.command_overflows = m_state->command_overflows.exchange(0);
        {
            std::lock_guard lock(m_state->command_mutex);
            statistics.commands_queued = m_state->queued.size();
        }
        m_state->snapshots->statistics(statistics);
    }
#if !CE_SHIPPING
    if (ce::profiler().counter_enabled(ce::counter_category::physics))
    {
        ce::profile_scope scope{ce::marker<"Physics.Counters">()};
        const ce::cpu_span_context owner{snapshot.scene.value, snapshot.tick.value, 0};
        const std::array samples{
            ce::profile_counter_sample{ce::profile_counter_id::physics_bodies, static_cast<double>(statistics.bodies),
                                       owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_shapes, static_cast<double>(statistics.shapes),
                                       owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_characters,
                                       static_cast<double>(statistics.characters), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_active_bodies,
                                       static_cast<double>(statistics.active_bodies), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_active_shapes,
                                       static_cast<double>(statistics.active_shapes), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_changed_bodies,
                                       static_cast<double>(statistics.changed_bodies), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_changed_shapes,
                                       static_cast<double>(statistics.changed_shapes), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_changed_characters,
                                       static_cast<double>(statistics.changed_characters), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_commands_applied,
                                       static_cast<double>(statistics.commands_applied), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_commands_failed,
                                       static_cast<double>(statistics.commands_failed), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_commands_cancelled,
                                       static_cast<double>(statistics.commands_cancelled), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_commands_queued,
                                       static_cast<double>(statistics.commands_queued), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_events_stored,
                                       static_cast<double>(statistics.events_stored), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_required_events,
                                       static_cast<double>(statistics.required_events), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_dropped_events,
                                       static_cast<double>(statistics.dropped_events), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_contacts_stored,
                                       static_cast<double>(statistics.contacts_stored), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_required_contacts,
                                       static_cast<double>(statistics.required_contacts), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_dropped_contacts,
                                       static_cast<double>(statistics.dropped_contacts), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_unresolved_identities,
                                       static_cast<double>(statistics.unresolved_identities), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_queries, static_cast<double>(statistics.queries),
                                       owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_query_hits,
                                       static_cast<double>(statistics.query_hits), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_query_overflows,
                                       static_cast<double>(statistics.query_overflows), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_workers, static_cast<double>(statistics.workers),
                                       owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_tasks_submitted,
                                       static_cast<double>(statistics.tasks_submitted), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_tasks_completed,
                                       static_cast<double>(statistics.tasks_completed), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_tasks_inline,
                                       static_cast<double>(statistics.tasks_inline), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_snapshot_buffers,
                                       static_cast<double>(statistics.snapshot_buffers), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_snapshot_buffers_in_use,
                                       static_cast<double>(statistics.snapshot_buffers_in_use), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_tick_buffer_bytes,
                                       static_cast<double>(statistics.tick_buffer_bytes), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_tick_buffer_peak_bytes,
                                       static_cast<double>(statistics.tick_buffer_peak_bytes), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_query_scratch_peak_bytes,
                                       static_cast<double>(statistics.query_scratch_peak_bytes), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_step_failed,
                                       static_cast<double>(statistics.step_failed), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_command_rejections,
                                       static_cast<double>(statistics.command_rejections), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_command_overflows,
                                       static_cast<double>(statistics.command_overflows), owner},
            ce::profile_counter_sample{ce::profile_counter_id::physics_max_command_wait_ticks,
                                       static_cast<double>(statistics.max_command_wait_ticks), owner},
        };
        ce::profiler().publish_counters(ce::profiler().current_frame(), ce::counter_category::physics, samples);
    }
#endif

    {
        ce::profile_scope scope{ce::marker<"Physics.SnapshotPublish">()};
        m_state->published.store(std::move(m_state->pending), std::memory_order_release);
    }
    ce::profile_scope_end();
    if (snapshot.step_succeeded)
        m_state->phase.store(scene_phase::idle, std::memory_order_release);
    if (!fetched || failure)
        return std::unexpected(error{error_code::backend_initialization, failure, "PhysX fetch failed", location});
    return {};
}

scene_status PhysicsScene::status() const noexcept
{
    return {m_state->identity,
            m_state->backend,
            m_state->gpu_requested,
            m_state->gpu_fallback != gpu_fallback_reason::none,
            m_state->gpu_fallback,
            m_state->scene->getFlags().isSet(physx::PxSceneFlag::eENABLE_GPU_DYNAMICS),
            m_state->scene->getBroadPhaseType() == physx::PxBroadPhaseType::eGPU,
            m_state->dispatcher->getWorkerCount(),
            m_state->dispatcher->submitted.load(),
            m_state->dispatcher->completed.load(),
            m_state->dispatcher->inline_tasks.load(),
            m_state->runtime->errors.count.load(),
            tick_id{m_state->tick.load(std::memory_order_relaxed)},
            m_state->failed.load(std::memory_order_acquire),
            m_state->phase.load(std::memory_order_acquire)};
}

result<physx::PxGeometryHolder> PhysicsScene::implementation::resolve(const geometry& form,
                                                                      std::source_location location) const
{
    return std::visit(
        [&](const auto& value) -> result<physx::PxGeometryHolder> {
            using T = std::remove_cvref_t<decltype(value)>;
            physx::PxGeometryHolder holder;

            if constexpr (std::same_as<T, box_geometry>)
            {
                if (!finite(value.half_extent) || value.half_extent.x <= 0 || value.half_extent.y <= 0 ||
                    value.half_extent.z <= 0)
                    return std::unexpected(
                        error{error_code::invalid_argument, 0, "Box extents must be positive", location});
                holder.storeAny(physx::PxBoxGeometry(sdk(value.half_extent)));
            }
            else if constexpr (std::same_as<T, sphere_geometry> || std::same_as<T, capsule_geometry>)
            {
                if (!std::isfinite(value.radius) || value.radius <= 0)
                    return std::unexpected(error{error_code::invalid_argument, 0, "Radius must be positive", location});
                if constexpr (std::same_as<T, capsule_geometry>)
                {
                    if (!std::isfinite(value.half_height) || value.half_height < 0)
                        return std::unexpected(error{error_code::invalid_argument, 0,
                                                     "Capsule half-height must be nonnegative", location});
                    holder.storeAny(physx::PxCapsuleGeometry(value.radius, value.half_height));
                }
                else
                    holder.storeAny(physx::PxSphereGeometry(value.radius));
            }
            else
            {
                if (!value.asset || !finite(value.scale) || value.scale.x <= 0 || value.scale.y <= 0 ||
                    value.scale.z <= 0)
                    return std::unexpected(
                        error{error_code::invalid_argument, 0, "Cooked asset and positive scale required", location});
                const auto& asset = *value.asset->m_state;
                if (asset.runtime != runtime)
                    return std::unexpected(
                        error{error_code::wrong_scene, 0, "Cooked asset uses another SDK session", location});
                switch (asset.kind)
                {
                case geometry_kind::convex:
                    holder.storeAny(
                        physx::PxConvexMeshGeometry(asset.convex.get(), physx::PxMeshScale(sdk(value.scale))));
                    break;
                case geometry_kind::triangle_mesh:
                    holder.storeAny(
                        physx::PxTriangleMeshGeometry(asset.triangle.get(), physx::PxMeshScale(sdk(value.scale))));
                    break;
                case geometry_kind::heightfield:
                    holder.storeAny(physx::PxHeightFieldGeometry(asset.heightfield.get(), {}, value.scale.y,
                                                                 value.scale.x, value.scale.z));
                    break;
                }
            }

            if (!physx::PxGeometryQuery::isValid(holder.any()))
                return std::unexpected(error{error_code::invalid_argument, 0, "Geometry exceeds SDK limits", location});
            return holder;
        },
        form);
}

std::uint32_t PhysicsScene::sdk_version() noexcept { return PX_PHYSICS_VERSION; }

result<std::vector<std::byte>> PhysicsScene::cook_geometry_blob(const geometry_cook_input& input)
{
    if (auto phase = m_state->require_idle(std::source_location::current()); !phase)
        return std::unexpected(phase.error());

    ce::profile_scope scope{ce::marker<"Physics.CookGeometryBlob">()};
    try
    {
        physx::PxDefaultMemoryOutputStream stream;
        physx::PxCookingParams params(m_state->runtime->physics->getTolerancesScale());
        params.buildGPUData = true;
        const bool success = std::visit([&](const auto& value) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<T, convex_cook_input>)
            {
                if (value.points.size() < 4 || value.points.size() > UINT32_MAX ||
                    !std::ranges::all_of(value.points, finite) || !convex_has_volume(value.points)) return false;

                configure_convex_area(params, value.points);

                physx::PxConvexMeshDesc desc;
                desc.points.count = static_cast<physx::PxU32>(value.points.size());
                desc.points.stride = sizeof(math::vector3);
                desc.points.data = value.points.data();
                desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eCHECK_ZERO_AREA_TRIANGLES;
                return PxCookConvexMesh(params, desc, stream);
            }
            else if constexpr (std::same_as<T, triangle_cook_input>)
            {
                if (value.points.size() < 3 || value.points.size() > UINT32_MAX || value.triangles.empty() ||
                    value.triangles.size() > UINT32_MAX || !std::ranges::all_of(value.points, finite) ||
                    !std::ranges::all_of(value.triangles, [&](auto triangle) {
                        return triangle.a < value.points.size() && triangle.b < value.points.size() &&
                               triangle.c < value.points.size() && triangle.a != triangle.b &&
                               triangle.a != triangle.c && triangle.b != triangle.c;
                    })) return false;

                physx::PxTriangleMeshDesc desc;
                desc.points.count = static_cast<physx::PxU32>(value.points.size());
                desc.points.stride = sizeof(math::vector3);
                desc.points.data = value.points.data();
                desc.triangles.count = static_cast<physx::PxU32>(value.triangles.size());
                desc.triangles.stride = sizeof(triangle_indices);
                desc.triangles.data = value.triangles.data();
                return PxCookTriangleMesh(params, desc, stream);
            }
            else
            {
                const auto count = std::uint64_t(value.rows) * value.columns;
                if (value.rows < 2 || value.columns < 2 || count > UINT32_MAX || count != value.heights.size()) return false;

                std::vector<physx::PxHeightFieldSample> samples(static_cast<std::size_t>(count));
                for (auto index : std::views::iota(std::size_t{}, samples.size()))
                {
                    samples[index].height = value.heights[index];
                    samples[index].materialIndex0 = 0;
                    samples[index].materialIndex1 = 0;
                    samples[index].clearTessFlag();
                }
                physx::PxHeightFieldDesc desc;
                desc.nbRows = value.rows;
                desc.nbColumns = value.columns;
                desc.samples.data = samples.data();
                desc.samples.stride = sizeof(physx::PxHeightFieldSample);
                return PxCookHeightField(desc, stream);
            }
        }, input);
        if (!success || !stream.getSize())
            return std::unexpected(error{error_code::cooking_failed, 0, "SDK geometry blob cooking failed"});

        const auto* begin = reinterpret_cast<const std::byte*>(stream.getData());
        return std::vector<std::byte>(begin, begin + stream.getSize());
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "SDK geometry blob allocation failed"});
    }
}

result<std::shared_ptr<const CollisionGeometry>> PhysicsScene::load_geometry_blob(
    geometry_kind kind, std::span<const std::byte> bytes)
{
    if (auto phase = m_state->require_idle(std::source_location::current()); !phase)
        return std::unexpected(phase.error());

    ce::profile_scope scope{ce::marker<"Physics.LoadGeometryBlob">()};
    if (bytes.empty() || bytes.size() > UINT32_MAX)
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid cooked geometry blob size"});

    try
    {
        auto state = std::make_unique<CollisionGeometry::implementation>();
        state->runtime = m_state->runtime;
        state->kind = kind;
        physx::PxDefaultMemoryInputData stream(reinterpret_cast<physx::PxU8*>(const_cast<std::byte*>(bytes.data())),
                                              static_cast<physx::PxU32>(bytes.size()));
        switch (kind)
        {
        case geometry_kind::convex:
            state->convex.reset(m_state->runtime->physics->createConvexMesh(stream));
            if (state->convex) state->gpu_compatible = state->convex->isGpuCompatible();
            break;
        case geometry_kind::triangle_mesh:
            state->triangle.reset(m_state->runtime->physics->createTriangleMesh(stream));
            state->gpu_compatible = true;
            break;
        case geometry_kind::heightfield:
            state->heightfield.reset(m_state->runtime->physics->createHeightField(stream));
            state->gpu_compatible = true;
            break;
        default:
            return std::unexpected(error{error_code::invalid_argument, 0, "Unknown cooked geometry kind"});
        }
        if (!state->convex && !state->triangle && !state->heightfield)
            return std::unexpected(error{error_code::invalid_argument, 0, "SDK cooked geometry load failed"});

        return std::shared_ptr<const CollisionGeometry>(new CollisionGeometry(std::move(state)));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Cooked geometry load allocation failed"});
    }
}

result<std::shared_ptr<const CollisionGeometry>> PhysicsScene::cook_convex(std::span<const math::vector3> points,
                                                                           std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    ce::profile_scope scope{ce::marker<"Physics.CookConvex">()};

    if (points.size() < 4 || points.size() > UINT32_MAX || !std::ranges::all_of(points, finite) || !convex_has_volume(points))
        return std::unexpected(error{error_code::invalid_argument, 0, "Convex requires finite vertices with nonzero volume", location});
    try
    {
        auto state = std::make_unique<CollisionGeometry::implementation>();
        state->runtime = m_state->runtime;
        state->kind = geometry_kind::convex;
        physx::PxCookingParams params(m_state->runtime->physics->getTolerancesScale());
        params.buildGPUData = true;
        configure_convex_area(params, points);

        physx::PxConvexMeshDesc desc;
        desc.points.stride = sizeof(math::vector3);
        desc.points.data = points.data();
        desc.points.count = static_cast<physx::PxU32>(points.size());
        desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eCHECK_ZERO_AREA_TRIANGLES;
        physx::PxConvexMeshCookingResult::Enum condition;
        state->convex.reset(
            PxCreateConvexMesh(params, desc, m_state->runtime->physics->getPhysicsInsertionCallback(), &condition));
        if (!state->convex)
            return std::unexpected(error{error_code::cooking_failed, static_cast<std::uint32_t>(condition),
                                         "Convex cooking failed", location});
        state->gpu_compatible = state->convex->isGpuCompatible();

        return std::shared_ptr<const CollisionGeometry>(new CollisionGeometry(std::move(state)));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Convex allocation failed", location});
    }
}

result<std::shared_ptr<const CollisionGeometry>> PhysicsScene::cook_triangle_mesh(
    std::span<const math::vector3> points, std::span<const triangle_indices> triangles, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    ce::profile_scope scope{ce::marker<"Physics.CookTriangleMesh">()};

    if (points.size() < 3 || points.size() > UINT32_MAX || triangles.empty() || triangles.size() > UINT32_MAX ||
        !std::ranges::all_of(points, finite) || !std::ranges::all_of(triangles, [&](auto t) {
            return t.a < points.size() && t.b < points.size() && t.c < points.size() && t.a != t.b && t.a != t.c &&
                   t.b != t.c;
        }))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid triangle vertex/index data", location});
    try
    {
        auto state = std::make_unique<CollisionGeometry::implementation>();
        state->runtime = m_state->runtime;
        state->kind = geometry_kind::triangle_mesh;
        physx::PxCookingParams params(m_state->runtime->physics->getTolerancesScale());
        params.buildGPUData = true;
        physx::PxTriangleMeshDesc desc;
        desc.points.stride = sizeof(math::vector3);
        desc.points.data = points.data();
        desc.points.count = static_cast<physx::PxU32>(points.size());
        desc.triangles.stride = sizeof(triangle_indices);
        desc.triangles.data = triangles.data();
        desc.triangles.count = static_cast<physx::PxU32>(triangles.size());
        physx::PxTriangleMeshCookingResult::Enum condition;
        state->triangle.reset(
            PxCreateTriangleMesh(params, desc, m_state->runtime->physics->getPhysicsInsertionCallback(), &condition));
        if (!state->triangle)
            return std::unexpected(error{error_code::cooking_failed, static_cast<std::uint32_t>(condition),
                                         "Triangle cooking failed", location});
        state->gpu_compatible = true;

        return std::shared_ptr<const CollisionGeometry>(new CollisionGeometry(std::move(state)));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Triangle allocation failed", location});
    }
}

result<std::shared_ptr<const CollisionGeometry>> PhysicsScene::cook_heightfield(const heightfield_desc& input,
                                                                                std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    ce::profile_scope scope{ce::marker<"Physics.CookHeightfield">()};

    const auto count = static_cast<std::uint64_t>(input.rows) * input.columns;
    if (input.rows < 2 || input.columns < 2 || count > UINT32_MAX || count != input.heights.size())
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid heightfield dimensions", location});
    try
    {
        auto state = std::make_unique<CollisionGeometry::implementation>();
        state->runtime = m_state->runtime;
        state->kind = geometry_kind::heightfield;
        std::vector<physx::PxHeightFieldSample> samples(static_cast<std::size_t>(count));
        const std::mdspan<const std::int16_t, std::dextents<std::size_t, 2>> heights(input.heights.data(), input.rows,
                                                                                     input.columns);
        for (auto row : std::views::iota(0u, input.rows))
            for (auto column : std::views::iota(0u, input.columns))
            {
                auto& sample = samples[static_cast<std::size_t>(row) * input.columns + column];
                sample.height = heights[row, column];
                sample.materialIndex0 = 0;
                sample.materialIndex1 = 0;
                sample.clearTessFlag();
            }
        physx::PxHeightFieldDesc desc;
        desc.nbRows = input.rows;
        desc.nbColumns = input.columns;
        desc.samples.data = samples.data();
        desc.samples.stride = sizeof(physx::PxHeightFieldSample);
        state->heightfield.reset(PxCreateHeightField(desc, m_state->runtime->physics->getPhysicsInsertionCallback()));
        if (!state->heightfield)
            return std::unexpected(error{error_code::cooking_failed, m_state->runtime->errors.last.load(),
                                         "Heightfield cooking failed", location});
        state->gpu_compatible = true;

        return std::shared_ptr<const CollisionGeometry>(new CollisionGeometry(std::move(state)));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Heightfield allocation failed", location});
    }
}

result<character_handle> PhysicsScene::create_character(const character_desc& desc, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    ce::profile_scope scope{ce::marker<"Physics.CharacterCreate">()};
    if (!valid_character_desc(desc))
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Invalid capsule character definition", location});
    try
    {
        if (!m_state->character_manager)
        {
#if defined(CE_PHYSICS_TESTING)
            if (test::consume(test::failure_point::character_manager))
                return std::unexpected(error{error_code::out_of_memory, 0, "Injected CCT manager failure", location});
#endif
            m_state->character_manager.reset(PxCreateControllerManager(*m_state->scene));
            if (!m_state->character_manager)
                return std::unexpected(error{error_code::out_of_memory, 0, "CCT manager allocation failed", location});
        }
        sdk_owner<physx::PxMaterial> material(m_state->runtime->physics->createMaterial(0, 0, 0));
        if (!material)
            return std::unexpected(
                error{error_code::out_of_memory, 0, "Character material allocation failed", location});
        physx::PxCapsuleControllerDesc sdk_desc;
        sdk_desc.position = physx::PxExtendedVec3(desc.position.x, desc.position.y, desc.position.z);
        sdk_desc.radius = desc.radius;
        sdk_desc.height = desc.cylinder_height;
        sdk_desc.contactOffset = desc.contact_offset;
        sdk_desc.stepOffset = desc.step_offset;
        sdk_desc.slopeLimit = desc.slope_limit_cosine;
        sdk_desc.climbingMode = physx::PxCapsuleClimbingMode::eCONSTRAINED;
        sdk_desc.nonWalkableMode = physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING;
        sdk_desc.material = material.get();
        if (!sdk_desc.isValid())
            return std::unexpected(
                error{error_code::invalid_argument, 0, "Capsule definition exceeds SDK limits", location});
        auto index = m_state->free_character_slot;
        if (index == UINT32_MAX)
        {
            if (m_state->characters.size() == UINT32_MAX)
                return std::unexpected(error{error_code::capacity_exceeded, 0, "Character slots exhausted", location});
            index = static_cast<std::uint32_t>(m_state->characters.size());
            m_state->characters.emplace_back();
            m_state->free_character_slot = index;
        }
        auto& slot = m_state->characters[index];
        auto record = std::make_unique<character_record>();
        record->definition = desc;
        sdk_desc.userData = record.get();
        record->controller.reset(m_state->character_manager->createController(sdk_desc));
        if (!record->controller)
            return std::unexpected(
                error{error_code::out_of_memory, 0, "Character controller allocation failed", location});
#if defined(CE_PHYSICS_TESTING)
        if (test::consume(test::failure_point::character_creation))
            return std::unexpected(error{error_code::out_of_memory, 0, "Injected CCT creation rollback", location});
#endif
        // Query-only CCT actor: collision resolution belongs to move(), not the rigid-body solver.
        auto* actor = record->controller->getActor();
        for (auto i : std::views::iota(0u, actor->getNbShapes()))
        {
            physx::PxShape* shape = nullptr;
            actor->getShapes(&shape, 1, i);
            shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
            shape->userData = nullptr; // Body queries intentionally exclude controller proxies.
        }
        const auto position = record->controller->getPosition();
        const auto foot = record->controller->getFootPosition();
        record->state.position = {float(position.x), float(position.y), float(position.z)};
        record->state.foot_position = {float(foot.x), float(foot.y), float(foot.z)};
        auto generation = slot.generation;
#if defined(CE_PHYSICS_TESTING)
        if (test::consume(test::failure_point::character_generation_limit))
            generation = UINT32_MAX;
#endif
        const character_handle handle{m_state->identity, index, generation};
        m_state->free_character_slot = slot.next_free;
        slot.generation = generation;
        slot.record = std::move(record);
        ++m_state->character_count;
        m_state->note_change(*slot.record);
        return handle;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Character storage allocation failed", location});
    }
}

result<void> PhysicsScene::destroy_character(character_handle handle, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    ce::profile_scope scope{ce::marker<"Physics.CharacterDestroy">()};
    if (handle.scene == m_state->identity && handle.slot < m_state->characters.size())
    {
        const auto& slot = m_state->characters[handle.slot];
        if (!slot.record && handle.generation != 0 && slot.last_destroyed == handle.generation)
            return {};
    }
    auto found = m_state->find(handle, location);
    if (!found)
        return std::unexpected(found.error());
    auto& slot = m_state->characters[handle.slot];
    m_state->note_change(*slot.record);
    --m_state->character_count;
    slot.record.reset();
    slot.last_destroyed = handle.generation;
    if (slot.generation != UINT32_MAX)
        ++slot.generation;
    if (slot.generation != UINT32_MAX)
    {
        slot.next_free = m_state->free_character_slot;
        m_state->free_character_slot = handle.slot;
    }
    return {};
}

result<character_state> PhysicsScene::read_character(character_handle handle, std::source_location location) const
{
    if (auto phase = m_state->require_read(location); !phase)
        return std::unexpected(phase.error());
    return m_state->find(handle, location).transform([](character_record* record) { return record->state; });
}

result<void> PhysicsScene::teleport_character(character_handle handle, math::vector3 position,
                                              std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    auto found = m_state->find(handle, location);
    if (!found)
        return std::unexpected(found.error());
    if (!finite(position))
        return std::unexpected(error{error_code::invalid_argument, 0, "Character position must be finite", location});
    ce::profile_scope scope{ce::marker<"Physics.CharacterTeleport">()};
    auto& record = **found;
    if (!record.controller->setPosition(physx::PxExtendedVec3(position.x, position.y, position.z)))
        return std::unexpected(error{error_code::backend_initialization, 0, "Character teleport failed", location});
    record.controller->invalidateCache();
    record.state = {};
    record.state.position = position;
    const auto foot = record.controller->getFootPosition();
    record.state.foot_position = {float(foot.x), float(foot.y), float(foot.z)};
    m_state->note_change(record);
    return {};
}

result<void> PhysicsScene::set_character_filter(character_handle handle, std::uint32_t belongs,
    std::uint32_t collides, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());

    auto found = m_state->find(handle, location);
    if (!found) return std::unexpected(found.error());

    ce::profile_scope scope{ce::marker<"Physics.CharacterFilterCommit">()};
    auto& record = **found;
    record.definition.belongs_to = belongs;
    record.definition.collides_with = collides;
    record.controller->invalidateCache();
    return {};
}

result<character_state> PhysicsScene::move_character(character_handle handle, const character_move& move,
                                                     std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    auto found = m_state->find(handle, location);
    if (!found)
        return std::unexpected(found.error());
    if (!finite(move.displacement) || !std::isfinite(move.seconds) || move.seconds <= 0 ||
        !std::isfinite(move.minimum_distance) || move.minimum_distance < 0)
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid character move", location});
    ce::profile_scope scope{ce::marker<"Physics.CharacterMovement">()};
    auto& record = **found;
    const auto old = record.state.position;
    character_filter filter(record.definition);
    // Body query-layer bits are a different contract from collision membership.
    // Let the bilateral collision-policy callback decide; SDK query-mask prefiltering
    // would otherwise discard allowed contacts between different common layers.
    const physx::PxFilterData data;
    const physx::PxControllerFilters filters(&data, &filter, &filter);
    const auto flags = record.controller->move(sdk(move.displacement), move.minimum_distance, move.seconds, filters);
    const auto position = record.controller->getPosition();
    const auto foot = record.controller->getFootPosition();
    record.state.position = {float(position.x), float(position.y), float(position.z)};
    record.state.foot_position = {float(foot.x), float(foot.y), float(foot.z)};
    record.state.actual_displacement = record.state.position - old;
    record.state.sides = flags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_SIDES);
    record.state.above = flags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_UP);
    record.state.below = flags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_DOWN);
    m_state->note_change(record);
    return record.state;
}

result<body_handle> PhysicsScene::create_body(const body_desc& desc, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    ce::profile_scope scope{ce::marker<"Physics.BodyCreate">()};

    if (!valid_pose(desc.initial_pose) || !finite(desc.linear_velocity) || !finite(desc.angular_velocity) ||
        !std::isfinite(desc.mass) || desc.mass <= 0 || desc.shapes.empty() || desc.shapes.size() > 65535 ||
        !std::isfinite(desc.linear_damping) || desc.linear_damping < 0 || !std::isfinite(desc.angular_damping) ||
        desc.angular_damping < 0 || std::to_underlying(desc.constraints.translation) > 7 ||
        std::to_underlying(desc.constraints.rotation) > 7 ||
        (desc.kind != body_kind::static_body && desc.kind != body_kind::dynamic && desc.kind != body_kind::kinematic))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid body definition", location});
    if (desc.kind != body_kind::static_body && std::ranges::all_of(desc.shapes, &ShapeInstance::sensor))
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Non-static body requires a solid mass shape", location});
    try
    {
        // Validate the entire definition before allocating any SDK actor/material/shape.
        std::vector<physx::PxGeometryHolder> forms;
        forms.reserve(desc.shapes.size());
        std::vector<std::uint32_t> ids;
        ids.reserve(desc.shapes.size());
        for (const auto& shape : desc.shapes)
        {
            const auto& material = shape.surface;
            if (!shape.id.value || !valid_pose(shape.local_pose) || !std::isfinite(material.static_friction) ||
                !std::isfinite(material.dynamic_friction) || !std::isfinite(material.restitution) ||
                material.static_friction < 0 || material.dynamic_friction < 0 || material.restitution < 0 ||
                material.restitution > 1)
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Invalid shape ID/pose/material", location});
            ids.push_back(shape.id.value);
            auto form = m_state->resolve(shape.form, location);
            if (!form)
                return std::unexpected(form.error());
            const auto type = form->getType();
            if ((type == physx::PxGeometryType::eTRIANGLEMESH || type == physx::PxGeometryType::eHEIGHTFIELD) &&
                (desc.kind != body_kind::static_body || shape.sensor))
                return std::unexpected(error{error_code::unsupported_geometry, 0,
                                             "Mesh/heightfield requires a static solid body", location});
            forms.push_back(*form);
        }
        std::ranges::sort(ids);
        if (std::ranges::adjacent_find(ids) != ids.end())
            return std::unexpected(error{error_code::invalid_argument, 0, "Duplicate shape IDs", location});

        if (m_state->free_body_slot == UINT32_MAX)
        {
            if (m_state->bodies.size() >= UINT32_MAX)
                return std::unexpected(error{error_code::capacity_exceeded, 0, "Body slots exhausted", location});
            m_state->bodies.emplace_back();
            m_state->free_body_slot = static_cast<std::uint32_t>(m_state->bodies.size() - 1);
        }
        // Keep the slot on the intrusive free list until construction commits.
        // Failed SDK/host allocations therefore leave it reusable without allocating a rollback record.
        const auto index = m_state->free_body_slot;
        auto& slot = m_state->bodies[index];
        auto generation = slot.generation;
#if defined(CE_PHYSICS_TESTING)
        if (test::consume(test::failure_point::body_generation_limit))
            generation = UINT32_MAX;
#endif
        const body_handle handle{m_state->identity, index, generation};
        auto record = std::make_unique<body_record>();
        record->kind = desc.kind;
        record->handle = handle;
        record->shapes.reserve(desc.shapes.size());
        record->identities.reserve(desc.shapes.size());
        record->assets.reserve(desc.shapes.size());

        if (desc.kind == body_kind::static_body)
            record->actor.reset(m_state->runtime->physics->createRigidStatic(sdk(desc.initial_pose)));
        else
            record->actor.reset(m_state->runtime->physics->createRigidDynamic(sdk(desc.initial_pose)));
        if (!record->actor)
            return std::unexpected(error{error_code::out_of_memory, 0, "Actor allocation failed", location});

        record->actor->userData = record.get();

        for (auto i : std::views::iota(std::size_t{0}, desc.shapes.size()))
        {
            const auto& shape = desc.shapes[i];
            sdk_owner<physx::PxMaterial> material(m_state->runtime->physics->createMaterial(
                shape.surface.static_friction, shape.surface.dynamic_friction, shape.surface.restitution));
            if (!material)
                return std::unexpected(error{error_code::out_of_memory, 0, "Material allocation failed", location});
            physx::PxShapeFlags flags =
                shape.sensor ? physx::PxShapeFlag::eTRIGGER_SHAPE : physx::PxShapeFlag::eSIMULATION_SHAPE;
            if (shape.query_enabled)
                flags |= physx::PxShapeFlag::eSCENE_QUERY_SHAPE;
            sdk_owner<physx::PxShape> sdk_shape(
                m_state->runtime->physics->createShape(forms[i].any(), *material, true, flags));
            if (!sdk_shape)
                return std::unexpected(error{error_code::backend_initialization, 0, "Shape creation failed", location});
            auto local = sdk(shape.local_pose);
            if (std::holds_alternative<capsule_geometry>(shape.form))
                local = local * physx::PxTransform(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0, 0, 1)));
            sdk_shape->setLocalPose(local);
            sdk_shape->setSimulationFilterData({shape.filter.belongs_to, shape.filter.collides_with, 0, 0});
            sdk_shape->setQueryFilterData({shape.filter.query_layers, 0, 0, 0});
            record->identities.push_back({handle, shape.id, shape.sensor});
            sdk_shape->userData = &record->identities.back();
            if (const auto* cooked = std::get_if<cooked_geometry>(&shape.form))
                record->assets.push_back(cooked->asset);
            if (!record->actor->attachShape(*sdk_shape))
                return std::unexpected(
                    error{error_code::backend_initialization, 0, "Shape attachment failed", location});
            record->shapes.push_back(sdk_shape.get());
#if defined(CE_PHYSICS_TESTING)
            if (test::consume(test::failure_point::body_shape))
                return std::unexpected(
                    error{error_code::backend_initialization, 0, "Injected partial body failure", location});
#endif
        }
        if (auto* dynamic = record->actor->is<physx::PxRigidDynamic>())
        {
            if (!physx::PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, desc.mass))
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Compound mass calculation failed", location});
            dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, desc.kind == body_kind::kinematic);
            dynamic->setLinearDamping(desc.linear_damping);
            dynamic->setAngularDamping(desc.angular_damping);
            dynamic->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !desc.gravity_enabled);
            const auto lock_bits =
                std::to_underlying(desc.constraints.translation) | (std::to_underlying(desc.constraints.rotation) << 3);
            dynamic->setRigidDynamicLockFlags(physx::PxRigidDynamicLockFlags(static_cast<physx::PxU8>(lock_bits)));
            if (desc.kind == body_kind::dynamic)
            {
                dynamic->setLinearVelocity(sdk(desc.linear_velocity));
                dynamic->setAngularVelocity(sdk(desc.angular_velocity));
            }
        }

        m_state->scene->addActor(*record->actor);
        m_state->free_body_slot = slot.next_free;
        slot.next_free = UINT32_MAX;
        slot.generation = generation;
        slot.record = std::move(record);
        ++m_state->body_count;
        m_state->shape_count += slot.record->shapes.size();
        m_state->note_change(*slot.record); // Publish only after full successful construction.
        return handle;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Body allocation failed", location});
    }
}

result<body_handle> PhysicsScene::replace_body(body_handle body, const body_desc& desc, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    if (auto original = m_state->find(body, location); !original)
        return std::unexpected(original.error());

    ce::profile_scope scope{ce::marker<"Physics.BodyReplace">()};
    try
    {
        m_state->retired.reserve(m_state->retired.size() + 2);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            error{error_code::out_of_memory, 0, "Body replacement retirement allocation failed", location});
    }
    auto replacement = create_body(desc, location);
    if (!replacement)
        return std::unexpected(replacement.error());

    const auto removed = destroy_body(body, location);
    if (!removed)
    {
        (void)destroy_body(*replacement); // Reserved rollback storage; old body remains queryable.
        return std::unexpected(removed.error());
    }
    return replacement;
}

result<void> PhysicsScene::set_shape_filter(body_handle body, shape_id shape, collision_filter filter,
                                            std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());

    auto found = m_state->find(body, location);
    if (!found)
        return std::unexpected(found.error());

    auto& record = **found;
    const auto identity = std::ranges::find(record.identities, shape, &shape_identity::shape);
    if (identity == record.identities.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown body shape", location});

    ce::profile_scope commit{ce::marker<"Physics.FilterCommit">()};
    auto* sdk_shape = record.shapes[identity - record.identities.begin()];
    const auto previous_simulation = sdk_shape->getSimulationFilterData();
    const auto previous_query = sdk_shape->getQueryFilterData();
    sdk_shape->setSimulationFilterData({filter.belongs_to, filter.collides_with, 0, 0});
    sdk_shape->setQueryFilterData({filter.query_layers, 0, 0, 0});

    {
        ce::profile_scope refilter{ce::marker<"Physics.Refilter">()};
        if (!m_state->scene->resetFiltering(*record.actor, &sdk_shape, 1))
        {
            sdk_shape->setSimulationFilterData(previous_simulation);
            sdk_shape->setQueryFilterData(previous_query);
            return std::unexpected(error{error_code::backend_initialization, 0, "Shape refilter failed", location});
        }
    }

    m_state->note_change(record);
    return {};
}

result<void> PhysicsScene::destroy_body(body_handle body, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    ce::profile_scope scope{ce::marker<"Physics.BodyDestroy">()};

    if (body.scene == m_state->identity && body.slot < m_state->bodies.size())
    {
        auto& slot = m_state->bodies[body.slot];
        if (!slot.record && body.generation != 0 && slot.last_destroyed == body.generation)
            return {};
    }

    auto record = m_state->find(body, location);
    if (!record)
        return std::unexpected(record.error());
    auto& slot = m_state->bodies[body.slot];
    try
    {
        m_state->retired.reserve(m_state->retired.size() + 1);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            error{error_code::out_of_memory, 0, "Deferred body release allocation failed", location});
    }
    m_state->note_change(*slot.record);
    --m_state->body_count;
    m_state->shape_count -= slot.record->shapes.size();
    m_state->scene->removeActor(*slot.record->actor);
    m_state->retired.push_back(std::move(slot.record));
    slot.last_destroyed = body.generation;
    if (slot.generation != UINT32_MAX)
        ++slot.generation;
    if (slot.generation != UINT32_MAX)
    {
        slot.next_free = m_state->free_body_slot;
        m_state->free_body_slot = body.slot;
    }

    return {};
}

result<body_state> PhysicsScene::read_body(body_handle body, std::source_location location) const
{
    if (auto phase = m_state->require_read(location); !phase)
        return std::unexpected(phase.error());
    auto record = m_state->find(body, location);
    if (!record)
        return std::unexpected(record.error());
    const auto* actor = (*record)->actor.get();
    body_state state{(*record)->kind, engine(actor->getGlobalPose()), {}, {}, 0.f, {}};
    if (const auto* dynamic = actor->is<physx::PxRigidDynamic>())
    {
        state.linear_velocity = engine(dynamic->getLinearVelocity());
        state.angular_velocity = engine(dynamic->getAngularVelocity());
        state.mass = dynamic->getMass();
        state.inertia = engine(dynamic->getMassSpaceInertiaTensor());
        state.center_of_mass = engine(dynamic->getCMassLocalPose());
    }
    return state;
}

result<void> PhysicsScene::set_pose(body_handle body, const pose& value, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    auto record = m_state->find(body, location);
    if (!record)
        return std::unexpected(record.error());
    if (!valid_pose(value))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid body pose", location});
    (*record)->actor->setGlobalPose(sdk(value));
    m_state->note_change(**record);
    return {};
}

result<void> PhysicsScene::set_velocity(body_handle body, math::vector3 linear, math::vector3 angular,
                                        std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    auto record = m_state->find(body, location);
    if (!record)
        return std::unexpected(record.error());
    if ((*record)->kind != body_kind::dynamic || !finite(linear) || !finite(angular))
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Velocity requires finite values and a dynamic body", location});
    auto* actor = (*record)->actor->is<physx::PxRigidDynamic>();
    actor->setLinearVelocity(sdk(linear));
    actor->setAngularVelocity(sdk(angular));
    m_state->note_change(**record);
    return {};
}

result<void> PhysicsScene::set_kinematic_target(body_handle body, const pose& value, std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());
    auto record = m_state->find(body, location);
    if (!record)
        return std::unexpected(record.error());
    if ((*record)->kind != body_kind::kinematic || !valid_pose(value))
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Target requires a kinematic body and valid pose", location});
    (*record)->actor->is<physx::PxRigidDynamic>()->setKinematicTarget(sdk(value));
    m_state->note_change(**record);
    return {};
}

namespace
{
bool valid_direction(math::vector3 direction, float distance)
{
    const float length = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    return finite(direction) && std::abs(length - 1.f) < 1e-4f && std::isfinite(distance) && distance > 0;
}

physx::PxTransform query_pose(const geometry& form, const pose& value)
{
    auto result = sdk(value);
    if (std::holds_alternative<capsule_geometry>(form))
        result = result * physx::PxTransform(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0, 0, 1)));
    return result;
}

physx::PxQueryFilterData query_data()
{
    return physx::PxQueryFilterData(physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC |
                                    physx::PxQueryFlag::ePREFILTER | physx::PxQueryFlag::eNO_BLOCK);
}

} // namespace

result<query_result> PhysicsScene::raycast(math::vector3 origin, math::vector3 direction, float distance,
                                           std::span<query_hit> output, const query_filter& filter,
                                           std::source_location location)
{
    if (auto phase = m_state->require_read(location); !phase)
        return std::unexpected(phase.error());
    if (auto valid = m_state->validate_query_filter(filter, location); !valid)
        return std::unexpected(valid.error());
    if (!finite(origin) || !valid_direction(direction, distance))
        return std::unexpected(error{error_code::invalid_argument, 0,
                                     "Ray requires finite origin/unit direction/positive distance", location});
    scoped_scene_phase read_window{m_state->phase, scene_phase::query_read};
    ce::profile_scope scope{ce::marker<"Physics.Raycast">()};
    query_collector<physx::PxRaycastHit> hits(output);
    query_selection selection(filter);
    m_state->scene->raycast(sdk(origin), sdk(direction), distance, hits, physx::PxHitFlag::eDEFAULT, query_data(),
                            &selection);
    m_state->note_query(hits.summary, 32 * sizeof(physx::PxRaycastHit));
    return hits.summary;
}

result<query_result> PhysicsScene::sweep(const geometry& form, const pose& origin, math::vector3 direction,
                                         float distance, std::span<query_hit> output, const query_filter& filter,
                                         std::source_location location)
{
    if (auto phase = m_state->require_read(location); !phase)
        return std::unexpected(phase.error());
    if (auto valid = m_state->validate_query_filter(filter, location); !valid)
        return std::unexpected(valid.error());
    if (!valid_pose(origin) || !valid_direction(direction, distance))
        return std::unexpected(error{error_code::invalid_argument, 0,
                                     "Sweep requires valid pose/unit direction/positive distance", location});
    auto resolved = m_state->resolve(form, location);
    if (!resolved)
        return std::unexpected(resolved.error());
    if (resolved->getType() == physx::PxGeometryType::eTRIANGLEMESH ||
        resolved->getType() == physx::PxGeometryType::eHEIGHTFIELD)
        return std::unexpected(error{error_code::unsupported_geometry, 0, "Sweep input must be convex", location});
    scoped_scene_phase read_window{m_state->phase, scene_phase::query_read};
    ce::profile_scope scope{ce::marker<"Physics.Sweep">()};
    query_collector<physx::PxSweepHit> hits(output);
    query_selection selection(filter);
    m_state->scene->sweep(resolved->any(), query_pose(form, origin), sdk(direction), distance, hits,
                          physx::PxHitFlag::eDEFAULT, query_data(), &selection);
    m_state->note_query(hits.summary, 32 * sizeof(physx::PxSweepHit));
    return hits.summary;
}

result<query_result> PhysicsScene::overlap(const geometry& form, const pose& origin, std::span<query_hit> output,
                                           const query_filter& filter, std::source_location location)
{
    if (auto phase = m_state->require_read(location); !phase)
        return std::unexpected(phase.error());
    if (auto valid = m_state->validate_query_filter(filter, location); !valid)
        return std::unexpected(valid.error());
    if (!valid_pose(origin))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid overlap pose", location});
    auto resolved = m_state->resolve(form, location);
    if (!resolved)
        return std::unexpected(resolved.error());
    if (resolved->getType() == physx::PxGeometryType::eTRIANGLEMESH ||
        resolved->getType() == physx::PxGeometryType::eHEIGHTFIELD)
        return std::unexpected(error{error_code::unsupported_geometry, 0, "Overlap input must be convex", location});
    scoped_scene_phase read_window{m_state->phase, scene_phase::query_read};
    ce::profile_scope scope{ce::marker<"Physics.Overlap">()};
    query_collector<physx::PxOverlapHit> hits(output);
    query_selection selection(filter);
    m_state->scene->overlap(resolved->any(), query_pose(form, origin), hits, query_data(), &selection);
    m_state->note_query(hits.summary, 32 * sizeof(physx::PxOverlapHit));
    return hits.summary;
}

result<void> PhysicsScene::query_batch(std::span<const query_request> requests,
                                       std::span<result<query_result>> results,
                                       std::source_location location)
{
    if (auto phase = m_state->require_idle(location); !phase)
        return std::unexpected(phase.error());

    if (results.size() != requests.size())
        return std::unexpected(error{error_code::invalid_argument, 0,
                                     "Batch requires one result slot per request", location});

    if (requests.empty())
        return {};

    ce::profile_context_scope context{{m_state->identity.value,
                                       m_state->tick.load(std::memory_order_relaxed), 0}};
    ce::profile_scope batch{ce::marker<"Physics.QueryBatch">()};
    scoped_scene_phase read_window{m_state->phase, scene_phase::query_read};

    {
        ce::profile_scope update{ce::marker<"Physics.QueryStructureUpdate">()};
        m_state->scene->flushQueryUpdates();
    }

    for (std::size_t index : std::views::iota(std::size_t{0}, requests.size()))
    {
        const auto& request = requests[index];
        results[index] = std::visit([&](const auto& input) -> result<query_result> {
            using input_type = std::remove_cvref_t<decltype(input)>;

            if constexpr (std::same_as<input_type, ray_query>)
                return raycast(input.origin, input.direction, input.distance, request.output, request.filter, location);
            else if constexpr (std::same_as<input_type, sweep_query>)
                return sweep(input.form, input.origin, input.direction, input.distance,
                             request.output, request.filter, location);
            else
                return overlap(input.form, input.origin, request.output, request.filter, location);
        }, request.input);
    }

    return {};
}

} // namespace ce::physics
