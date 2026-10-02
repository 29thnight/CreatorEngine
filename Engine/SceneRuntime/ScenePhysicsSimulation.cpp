#include "ScenePhysicsSimulation.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <ranges>

using namespace ce::physics;

ScenePhysicsSimulation::~ScenePhysicsSimulation()
{
    if (m_owner != std::this_thread::get_id())
        std::terminate();
}

result<void> ScenePhysicsSimulation::RequireOwner() const
{
    if (m_owner != std::this_thread::get_id())
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics session owner violation"});
    return {};
}

std::uint64_t ScenePhysicsSimulation::Key(body_handle handle)
{
    return (std::uint64_t{handle.slot} << 32) | handle.generation;
}

void ScenePhysicsSimulation::Reset(entry& value)
{
    value.body = {};
    value.state = {};
    value.state.kind = value.definition.properties.kind;
    value.state.transform = value.definition.properties.initial_pose;
    value.state.linear_velocity = value.definition.properties.linear_velocity;
    value.state.angular_velocity = value.definition.properties.angular_velocity;
    value.state.mass = value.definition.properties.mass;
}

result<ScenePhysicsSimulation::binding_id> ScenePhysicsSimulation::Register(body_definition definition, bool enabled)
{
    if (auto owner = RequireOwner(); !owner)
        return std::unexpected(owner.error());
    if (m_next == UINT64_MAX)
        return std::unexpected(error{error_code::capacity_exceeded, 0, "Binding IDs exhausted"});
    try
    {
        definition.properties.shapes = {};
        const auto id = m_next;
        entry value{std::move(definition), {}, {}, enabled};
        Reset(value);
        // Secure maps and pose publication capacity before creating an SDK body.
        m_changed.reserve(m_entries.size() + 1);
        auto [position, inserted] = m_entries.emplace(id, std::move(value));
        (void)inserted;
        ++m_next;
        if (m_runtime && enabled)
        {
            auto body = m_runtime->create_body(position->second.definition.view());
            if (!body)
            {
                m_entries.erase(position);
                return std::unexpected(body.error());
            }
            try
            {
                m_handles.emplace(Key(*body), id);
            }
            catch (...)
            {
                (void)m_runtime->destroy_body(*body);
                m_entries.erase(position);
                throw;
            }
            position->second.body = *body;
        }
        return id;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Binding allocation failed"});
    }
}

result<void> ScenePhysicsSimulation::Unregister(binding_id id)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    const auto position = m_entries.find(id);
    if (position == m_entries.end())
        return {}; // Lifecycle teardown is idempotent.
    if (position->second.body)
    {
        auto removed = m_runtime->destroy_body(position->second.body);
        if (!removed)
            return removed;
        m_handles.erase(Key(position->second.body));
    }
    m_entries.erase(position);
    std::erase_if(m_changed, [id](const changed_pose& value) { return value.binding == id; });
    return {};
}

result<void> ScenePhysicsSimulation::Define(binding_id id, body_definition definition)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    if (m_runtime)
        return std::unexpected(error{error_code::wrong_phase, 0, "Authoring definition is frozen during play"});
    const auto position = m_entries.find(id);
    if (position == m_entries.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown physics binding"});
    definition.properties.shapes = {};
    position->second.definition = std::move(definition);
    Reset(position->second);
    return {};
}

result<void> ScenePhysicsSimulation::Replace(binding_id id, body_definition definition)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;

    if (!m_runtime || m_runtime->status().phase != scene_phase::idle)
        return std::unexpected(error{error_code::wrong_phase, 0, "Body replacement requires an idle play session"});

    const auto found = m_entries.find(id);
    if (found == m_entries.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown physics binding"});

    ce::profile_scope scope{ce::marker<"Physics.BodyDefinitionReplace">()};
    auto& value = found->second;
    auto state = value.state;
    if (value.body)
    {
        auto current = m_runtime->read_body(value.body);
        if (!current)
            return std::unexpected(current.error());

        state = *current;
    }

    definition.properties.shapes = {};
    auto request = definition.view();
    request.initial_pose = state.transform;
    request.linear_velocity = state.linear_velocity;
    request.angular_velocity = state.angular_velocity;
    try
    {
        if (value.body)
        {
            // Allocate the complete map before retiring the old SDK body.
            auto prepared = m_handles;
            prepared.reserve(prepared.size() + 1);
            auto node = prepared.extract(Key(value.body));
            if (node.empty())
                return std::unexpected(error{error_code::stale_handle, 0, "Body binding map mismatch"});

            auto replacement = m_runtime->replace_body(value.body, request);
            if (!replacement)
                return std::unexpected(replacement.error());

            node.key() = Key(*replacement);
            prepared.insert(std::move(node)); // Existing node and reserved buckets: no allocation.
            m_handles.swap(prepared);
            value.body = *replacement;
        }
        else
        {
            // Disabled bodies validate with the SDK before replacing their retained definition.
            scene_config config;
            config.workers = 1;
            config.execution = m_runtime->status().backend == execution_backend::gpu ? execution_preference::prefer_gpu
                                                                                     : execution_preference::cpu;
            auto validator = PhysicsScene::create(config);
            if (!validator)
                return std::unexpected(validator.error());

            auto candidate = (*validator)->create_body(request);
            if (!candidate)
                return std::unexpected(candidate.error());

            // Private validation scene teardown cannot leave an unbound actor in the live scene.
        }
        value.definition = std::move(definition);
        state.kind = value.definition.properties.kind;
        state.mass = value.definition.properties.mass;
        state.inertia = {};
        value.state = state;
        value.publication = 0;
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Body replacement map allocation failed"});
    }
}

result<void> ScenePhysicsSimulation::SetEnabled(binding_id id, bool enabled)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    const auto position = m_entries.find(id);
    if (position == m_entries.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown physics binding"});
    auto& value = position->second;
    if (value.enabled == enabled)
        return {};
    if (m_runtime)
    {
        if (enabled)
        {
            auto definition = value.definition.view();
            definition.initial_pose = value.state.transform;
            definition.linear_velocity = value.state.linear_velocity;
            definition.angular_velocity = value.state.angular_velocity;
            auto body = m_runtime->create_body(definition);
            if (!body)
                return std::unexpected(body.error());
            try
            {
                m_handles.emplace(Key(*body), id);
            }
            catch (const std::bad_alloc&)
            {
                (void)m_runtime->destroy_body(*body);
                return std::unexpected(error{error_code::out_of_memory, 0, "Binding map allocation failed"});
            }
            value.body = *body;
        }
        else
        {
            auto state = m_runtime->read_body(value.body);
            if (!state)
                return std::unexpected(state.error());
            auto removed = m_runtime->destroy_body(value.body);
            if (!removed)
                return removed;
            value.state = *state;
            m_handles.erase(Key(value.body));
            value.body = {};
        }
    }
    value.enabled = enabled;
    return {};
}

result<void> ScenePhysicsSimulation::CommitLayers(const project_layer_snapshot& settings,
                                                  std::span<const layer_assignment> assignments)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    ce::profile_scope scope{ce::marker<"Physics.LayerPolicyCommit">()};
    if (m_runtime && m_runtime->status().phase != scene_phase::idle)
        return std::unexpected(error{error_code::wrong_phase, 0, "Layer commit requires idle physics"});
    if (!settings.revision || settings.catalog.revision != settings.revision ||
        settings.policy.revision != settings.revision || settings.revision < m_layerRevision ||
        assignments.size() != m_entries.size() + m_characters.size() ||
        !ce::layers::LayerCatalog::Validate(settings.catalog) || !PhysicsCollisionPolicy::Validate(settings.policy))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid project layer revision or membership"});

    struct change
    {
        entry* body = nullptr;
        ShapeInstance* shape = nullptr;
        character_entry* character = nullptr;
        collision_filter before, after;
    };
    try
    {
        std::vector<change> changes;
        binding_id previous = 0;
        for (const auto& assignment : assignments)
        {
            if (assignment.binding <= previous)
                return std::unexpected(error{error_code::stale_handle, 0, "Invalid layer binding order"});
            previous = assignment.binding;
            const auto filter = settings.policy.Filter(settings.catalog, assignment.layer);
            if (!filter)
                return std::unexpected(error{error_code::invalid_argument, 0, "Unknown or retired entity layer"});

            if (const auto found = m_entries.find(assignment.binding); found != m_entries.end())
            {
                auto& value = found->second;
                for (auto& shape : value.definition.shapes)
                {
                    const auto effective = shape.layer_override.value
                                               ? settings.policy.Filter(settings.catalog, shape.layer_override)
                                               : filter;
                    if (!effective)
                        return std::unexpected(
                            error{error_code::invalid_argument, 0, "Unknown or retired shape layer override"});
                    if (shape.filter.belongs_to != effective->belongs_to ||
                        shape.filter.collides_with != effective->collides_with ||
                        shape.filter.query_layers != effective->query_layers)
                        changes.push_back({&value, &shape, nullptr, shape.filter, *effective});
                }
            }
            else if (const auto found = m_characters.find(assignment.binding); found != m_characters.end())
            {
                auto& value = found->second;
                const auto& capsule = value.definition.capsule;
                if (capsule.belongs_to != filter->belongs_to || capsule.collides_with != filter->collides_with)
                    changes.push_back({nullptr,
                                       nullptr,
                                       &value,
                                       {capsule.belongs_to, capsule.collides_with, capsule.belongs_to},
                                       *filter});
            }
            else
                return std::unexpected(error{error_code::stale_handle, 0, "Unknown layer binding"});
        }

        const auto apply = [&](const change& item, collision_filter filter) -> result<void> {
            if (item.body && item.body->body)
                return m_runtime->set_shape_filter(item.body->body, item.shape->id, filter);
            if (item.character && item.character->handle)
                return m_runtime->set_character_filter(item.character->handle, filter.belongs_to, filter.collides_with);
            return {};
        };
        // One transaction for body and character filters. Definitions publish only after every SDK write.
        for (std::size_t i = 0; i < changes.size(); ++i)
        {
            auto applied = apply(changes[i], changes[i].after);
            if (applied)
                continue;
            for (std::size_t rollback = i; rollback > 0; --rollback)
                if (!apply(changes[rollback - 1], changes[rollback - 1].before))
                {
                    (void)Stop();
                    return std::unexpected(error{error_code::wrong_phase, 0, "Layer rollback failed; physics stopped"});
                }
            return applied;
        }
        for (const auto& item : changes)
        {
            if (item.shape)
                item.shape->filter = item.after;
            else
            {
                item.character->definition.capsule.belongs_to = item.after.belongs_to;
                item.character->definition.capsule.collides_with = item.after.collides_with;
            }
        }
        m_layerRevision = settings.revision;
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Layer commit preparation failed"});
    }
}

result<void> ScenePhysicsSimulation::Start(const scene_config& config)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    if (m_runtime)
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics play session already running"});
    ce::profile_scope scope{ce::marker<"Physics.PlayStart">()};
    try
    {
        auto created = PhysicsScene::create(config);
        if (!created)
            return std::unexpected(created.error());
        auto candidate = std::move(*created);
        std::unordered_map<std::uint64_t, binding_id> handles;
        std::vector<std::pair<binding_id, body_handle>> prepared;
        handles.reserve(m_entries.size());
        prepared.reserve(m_entries.size());
        m_changed.reserve(m_entries.size());
        std::vector<binding_id> ordered;
        ordered.reserve(m_entries.size());
        for (const auto id : m_entries | std::views::keys)
            ordered.push_back(id);
        std::ranges::sort(ordered);
        for (const auto id : ordered)
        {
            const auto& value = m_entries.at(id);
            if (!value.enabled)
                continue;
            const auto body = candidate->create_body(value.definition.view());
            if (!body)
                return std::unexpected(body.error()); // Candidate rolls every earlier body back.
            handles.emplace(Key(*body), id);
            prepared.emplace_back(id, *body);
        }
        std::vector<std::pair<binding_id, character_handle>> preparedCharacters;
        preparedCharacters.reserve(m_characters.size());
        m_changedCharacters.reserve(m_characters.size());
        for (const auto id : m_characterOrder)
        {
            const auto& value = m_characters.at(id);
            if (!value.enabled)
                continue;
            auto created = candidate->create_character(value.definition.capsule);
            if (!created)
                return std::unexpected(created.error());
            preparedCharacters.emplace_back(id, *created);
        }
        for (auto& value : m_characters | std::views::values)
            Reset(value);
        for (const auto& [id, handle] : preparedCharacters)
            m_characters.at(id).handle = handle;
        m_changedCharacters.clear();
        for (auto& [id, value] : m_entries)
            Reset(value);
        for (const auto& [id, body] : prepared)
            m_entries.at(id).body = body;
        m_handles = std::move(handles);
        m_runtime = std::move(candidate);
        m_changed.clear();
        m_accumulator = m_dropped = 0;
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Play preparation allocation failed"});
    }
}

result<void> ScenePhysicsSimulation::Stop()
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    ce::profile_scope scope{ce::marker<"Physics.PlayStop">()};
    m_runtime.reset(); // In-flight fetch/drain and controller/body owners precede authoring restoration.
    m_handles.clear();
    m_changed.clear();
    for (auto& [id, value] : m_entries)
    {
        Reset(value);
        m_changed.push_back({id, value.state.transform}); // Capacity secured by Register/Start.
    }
    m_changedCharacters.clear();
    for (const auto id : m_characterOrder)
    {
        auto& value = m_characters.at(id);
        Reset(value);
        m_changedCharacters.push_back({id, value.state});
    }
    m_accumulator = m_dropped = 0;
    return {};
}

result<std::uint32_t> ScenePhysicsSimulation::Advance(double seconds)
{
    if (auto owner = RequireOwner(); !owner)
        return std::unexpected(owner.error());
    if (!std::isfinite(seconds) || seconds < 0)
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid frame duration"});
    m_changed.clear();
    m_changedCharacters.clear();
    if (!m_runtime || seconds == 0)
        return 0;
    const auto budget = fixed_seconds * max_catchup_ticks;
    const auto accepted = (std::min)(seconds, budget - m_accumulator);
    m_accumulator += accepted;
    const auto discarded = seconds - accepted;
    m_dropped = discarded > (std::numeric_limits<double>::max)() - m_dropped ? (std::numeric_limits<double>::max)()
                                                                             : m_dropped + discarded;
    if (++m_publication == 0)
    {
        for (auto& value : m_entries | std::views::values)
            value.publication = 0;
        m_publication = 1;
    }
    std::uint32_t ticks = 0;
    while (m_accumulator + 1e-12 >= fixed_seconds && ticks < max_catchup_ticks)
    {
        if (auto moved = MoveCharacters(); !moved)
            return std::unexpected(moved.error());
        auto begun = m_runtime->begin_step(static_cast<float>(fixed_seconds));
        if (!begun)
            return std::unexpected(begun.error());
        const auto finished = m_runtime->finish_step();
        if (!finished)
            return std::unexpected(finished.error());
        ++ticks;
        m_accumulator = (std::max)(0.0, m_accumulator - fixed_seconds);
        for (const auto& active : m_runtime->latest_snapshot()->active_poses)
        {
            if (active.body.scene != m_runtime->status().identity)
                continue;
            const auto found = m_handles.find(Key(active.body));
            if (found == m_handles.end())
                continue;
            auto& value = m_entries.at(found->second);
            value.state = active.state;
            if (value.publication != m_publication)
            {
                value.publication = m_publication;
                value.changed_index = m_changed.size();
                m_changed.push_back({found->second, active.state.transform});
            }
            else
                m_changed[value.changed_index].value = active.state.transform;
        }
    }
    if (ticks)
    {
        for (const auto id : m_characterOrder)
        {
            auto& value = m_characters.at(id);
            if (!value.handle)
                continue;
            value.state.tick = m_runtime->status().last_tick;
            m_changedCharacters.push_back({id, value.state});
        }
    }
    return ticks;
}

result<body_state> ScenePhysicsSimulation::Read(binding_id id) const
{
    if (auto owner = RequireOwner(); !owner)
        return std::unexpected(owner.error());
    const auto position = m_entries.find(id);
    if (position == m_entries.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown physics binding"});
    // An active body has SDK-computed mass properties even before its first tick.
    // During simulation callers use the immutable snapshot, rather than reading the SDK.
    if (m_runtime && position->second.body)
        return m_runtime->read_body(position->second.body);
    return position->second.state;
}

body_handle ScenePhysicsSimulation::Handle(binding_id id) const
{
    if (!RequireOwner())
        return {};
    const auto position = m_entries.find(id);
    return position == m_entries.end() ? body_handle{} : position->second.body;
}

result<body_handle> ScenePhysicsSimulation::RequireActiveBody(binding_id id) const
{
    if (auto owner = RequireOwner(); !owner)
        return std::unexpected(owner.error());

    const auto position = m_entries.find(id);
    if (position == m_entries.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown physics binding"});

    if (!m_runtime || !position->second.body)
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics body is not simulating"});

    return position->second.body;
}

result<void> ScenePhysicsSimulation::SetVelocity(binding_id id, math::vector3 linear, math::vector3 angular)
{
    const auto body = RequireActiveBody(id);
    if (!body)
        return std::unexpected(body.error());

    return m_runtime->set_velocity(*body, linear, angular);
}

result<void> ScenePhysicsSimulation::ApplyForce(binding_id id, math::vector3 linear, math::vector3 angular,
                                                force_mode mode)
{
    const auto body = RequireActiveBody(id);
    if (!body)
        return std::unexpected(body.error());

    return m_runtime->apply_force(*body, linear, angular, mode);
}

result<ScenePhysicsSimulation::binding_id> ScenePhysicsSimulation::Binding(body_handle body) const
{
    if (auto owner = RequireOwner(); !owner)
        return std::unexpected(owner.error());
    if (!m_runtime)
        return std::unexpected(error{error_code::wrong_phase, 0, "Physics simulation is not running"});
    if (body.scene != m_runtime->status().identity)
        return std::unexpected(error{error_code::wrong_scene, 0, "Query body belongs to another scene"});
    if (!body)
        return std::unexpected(error{error_code::stale_handle, 0, "Empty query body"});

    const auto found = m_handles.find(Key(body));
    if (found == m_handles.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown or replaced query body"});

    return found->second;
}

namespace
{
bool finite_character_vector(math::vector3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid_character_motion(const ScenePhysicsSimulation::character_definition& value)
{
    return valid_character_desc(value.capsule) && finite_character_vector(value.initial_velocity) &&
           std::isfinite(value.gravity) && std::isfinite(value.initial_fall_velocity) &&
           std::isfinite(value.minimum_distance) && value.minimum_distance >= 0 &&
           valid_motion_parameters(value.motion) && valid_motion_memory(value.initial_motion);
}
} // namespace

void ScenePhysicsSimulation::Reset(character_entry& value)
{
    value.handle = {};
    value.state = {};
    value.state.collision.position = value.definition.capsule.position;
    value.state.collision.foot_position =
        value.definition.capsule.position -
        math::vector3{0,
                      value.definition.capsule.radius + value.definition.capsule.cylinder_height * .5f +
                          value.definition.capsule.contact_offset,
                      0};
    value.state.desired_velocity = value.definition.initial_velocity;
    value.state.fall_velocity = value.definition.initial_fall_velocity;
    value.state.motion = value.definition.initial_motion;
}

result<ScenePhysicsSimulation::binding_id> ScenePhysicsSimulation::RegisterCharacter(character_definition definition,
                                                                                     bool enabled)
{
    if (auto owner = RequireOwner(); !owner)
        return std::unexpected(owner.error());
    if (!valid_character_motion(definition))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid character movement definition"});
    if (m_next == UINT64_MAX)
        return std::unexpected(error{error_code::capacity_exceeded, 0, "Binding IDs exhausted"});

    try
    {
        m_characterOrder.reserve(m_characters.size() + 1);
        m_changedCharacters.reserve(m_characters.size() + 1);
        const auto id = m_next;
        auto [found, inserted] = m_characters.emplace(id, character_entry{definition, {}, {}, enabled});
        (void)inserted;
        Reset(found->second);
        if (m_runtime && enabled)
        {
            auto created = m_runtime->create_character(definition.capsule);
            if (!created)
            {
                m_characters.erase(found);
                return std::unexpected(created.error());
            }
            found->second.handle = *created;
        }
        ++m_next;
        m_characterOrder.push_back(id); // Monotonic bindings give deterministic move order.
        return id;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(error{error_code::out_of_memory, 0, "Character membership allocation failed"});
    }
}

result<void> ScenePhysicsSimulation::UnregisterCharacter(binding_id id)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    const auto found = m_characters.find(id);
    if (found == m_characters.end())
        return {};
    if (found->second.handle)
    {
        if (auto removed = m_runtime->destroy_character(found->second.handle); !removed)
            return removed;
    }
    m_characters.erase(found);
    std::erase(m_characterOrder, id);
    std::erase_if(m_changedCharacters, [id](const auto& value) { return value.binding == id; });
    return {};
}

result<void> ScenePhysicsSimulation::DefineCharacter(binding_id id, character_definition definition)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    if (m_runtime)
        return std::unexpected(error{error_code::wrong_phase, 0, "Character authoring is frozen during play"});
    const auto found = m_characters.find(id);
    if (found == m_characters.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown character binding"});
    if (!valid_character_motion(definition))
        return std::unexpected(error{error_code::invalid_argument, 0, "Invalid character movement definition"});
    found->second.definition = definition;
    Reset(found->second);
    return {};
}

result<void> ScenePhysicsSimulation::SetCharacterEnabled(binding_id id, bool enabled)
{
    if (auto owner = RequireOwner(); !owner)
        return owner;
    const auto found = m_characters.find(id);
    if (found == m_characters.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown character binding"});
    auto& value = found->second;
    if (value.enabled == enabled)
        return {};
    if (m_runtime)
    {
        if (enabled)
        {
            auto definition = value.definition.capsule;
            definition.position = value.state.collision.position;
            auto created = m_runtime->create_character(definition);
            if (!created)
                return std::unexpected(created.error());
            value.handle = *created;
        }
        else
        {
            if (auto removed = m_runtime->destroy_character(value.handle); !removed)
                return removed;
            value.handle = {};
        }
    }
    value.enabled = enabled;
    return {};
}

result<ScenePhysicsSimulation::character_entry*> ScenePhysicsSimulation::ActiveCharacter(binding_id id)
{
    if (auto owner = RequireOwner(); !owner)
        return std::unexpected(owner.error());
    const auto found = m_characters.find(id);
    if (found == m_characters.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown character binding"});
    if (!m_runtime || !found->second.handle || m_runtime->status().phase != scene_phase::idle)
        return std::unexpected(error{error_code::wrong_phase, 0, "Character requires an idle play session"});
    return &found->second;
}

result<void> ScenePhysicsSimulation::SetCharacterVelocity(binding_id id, math::vector3 velocity)
{
    auto value = ActiveCharacter(id);
    if (!value)
        return std::unexpected(value.error());
    if (!finite_character_vector(velocity))
        return std::unexpected(error{error_code::invalid_argument, 0, "Character velocity must be finite"});
    (*value)->state.desired_velocity = velocity;
    return {};
}

result<void> ScenePhysicsSimulation::JumpCharacter(binding_id id)
{
    ce::profile_scope scope{ce::marker<"Physics.CharacterJumpRequest">()};
    auto value = ActiveCharacter(id);
    if (!value)
        return std::unexpected(value.error());
    auto& state = (*value)->state;
    if (!state.collision.below || state.fall_velocity > 0 || state.motion.jump_requested ||
        state.motion.forced_remaining > 0)
        return std::unexpected(
            error{error_code::wrong_phase, 0, "Jump requires completed ground contact and no forced movement"});

    state.motion.jump_requested = true;
    return {};
}

result<void> ScenePhysicsSimulation::ForceCharacter(binding_id id, math::vector3 velocity, double seconds)
{
    ce::profile_scope scope{ce::marker<"Physics.CharacterForceRequest">()};
    auto value = ActiveCharacter(id);
    if (!value)
        return std::unexpected(value.error());
    if (!finite_motion_vector(velocity) || !std::isfinite(seconds) || seconds <= 0)
        return std::unexpected(
            error{error_code::invalid_argument, 0, "Forced movement requires finite velocity and positive seconds"});

    auto& state = (*value)->state;
    state.motion.forced_velocity = velocity;
    state.motion.forced_remaining = seconds;
    state.motion.jump_requested = false;
    return {};
}

result<void> ScenePhysicsSimulation::CancelCharacterForce(binding_id id)
{
    ce::profile_scope scope{ce::marker<"Physics.CharacterForceCancel">()};
    auto value = ActiveCharacter(id);
    if (!value)
        return std::unexpected(value.error());
    (*value)->state.motion.forced_velocity = {};
    (*value)->state.motion.forced_remaining = 0;
    return {};
}

result<void> ScenePhysicsSimulation::TeleportCharacter(binding_id id, math::vector3 position)
{
    auto value = ActiveCharacter(id);
    if (!value)
        return std::unexpected(value.error());
    if (auto moved = m_runtime->teleport_character((*value)->handle, position); !moved)
        return moved;
    auto state = m_runtime->read_character((*value)->handle);
    if (!state)
        return std::unexpected(state.error());
    (*value)->state.collision = *state;
    (*value)->state.fall_velocity = 0;
    (*value)->state.motion = {};
    return {};
}

result<ScenePhysicsSimulation::character_motion_state> ScenePhysicsSimulation::ReadCharacter(binding_id id) const
{
    if (auto owner = RequireOwner(); !owner)
        return std::unexpected(owner.error());
    const auto found = m_characters.find(id);
    if (found == m_characters.end())
        return std::unexpected(error{error_code::stale_handle, 0, "Unknown character binding"});
    return found->second.state;
}

character_handle ScenePhysicsSimulation::CharacterHandle(binding_id id) const
{
    if (!RequireOwner())
        return {};
    const auto found = m_characters.find(id);
    return found == m_characters.end() ? character_handle{} : found->second.handle;
}

result<void> ScenePhysicsSimulation::MoveCharacters()
{
    ce::profile_scope scope{ce::marker<"Physics.CharacterFixedStep">()};
    // Validate every input before the first SDK move. Move order is independent of unordered-map iteration.
    for (const auto id : m_characterOrder)
    {
        const auto& value = m_characters.at(id);
        if (!value.handle)
            continue;
        auto step = prepare_character_motion(value.definition.motion, value.state.motion, value.state.desired_velocity,
                                             value.state.fall_velocity, value.definition.gravity, fixed_seconds);
        if (!step)
            return std::unexpected(step.error());
    }
    for (const auto id : m_characterOrder)
    {
        auto& value = m_characters.at(id);
        if (!value.handle)
            continue;
        auto step = prepare_character_motion(value.definition.motion, value.state.motion, value.state.desired_velocity,
                                             value.state.fall_velocity, value.definition.gravity, fixed_seconds);
        if (!step)
            return std::unexpected(step.error());
        auto moved = m_runtime->move_character(
            value.handle, {step->displacement, static_cast<float>(fixed_seconds), value.definition.minimum_distance});
        if (!moved)
            return std::unexpected(moved.error());
        value.state.collision = *moved;
        value.state.motion = step->motion;
        value.state.fall_velocity =
            (moved->below && step->fall_velocity < 0) || (moved->above && step->fall_velocity > 0)
                ? 0
                : step->fall_velocity;
    }
    return {};
}
