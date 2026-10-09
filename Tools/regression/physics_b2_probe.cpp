#include "../../Engine/SceneRuntime/ScenePhysicsSimulation.h"
#include "../../Engine/SceneRuntime/PhysicsTransformPolicy.h"
#include "../../Engine/SceneRuntime/PhysicsLifecyclePolicy.h"
#include <array>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include "../../Engine/Physics/PhysicsTestHooks.h"
#if !CE_SHIPPING
#include "../../Engine/EngineDiagnostics/ProfileScope.h"
#include "../../Engine/EngineDiagnostics/ProfileCaptureFile.h"
#endif

using namespace ce::physics;
#if defined(CE_PHYSICS_TESTING)
// Synthetic entries exercise the real 65,536-entry guard without 65,536 SDK actors.
// The production class has no test access or fault injection.
struct ScenePhysicsRetirementProbe
{
    static void Fill(ScenePhysicsSimulation& session, ScenePhysicsSimulation::binding_id id)
    {
        for (std::uint64_t index = 0; index < 65536; ++index)
            session.m_retiredContactHandles.emplace((0xfffffff0ull << 32) | index, id);
    }

    static std::size_t Pending(const ScenePhysicsSimulation& session) { return session.m_retiredContactHandles.size(); }
    static const body_definition& Definition(const ScenePhysicsSimulation& session, ScenePhysicsSimulation::binding_id id)
    {
        return session.m_entries.at(id).definition;
    }

    static bool Enabled(const ScenePhysicsSimulation& session, ScenePhysicsSimulation::binding_id id)
    {
        return session.m_entries.at(id).enabled;
    }
};
#endif

namespace {
int checks = 0;
#if !CE_SHIPPING
std::uint32_t captureFrame = 1;
#endif

auto advance_frame(ScenePhysicsSimulation& session, double seconds)
{
    auto result = session.Advance(seconds);
#if !CE_SHIPPING
    // Model the product EndOfFrame boundary instead of placing hundreds of ticks
    // into one unbounded probe frame. The first boundary is opened in main.
    ce::profiler().publish_frame(++captureFrame);
    ce::profiler().wait_until_idle();
#endif
    return result;
}

void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "B2 failure: " << message << '\n'; std::exit(1); }
}
bool close(float a, float b) { return std::abs(a-b) < .001f; }
void contact_stream_source() {
    ScenePhysicsSimulation session;
    body_definition trigger;
    trigger.properties.kind = body_kind::static_body;
    ShapeInstance sensor;
    sensor.sensor = true;
    sensor.id = shape_id{17};
    sensor.contact_role = {0x67adfded, 0x47c8, 0x4ef9, {0x9c, 0x38, 0xd1, 0xc3, 0x49, 0x7a, 0x74, 0x21}};
    trigger.shapes.push_back(sensor);
    auto first = session.Register(trigger, true);
    body_definition moving;
    moving.properties.kind = body_kind::dynamic;
    moving.properties.gravity_enabled = false;
    ShapeInstance solid;
    solid.id = shape_id{23};
    solid.contact_role = {0x7d352a65, 0xf9bd, 0x4bd5, {0x83, 0xcf, 0x97, 0xd1, 0xd2, 0x76, 0xf7, 0x40}};
    moving.shapes.push_back(solid);
    auto second = session.Register(moving, true);
    check(first && second, "contact bodies register");
    check(bool(session.Start()), "contact session starts");
    const auto stepped = advance_frame(session, session.fixed_seconds * 4);
    check(stepped && *stepped == 4, "contact catchup ticks");
    check(!session.Contacts().empty(), "earlier catchup sensor begin retained");
    check(session.Contacts().size() == 4, "sensor one event per catchup tick");
    for (std::size_t index = 0; index < session.Contacts().size(); ++index)
        check(session.Contacts()[index].tick.value == index + 1 &&
              session.Contacts()[index].value.kind == (index == 0 ? event_kind::sensor_enter : event_kind::sensor_persist),
              "initial sensor Begin followed by tick Persist");
    const auto& event = session.Contacts().front();
    check(event.tick.value == 1 && event.value.kind == event_kind::sensor_enter, "contact original tick and kind");
    check((event.first == *first && event.second == *second) ||
          (event.first == *second && event.second == *first), "contact binding identity");
    check((event.value.first.shape.value == 17 && event.value.second.shape.value == 23) ||
          (event.value.first.shape.value == 23 && event.value.second.shape.value == 17), "contact shape identity");
    check((event.value.first.shape.value == 17 ? event.value.first.role : event.value.second.role) == sensor.contact_role,
          "sensor role preserved in SDK snapshot");
    check((event.value.first.shape.value == 23 ? event.value.first.role : event.value.second.role) == solid.contact_role,
          "solid role preserved in SDK snapshot");

    const auto previousBody = session.Handle(*second);
    moving.shapes.front().contact_role = sensor.contact_role;
    check(bool(session.Replace(*second, moving)), "replace contacted body");
    check(bool(advance_frame(session, 0)), "replacement survives no-step frame");
    check(bool(advance_frame(session, session.fixed_seconds)), "replacement lost pair resolves retired binding");
    for (const auto& replaced : session.Contacts())
    {
        check((replaced.first == *first && replaced.second == *second) ||
              (replaced.first == *second && replaced.second == *first), "replacement event stable component bindings");
        const auto& endpoint = replaced.value.first.shape.value == 23 ? replaced.value.first : replaced.value.second;
        check(endpoint.role == (endpoint.body == previousBody ? solid.contact_role : sensor.contact_role),
              "replacement event retains original generation role");
    }
    check(bool(advance_frame(session, 0)) && session.Contacts().empty(), "no-step contact no replay");
    check(bool(session.SetPose(*second, pose{{10, 0, 0}, {0, 0, 0, 1}})), "leave sensor");
    check(bool(advance_frame(session, session.fixed_seconds)), "sensor exit step");
    check(!session.Contacts().empty() && session.Contacts().front().value.kind == event_kind::sensor_exit, "sensor exit delivered");
    check(bool(session.SetPose(*second, pose{{0, 0, 0}, {0, 0, 0, 1}})), "return to sensor before disable");
    check(bool(advance_frame(session, session.fixed_seconds)), "sensor re-entry before disable");
    const auto disabledBody = session.Handle(*second);
    check(bool(session.SetEnabled(*second, false)), "disable overlapping body");
    check(bool(advance_frame(session, 0)) && session.Contacts().empty(), "disabled retired binding survives no-step");
    check(bool(advance_frame(session, session.fixed_seconds)), "disabled End resolves retired binding");
    bool disabledEnd = false;
    for (const auto& removed : session.Contacts())
    {
        check(removed.value.kind != event_kind::sensor_persist, "disabled body emits no stale Persist");
        disabledEnd |= removed.value.kind == event_kind::sensor_exit &&
            (removed.value.first.body == disabledBody || removed.value.second.body == disabledBody);
    }
    check(disabledEnd, "disable delivers old-generation sensor End");
    check(bool(advance_frame(session, session.fixed_seconds)) && session.Contacts().empty(), "disable End not replayed");
    check(bool(session.SetEnabled(*second, true)), "enable creates new body generation");
    check(session.Handle(*second) != disabledBody, "enabled body generation isolated");
    check(bool(advance_frame(session, session.fixed_seconds)) && !session.Contacts().empty() &&
          session.Contacts().front().value.kind == event_kind::sensor_enter, "enable emits new sensor Begin");
    const auto deletedBody = session.Handle(*second);
    check(bool(session.Unregister(*second)), "unregister overlapping body");
    check(bool(session.Unregister(*second)), "unregister idempotent");
    check(bool(advance_frame(session, 0)), "deleted binding survives no-step");
    check(bool(advance_frame(session, session.fixed_seconds)), "deleted End resolves retired binding");
    bool deletedEnd = false;
    for (const auto& removed : session.Contacts())
    {
        check(removed.value.kind != event_kind::sensor_persist, "deleted body emits no stale Persist");
        deletedEnd |= removed.value.kind == event_kind::sensor_exit &&
            (removed.value.first.body == deletedBody || removed.value.second.body == deletedBody);
        check(removed.first == *second || removed.second == *second, "deleted stable binding retained for End");
    }
    check(deletedEnd, "unregister delivers old-generation sensor End");
    check(bool(advance_frame(session, session.fixed_seconds)) && session.Contacts().empty(), "deleted End not replayed");
    check(bool(session.Stop()) && session.Contacts().empty(), "stop clears contacts");
}

#if defined(CE_PHYSICS_TESTING)
void lifecycle_failure_policy()
{
    ScenePhysicsSimulation session;
    body_definition definition;
    definition.properties.kind = body_kind::dynamic;
    definition.properties.gravity_enabled = false;
    definition.shapes.push_back(ShapeInstance{});
    auto owner = session.Register(definition, true);
    check(owner && bool(session.Start()), "lifecycle failure policy session starts");
    const auto original = session.Handle(*owner);
    ScenePhysicsRetirementProbe::Fill(session, *owner);
    bool componentEnabled = false; // Local state already changed before OnDisable.
    int reported = 0;
    auto disabled = ApplyPhysicsEnabledTransition(false,
        [&](bool value) { return session.SetEnabled(*owner, value); },
        [&](bool value) { componentEnabled = value; },
        [&](const error& failure) {
            check(failure.code == error_code::capacity_exceeded && componentEnabled,
                  "Disable reports original failure after restoring component state");
            ++reported;
        });
    check(!disabled && componentEnabled && session.Handle(*owner) == original &&
          ScenePhysicsRetirementProbe::Enabled(session, *owner), "Disable failure keeps frontend and SDK membership aligned");

    int detaches = 0;
    bool quiesced = false;
    auto removed = RetirePhysicsOwner(
        [&] {
            ++detaches;
            if (detaches == 2) check(quiesced && !session.IsRunning(), "retry unregister only after SDK quiescence");
            return session.Unregister(*owner);
        },
        [&] {
            check(reported == 2, "removal failure reported before SDK teardown");
            auto stopped = session.Stop();
            quiesced = bool(stopped);
            return stopped;
        },
        [&](const error& failure) {
            check(failure.code == error_code::capacity_exceeded, "removal reports original failure");
            ++reported;
        });
    check(removed && detaches == 2 && quiesced && !session.Handle(*owner) &&
          !session.Read(*owner) && ScenePhysicsRetirementProbe::Pending(session) == 0,
          "failed retirement recovers before owner can be released");

    int stops = 0, reports = 0;
    auto ordinary = RetirePhysicsOwner([]() -> result<void> { return {}; },
        [&]() -> result<void> { ++stops; return {}; }, [&](const error&) { ++reports; });
    check(ordinary && stops == 0 && reports == 0, "normal unregister does not stop physics or report failure");
    int attempts = 0;
    auto failedStop = RetirePhysicsOwner(
        [&]() -> result<void> { ++attempts; return std::unexpected(error{error_code::capacity_exceeded}); },
        []() -> result<void> { return std::unexpected(error{error_code::wrong_phase}); },
        [&](const error&) { ++reports; });
    check(!failedStop && failedStop.error().code == error_code::wrong_phase && attempts == 1,
          "failed quiescence blocks retry and owner release");
    attempts = 0;
    auto failedRetry = RetirePhysicsOwner(
        [&]() -> result<void> { ++attempts; return std::unexpected(error{error_code::stale_handle}); },
        []() -> result<void> { return {}; }, [&](const error&) { ++reports; });
    check(!failedRetry && attempts == 2 && failedRetry.error().code == error_code::stale_handle,
          "failed unregister retry remains an ownership drain failure");
    bool restored = false;
    auto enabled = ApplyPhysicsEnabledTransition(true, [](bool) -> result<void> { return {}; },
        [&](bool) { restored = true; }, [&](const error&) { ++reports; });
    check(enabled && !restored, "successful enable does not roll back component state");
    ScenePhysicsSimulation characters;
    ScenePhysicsSimulation::character_definition capsule;
    auto character = characters.RegisterCharacter(capsule, false);
    check(character && bool(characters.Start()), "character failure policy session starts disabled");
    bool characterEnabled = true;
    int characterReports = 0;
    test::next_failure.store(test::failure_point::character_creation);
    auto enableFailed = ApplyPhysicsEnabledTransition(true,
        [&](bool value) { return characters.SetCharacterEnabled(*character, value); },
        [&](bool value) { characterEnabled = value; },
        [&](const error& failure) {
            check(failure.code == error_code::out_of_memory && !characterEnabled,
                  "character Enable restores local state before reporting SDK failure");
            ++characterReports;
        });
    check(!enableFailed && !characterEnabled && !characters.CharacterHandle(*character) && characterReports == 1,
          "character Enable failure retains disabled SDK membership");
    check(bool(characters.SetCharacterEnabled(*character, true)) && bool(characters.CharacterHandle(*character)),
          "character Enable retry succeeds");
    auto characterRemoved = RetirePhysicsOwner([&] { return characters.UnregisterCharacter(*character); },
        [&] { return characters.Stop(); }, [&](const error&) { ++characterReports; });
    check(characterRemoved && characters.IsRunning() && characterReports == 1,
          "normal character removal does not tear down unrelated physics");
    check(bool(characters.Stop()), "character policy session stops");
    std::cerr << "[physics.lifecycle.failure] disableRollback=passed removalRecovery=passed abortRecovery=passed\n";
}

void retirement_rollback()
{
    ScenePhysicsSimulation session;
    body_definition trigger;
    trigger.properties.kind = body_kind::static_body;
    trigger.shapes.push_back(ShapeInstance{});
    trigger.shapes.front().sensor = true;
    trigger.shapes.front().id = shape_id{17};
    auto first = session.Register(trigger, true);
    body_definition target;
    target.properties.kind = body_kind::dynamic;
    target.properties.gravity_enabled = false;
    target.shapes.push_back(ShapeInstance{});
    target.shapes.front().id = shape_id{23};
    target.shapes.front().contact_role.a = 1;
    auto second = session.Register(target, true);
    check(first && second && bool(session.Start()), "retirement rollback session starts");
    check(bool(advance_frame(session, session.fixed_seconds)), "rollback initial overlap");
    const auto original = session.Handle(*second);
    check(bool(session.SetVelocity(*second, {.25f, .5f, .75f}, {.1f, .2f, .3f})), "rollback nonzero velocities");
    auto before = session.Read(*second);
    check(bool(before), "rollback state before failure");
    auto changed = target;
    changed.shapes.front().contact_role.a = 2;

    const auto intact = [&] {
        auto after = session.Read(*second);
        auto binding = session.Binding(original);
        check(after && binding && *binding == *second && session.Handle(*second) == original,
              "failed operation preserves body and live binding");
        check(ScenePhysicsRetirementProbe::Enabled(session, *second) &&
              ScenePhysicsRetirementProbe::Definition(session, *second).shapes.front().contact_role == target.shapes.front().contact_role,
              "failed operation preserves enabled membership and original role definition");
        check(after && close(after->mass, before->mass) && after->kind == before->kind &&
              close(after->transform.position.x, before->transform.position.x) &&
              close(after->transform.position.y, before->transform.position.y) &&
              close(after->transform.position.z, before->transform.position.z) &&
              close(after->transform.rotation.x, before->transform.rotation.x) &&
              close(after->transform.rotation.y, before->transform.rotation.y) &&
              close(after->transform.rotation.z, before->transform.rotation.z) &&
              close(after->transform.rotation.w, before->transform.rotation.w) &&
              close(after->linear_velocity.x, before->linear_velocity.x) &&
              close(after->linear_velocity.y, before->linear_velocity.y) &&
              close(after->linear_velocity.z, before->linear_velocity.z) &&
              close(after->angular_velocity.x, before->angular_velocity.x) &&
              close(after->angular_velocity.y, before->angular_velocity.y) &&
              close(after->angular_velocity.z, before->angular_velocity.z), "failed operation preserves pose and velocities");
    };
    const auto failed = [&](const result<void>& result, error_code expected) {
        check(!result && result.error().code == expected, "retirement operation reports expected failure");
        intact();
    };

    ScenePhysicsRetirementProbe::Fill(session, *second);
    check(ScenePhysicsRetirementProbe::Pending(session) == 65536, "real pending retirement capacity boundary filled");
    failed(session.Replace(*second, changed), error_code::capacity_exceeded);
    failed(session.SetEnabled(*second, false), error_code::capacity_exceeded);
    failed(session.Unregister(*second), error_code::capacity_exceeded);
    check(bool(advance_frame(session, 0)) && ScenePhysicsRetirementProbe::Pending(session) == 65536,
          "no-step frame does not release retirement budget");
    check(bool(advance_frame(session, session.fixed_seconds)) && ScenePhysicsRetirementProbe::Pending(session) == 0,
          "valid tick releases retirement budget");
    check(session.Contacts().size() == 1 && session.Contacts().front().value.kind == event_kind::sensor_persist,
          "capacity failures preserve overlap without phantom Begin or End");
    before = session.Read(*second);

    for (auto point : {test::failure_point::replacement_map_allocation, test::failure_point::replacement_retired_allocation})
    {
        test::next_failure.store(point);
        failed(session.Replace(*second, changed), error_code::out_of_memory);
        check(test::next_failure.load() == test::failure_point::none && ScenePhysicsRetirementProbe::Pending(session) == 0,
              "map preparation fault consumed without retirement commit");
    }
    test::next_failure.store(test::failure_point::retirement_map_allocation);
    failed(session.SetEnabled(*second, false), error_code::out_of_memory);
    test::next_failure.store(test::failure_point::retirement_map_allocation);
    failed(session.Unregister(*second), error_code::out_of_memory);
    test::next_failure.store(test::failure_point::body_shape);
    failed(session.Replace(*second, changed), error_code::backend_initialization);
    check(ScenePhysicsRetirementProbe::Pending(session) == 0, "SDK construction failure does not commit retired map");
    auto invalid = changed;
    invalid.shapes.front().form = sphere_geometry{-1};
    failed(session.Replace(*second, invalid), error_code::invalid_argument);
    check(bool(advance_frame(session, session.fixed_seconds)) && session.Contacts().size() == 1 &&
          session.Contacts().front().value.kind == event_kind::sensor_persist,
          "allocation and SDK failures preserve active sensor pair");

    check(bool(session.Replace(*second, changed)), "replacement succeeds after failures and budget release");
    const auto replacement = session.Handle(*second);
    check(replacement != original && ScenePhysicsRetirementProbe::Pending(session) == 1,
          "successful retry commits one new body and one retired endpoint");
    check(bool(advance_frame(session, 0)) && ScenePhysicsRetirementProbe::Pending(session) == 1,
          "successful retry retirement survives no-step");
    check(bool(advance_frame(session, session.fixed_seconds)), "successful retry contact publication");
    int ends = 0, begins = 0;
    for (const auto& event : session.Contacts())
    {
        const auto& endpoint = event.value.first.shape.value == 23 ? event.value.first : event.value.second;
        if (event.value.kind == event_kind::sensor_exit)
        {
            ++ends;
            check(endpoint.body == original && endpoint.role == target.shapes.front().contact_role,
                  "retry End retains old body and old role");
        }
        else if (event.value.kind == event_kind::sensor_enter)
        {
            ++begins;
            check(endpoint.body == replacement && endpoint.role == changed.shapes.front().contact_role,
                  "retry Begin carries new body and new role");
        }
    }
    check(ends == 1 && begins == 1 && ScenePhysicsRetirementProbe::Pending(session) == 0,
          "successful retry produces one End and one Begin without ghost actor");
    check(bool(advance_frame(session, session.fixed_seconds)) && session.Contacts().size() == 1 &&
          session.Contacts().front().value.kind == event_kind::sensor_persist, "successful retry has no contact replay");
    check(bool(session.SetEnabled(*second, false)), "disable retry succeeds");
    check(bool(advance_frame(session, session.fixed_seconds)), "disable retry End publishes");
    check(bool(session.SetEnabled(*second, true)), "enable retry succeeds");
    check(bool(advance_frame(session, session.fixed_seconds)), "enable retry Begin publishes");
    check(bool(session.Unregister(*second)), "unregister retry succeeds");
    check(bool(advance_frame(session, session.fixed_seconds)), "unregister retry End publishes");
    check(bool(session.Stop()) && ScenePhysicsRetirementProbe::Pending(session) == 0, "stop clears rollback session");
    std::cerr << "[physics.retirement.rollback] capacity=65536 failures=9 retry=passed\n";
}
#endif

bool exercise(execution_preference preference) {
    ScenePhysicsSimulation session;
    body_definition moving;
    moving.properties.kind = body_kind::dynamic;
    moving.properties.gravity_enabled = false;
    moving.properties.linear_velocity = {6, 0, 0};
    moving.properties.initial_pose.position = {0, 10, 0};
    moving.shapes.push_back(ShapeInstance{});
    auto dynamic = session.Register(moving, true);
    check(bool(dynamic), "dynamic registration");
    auto fixed = moving;
    fixed.properties.kind = body_kind::static_body;
    fixed.properties.initial_pose.position = {0, -10, 0};
    auto stationary = session.Register(fixed, true);
    check(bool(stationary), "static registration");
    auto kinematic = fixed;
    kinematic.properties.kind = body_kind::kinematic;
    kinematic.properties.initial_pose.position = {0, 20, 0};
    auto target = session.Register(kinematic, true);
    check(bool(target), "kinematic registration");
    check(!session.SetPose(*dynamic, {}), "Editor rejects runtime pose");
    scene_config config;
    config.execution = preference;
    check(bool(session.Start(config)), "session start");
    bool gpu = session.Runtime()->status().backend == execution_backend::gpu;
    auto step = advance_frame(session, session.fixed_seconds);
    check(step && *step == 1, "one fixed tick");
    check(close(session.Read(*dynamic)->transform.position.x, .1f), "completed authoritative pose");
    check(close(session.RenderPose(*dynamic)->position.x, 0), "alpha zero uses previous pose");
    check(session.RenderPoses().size() == 1, "static body absent from render work");
    auto half = advance_frame(session, session.fixed_seconds*.5);
    check(half && *half == 0, "no tick frame");
    check(close(session.RenderPose(*dynamic)->position.x, .05f), "no tick frame interpolates midpoint");
    check(close(session.Read(*dynamic)->transform.position.x, .1f), "render interpolation does not alter SDK state");
    check(session.ChangedPoses().empty(), "no tick frame makes no Transform writes");
    pose jump{{4, 10, 0}, {0,0,0,1}};
    check(bool(session.SetPose(*dynamic, jump)), "dynamic teleport");
    check(close(session.RenderPose(*dynamic)->position.x, 4), "teleport resets history");
    pose ground{{3, -10, 0}, {0,0,0,1}};
    check(bool(session.SetPose(*stationary, ground)), "static explicit move");
    check(close(session.Read(*stationary)->transform.position.x, 3), "static query state updates immediately");
    pose goal{{2, 20, 0}, {0,0,0,1}};
    check(bool(session.SetKinematicTarget(*target, goal)), "kinematic target request");
    check(close(session.Read(*target)->transform.position.x, 0), "target is not completed state");
    auto multiple = advance_frame(session, session.fixed_seconds*2);
    check(multiple && *multiple == 2, "multi tick frame");
    check(close(session.Read(*target)->transform.position.x, 2), "kinematic target completed");
    check(close(session.RenderPose(*dynamic)->position.x, 4.15f), "multi tick keeps last two completed poses");
    check(!session.SetKinematicTarget(*dynamic, goal), "dynamic rejects kinematic target");
    check(bool(session.SetVelocity(*dynamic, {}, {})), "stop moving body");
    for (int i=0; i<200; ++i)
        check(bool(advance_frame(session, session.fixed_seconds)), "sleep settling tick");
    check(session.RenderPoses().empty(), "sleeping bodies leave render work list");
    check(bool(session.SetEnabled(*dynamic, false)), "disable");
    check(close(session.RenderPose(*dynamic)->position.x, session.Read(*dynamic)->transform.position.x), "disabled history resets");
    check(!session.SetPose(*dynamic, jump), "disabled rejects teleport");
    check(bool(session.Unregister(*dynamic)), "unregister");
    check(!session.RenderPose(*dynamic), "stale render binding rejects");
    check(bool(session.Stop()), "stop");
    check(session.RenderPoses().empty() && session.InterpolationAlpha()==0, "Stop clears history and alpha");
    check(bool(session.Start(config)) && close(session.Read(*stationary)->transform.position.x,0), "replay restores authored static pose");
    check(bool(session.Stop()), "final teardown");
    return gpu;
}
}
int main(int argc, char** argv) {
#if !CE_SHIPPING
    auto& profiler = ce::profiler();
    ce::profiler_config captureConfig;
    // This probe produces hundreds of worker frames without real-time pacing.
    // Keep a bounded rolling window; exhaustion still fails acceptance below.
    captureConfig.retained_frames = 64;
    profiler.initialize(captureConfig);
    profiler.register_thread("PhysicsB2Probe", ce::track_kind::game_thread);
    profiler.record(1);
    profiler.wait_until_idle();
    profiler.publish_frame(captureFrame);
    profiler.wait_until_idle();
#endif
    const auto rotation = math::quaternion_from_axis_angle(math::vector3::unit_y(), .5f);
    check(PhysicsScalesNear({1, 1, 1}, {1, 1 + 2e-7f, 1}), "scale rounding does not replace geometry");
    check(!PhysicsScalesNear({1, 1, 1}, {1, 1.001f, 1}), "authored scale change still replaces geometry");
    const auto parent = math::compose(math::vector3{2,2,2}, rotation, math::vector3{3,4,5});
    const auto local = math::compose(math::vector3{1,1,1}, math::quaternion{0,0,0,1}, math::vector3{1,0,0});
    auto captured = CapturePhysicsTransform(local*parent);
    check(captured && close(captured->scale.x,2), "rotated scaled parent accepted");
    const auto shear = math::compose(math::vector3{1,1,1}, rotation, math::vector3{}) *
                       math::compose(math::vector3{2,1,1}, math::quaternion{0,0,0,1}, math::vector3{});
    check(!CapturePhysicsTransform(shear), "parent induced shear rejected");
    unsigned roundedHomogeneous = 0;
    for (unsigned pose = 1; pose <= 128; ++pose)
    {
        const auto rotation = math::quaternion_from_axis_angle(math::vector3::unit_y(), pose * .017f);
        const auto bodyWorld = math::compose(math::vector3{1.3f, 2.1f, .7f}, rotation, math::vector3{1, 2, 3});
        const auto interpolated = math::compose(math::vector3{1.3f, 2.1f, .7f}, rotation, math::vector3{4, 5, 6});
        const auto oldTransport = local * bodyWorld * math::inverse(bodyWorld) * interpolated;
        roundedHomogeneous += oldTransport.m[3][3] != 1.f;
        const auto rendered = PhysicsRenderDescendantMatrix(local * bodyWorld, bodyWorld, interpolated);
        check(rendered.m[0][3] == 0.f && rendered.m[1][3] == 0.f &&
              rendered.m[2][3] == 0.f && rendered.m[3][3] == 1.f, "descendant render matrix remains exactly affine");
        check(PhysicsTransformsNear(rendered, local * interpolated, 1e-4f), "descendant retains local offset and scale");
    }
    std::cerr << "[physics.render.affine] roundedLegacy=" << roundedHomogeneous << " poses=128\n";
    const auto singular = math::compose(math::vector3{0, 1, 1}, rotation, math::vector3{});
    check(PhysicsTransformsNear(PhysicsRenderDescendantMatrix(local, singular, parent), local),
          "singular parent preserves current world matrix");
    contact_stream_source();
#if defined(CE_PHYSICS_TESTING)
    retirement_rollback();
    lifecycle_failure_policy();
#endif
    exercise(execution_preference::cpu);
    const auto gpu = exercise(execution_preference::prefer_gpu);
#if !CE_SHIPPING
    profiler.publish_frame(++captureFrame);
    profiler.pause();
    profiler.wait_until_idle();
    const auto capture = profiler.capture();
    const auto summary = profiler.summary();
    std::cerr << "[contact.loss] dropped=" << summary.dropped_events << " counters=" << summary.dropped_counters
              << " lateEvents=" << summary.late_events_dropped << " lateSpans=" << summary.late_spans_dropped
              << " frames=" << summary.collector_dropped_frames << '\n';
    std::cerr << "[contact.pool] chunks=" << summary.chunk_count << " free=" << summary.free_chunks << '\n';
    std::cerr << "[contact.capture] present=" << bool(capture) << " complete=" << (capture && capture->complete()) << " unacked=" << (capture ? capture->unacked_streams() : 0) << " checks=" << checks << '\n';
    check(capture && capture->complete() && capture->unacked_streams()==0, "profiling capture complete");
    check(summary.dropped_events == 0 && summary.dropped_counters == 0 &&
          summary.late_events_dropped == 0 && summary.late_spans_dropped == 0 &&
          summary.collector_dropped_frames == 0, "profiling source and frame loss zero");
    check(capture->frame_count() <= 64, "profiling retention bounded");
    unsigned contactScopes = 0, sensorScopes = 0;
    for (const auto& frame : capture->frames())
        for (const auto& event : frame.events)
        {
            if (capture->marker(event.marker).name == "Physics.SensorPersist")
            {
                ++sensorScopes;
                check(event.cpu.session != 0 && event.cpu.tick != 0 && event.cpu.task == 0,
                      "sensor persist capture has scene and fixed tick");
            }
            if (capture->marker(event.marker).name == "Physics.ContactCollect")
            {
                ++contactScopes;
                check(event.cpu.session != 0 && event.cpu.tick != 0 && event.cpu.task == 0,
                      "contact collect capture has scene and fixed tick");
            }
        }
    check(contactScopes != 0, "contact collect samples captured");
    check(sensorScopes != 0, "sensor persist samples captured");
    if(argc==2)
        check(bool(ce::save_capture(*capture,argv[1])), "profiling capture saved");
    profiler.unregister_thread();
    profiler.shutdown();
#else
    (void)argc;
    (void)argv;
#endif
    std::cout << "{\"result\":\"PHYSICS_B2_OK\",\"checks\":" << checks
              << ",\"gpu_verified\":" << (gpu ? "true" : "false") << "}\n";
}
